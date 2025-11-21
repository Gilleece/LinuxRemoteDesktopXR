import argparse
import asyncio
import json
import logging
import sys
import time
import threading
import struct

from pynput import mouse

import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstWebRTC', '1.0')
gi.require_version('GstSdp', '1.0')
try:
    gi.require_version('Gdk', '3.0')
except ValueError:
    pass # Might be already loaded or not needed if Gtk is used
from gi.repository import Gst, GstWebRTC, GstSdp, GLib, Gdk

from aiohttp import ClientSession
from display_manager import DisplayManager

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

class WebRTCStreamer:
    def __init__(self, signaling_url):
        self.signaling_url = signaling_url
        self.pipeline = None
        self.webrtcbin = None
        self.session = None
        self.ws = None
        self.loop = asyncio.get_event_loop()
        self.glib_loop = GLib.MainLoop()
        self.frame_count = 0
        self.last_time = time.time()
        self.data_channel = None
        self.mouse_listener = None
        self.cursor_packet_count = 0
        self.display_manager = DisplayManager()
        
        # Get screen resolution for normalization
        self.update_screen_resolution()

    def update_screen_resolution(self):
        display = Gdk.Display.get_default()
        monitor = display.get_primary_monitor()
        geometry = monitor.get_geometry()
        self.screen_width = geometry.width
        self.screen_height = geometry.height
        logger.info(f"Screen resolution: {self.screen_width}x{self.screen_height}")

    def start_glib_loop(self):
        self.glib_loop.run()

    async def connect_signaling(self):
        self.session = ClientSession()
        try:
            self.ws = await self.session.ws_connect(self.signaling_url)
            await self.ws.send_json({'type': 'register', 'role': 'host'})
            logger.info("Connected to signaling server")
            asyncio.create_task(self.handle_messages())
        except Exception as e:
            logger.error(f"Failed to connect to signaling server: {e}")
            sys.exit(1)

    async def handle_messages(self):
        async for msg in self.ws:
            if msg.type == 1: # TEXT
                data = json.loads(msg.data)
                msg_type = data.get('type')
                
                if msg_type == 'offer':
                    logger.info("Received offer")
                    self.handle_offer(data['sdp'])
                elif msg_type == 'answer':
                    logger.info("Received answer")
                    self.handle_answer(data['sdp'])
                elif msg_type == 'ice-candidate':
                    logger.info("Received ICE candidate")
                    self.handle_ice_candidate(data['candidate'], data['sdpMid'], data['sdpMLineIndex'])
                elif msg_type == 'registered':
                    logger.info("Registered as host.")
                elif msg_type == 'client_connected':
                    logger.info("Client connected. Switching resolution and creating offer...")
                    
                    # Switch resolution to 1440p
                    if self.display_manager.create_and_set_mode(2560, 1440):
                        # Give X11 a moment to settle?
                        time.sleep(2) # Increased sleep time
                        self.update_screen_resolution()
                    
                    # We might need to restart the pipeline or at least ximagesrc to pick up the new resolution?
                    # ximagesrc usually adapts, but let's see.
                    # If the pipeline was already running (it is, in PLAYING state), changing resolution might cause error.
                    # We should probably pause/stop pipeline, change resolution, then restart/resume.
                    
                    self.pipeline.set_state(Gst.State.NULL)
                    time.sleep(0.5)
                    self.pipeline.set_state(Gst.State.PLAYING)
                    
                    # Re-create data channel since we restarted pipeline? 
                    # Actually webrtcbin is reset too.
                    # We need to wait for it to be ready again?
                    # But we are about to create an offer.
                    
                    # Wait a bit for pipeline to start up
                    time.sleep(1)
                    
                    # Re-create data channel
                    self.webrtcbin.emit('create-data-channel', 'cursor', None)

                    promise = Gst.Promise.new_with_change_func(self.on_offer_created, None, None)
                    self.webrtcbin.emit('create-offer', None, promise)

                elif msg_type == 'client_disconnected':
                    logger.info("Client disconnected (signaling).")
                    self.handle_client_disconnect()

    def on_offer_created(self, promise, *args):
        promise.wait()
        reply = promise.get_reply()
        if not reply:
            logger.error("Failed to create offer")
            return
        offer = reply.get_value('offer')
        promise = Gst.Promise.new_with_change_func(self.on_local_description_set, None, None)
        self.webrtcbin.emit('set-local-description', offer, promise)
        
        sdp = offer.sdp.as_text()
        logger.info("Sending offer")
        asyncio.run_coroutine_threadsafe(
            self.ws.send_json({'type': 'offer', 'sdp': sdp}),
            self.loop
        )

    def handle_offer(self, sdp):
        res, sdp_msg = GstSdp.SDPMessage.new_from_text(sdp)
        offer = GstWebRTC.WebRTCSessionDescription.new(GstWebRTC.WebRTCSDPType.OFFER, sdp_msg)
        self.webrtcbin.emit('set-remote-description', offer, None)

    def handle_answer(self, sdp):
        res, sdp_msg = GstSdp.SDPMessage.new_from_text(sdp)
        answer = GstWebRTC.WebRTCSessionDescription.new(GstWebRTC.WebRTCSDPType.ANSWER, sdp_msg)
        promise = Gst.Promise.new_with_change_func(self.on_remote_description_set, None, None)
        self.webrtcbin.emit('set-remote-description', answer, promise)

    def on_local_description_set(self, promise, *args):
        promise.wait()
        reply = promise.get_reply()
        if reply:
            logger.info("Local description set")
        else:
            logger.error("Failed to set local description")

    def on_remote_description_set(self, promise, *args):
        promise.wait()
        reply = promise.get_reply()
        if reply:
            logger.info("Remote description set")
        else:
            logger.error("Failed to set remote description")

    def handle_ice_candidate(self, candidate, sdp_mid, sdp_mline_index):
        # Try with 3 args first, if fails, try with 2 (older GStreamer)
        try:
            self.webrtcbin.emit('add-ice-candidate', sdp_mline_index, sdp_mid, candidate)
        except TypeError:
            self.webrtcbin.emit('add-ice-candidate', sdp_mline_index, candidate)

    def on_ice_candidate(self, _, mline_index, candidate):
        logger.info("Sending ICE candidate")
        asyncio.run_coroutine_threadsafe(
            self.ws.send_json({
                'type': 'ice-candidate',
                'candidate': candidate,
                'sdpMid': "video0", # Assuming video0 for now
                'sdpMLineIndex': mline_index
            }),
            self.loop
        )

    def on_ice_connection_state_notify(self, webrtcbin, pspec):
        state = webrtcbin.get_property("ice-connection-state")
        logger.info(f"ICE connection state changed to: {state}")
        if state in [GstWebRTC.WebRTCICEConnectionState.FAILED, 
                     GstWebRTC.WebRTCICEConnectionState.CLOSED,
                     GstWebRTC.WebRTCICEConnectionState.DISCONNECTED]:
             self.handle_client_disconnect()

    def handle_client_disconnect(self):
        logger.info("Client disconnected. Restoring resolution.")
        self.display_manager.restore()
        self.update_screen_resolution()

    def on_negotiation_needed(self, element):
        logger.info("Negotiation needed")

    def on_data_channel(self, webrtc, channel):
        logger.info(f"Data channel created: {channel.get_property('label')}")
        self.data_channel = channel
        self.start_mouse_capture()

    def start_mouse_capture(self):
        if self.mouse_listener:
            return
        
        logger.info("Starting mouse capture")
        self.mouse_listener = mouse.Listener(on_move=self.on_mouse_move)
        self.mouse_listener.start()

    def on_mouse_move(self, x, y):
        if self.data_channel and self.data_channel.get_property('ready-state') == GstWebRTC.WebRTCDataChannelState.OPEN:
            # Normalize coordinates
            norm_x = x / self.screen_width
            norm_y = y / self.screen_height
            
            # Send norm_x, norm_y as 4-byte floats (big endian)
            data = struct.pack('>ff', float(norm_x), float(norm_y))
            
            # GStreamer Data Channel send_string or send_data?
            # In python gi, it's often send_string for text, but for binary...
            # We need to create a GBytes
            gbytes = GLib.Bytes.new(data)
            self.data_channel.emit('send-data', gbytes)
            
            self.cursor_packet_count += 1
            if self.cursor_packet_count % 60 == 0:
                # print(f"Sent cursor data: {x}, {y} ({norm_x:.2f}, {norm_y:.2f})")
                pass

    def build_pipeline(self):
        self.pipeline = Gst.Pipeline.new("pipeline")

        try:
            ximagesrc = Gst.ElementFactory.make("ximagesrc", "ximagesrc0")
            ximagesrc.set_property("use-damage", False) # Required for capturing the full screen

            videoconvert = Gst.ElementFactory.make("videoconvert", "videoconvert0")
            
            # Add videorate to enforce stable framerate
            videorate = Gst.ElementFactory.make("videorate", "videorate0")
            
            # Add capsfilter to ensure compatible format for VAAPI (NV12 is standard) and enforce 30fps
            capsfilter = Gst.ElementFactory.make("capsfilter", "capsfilter0")
            caps = Gst.Caps.from_string("video/x-raw,framerate=30/1") 
            capsfilter.set_property("caps", caps)
            
            # Add vaapipostproc for hardware accelerated color conversion/scaling if possible
            # This helps offload CPU from videoconvert
            try:
                vaapipostproc = Gst.ElementFactory.make("vaapipostproc", "vaapipostproc0")
            except:
                vaapipostproc = None
                logger.warning("vaapipostproc not found, using software conversion only")

            # Configure queue to be leaky to prevent freezing/buffering
            queue = Gst.ElementFactory.make("queue", "queue0")
            queue.set_property("max-size-buffers", 1)
            queue.set_property("leaky", 2) # 2 = downstream (drop new buffers if full)

            # Configure encoder for better quality
            vaapih264enc = Gst.ElementFactory.make("vaapih264enc", "vaapih264enc0")
            vaapih264enc.set_property("bitrate", 4000) # 4 Mbps - Lowered for stability
            # vaapih264enc.set_property("rate-control", 2) # CBR
            vaapih264enc.set_property("keyframe-period", 30) # Keyframe every 30 frames (1 sec)
            
            rtph264pay = Gst.ElementFactory.make("rtph264pay", "rtph264pay0")
            rtph264pay.set_property("config-interval", 1) # Send SPS/PPS every keyframe
            
            self.webrtcbin = Gst.ElementFactory.make("webrtcbin", "sendrecv")
            self.webrtcbin.set_property("bundle-policy", "max-bundle")
            self.webrtcbin.set_property("stun-server", "stun://stun.l.google.com:19302")
            
            elements = [ximagesrc, videoconvert, videorate, capsfilter]
            if vaapipostproc:
                elements.append(vaapipostproc)
            elements.extend([queue, vaapih264enc, rtph264pay, self.webrtcbin])

            for elem in elements:
                self.pipeline.add(elem)

            if not ximagesrc.link(videoconvert):
                logger.error("Failed to link ximagesrc to videoconvert")
                return
            if not videoconvert.link(videorate):
                logger.error("Failed to link videoconvert to videorate")
                return
            if not videorate.link(capsfilter):
                logger.error("Failed to link videorate to capsfilter")
                return
            
            last_elem = capsfilter
            if vaapipostproc:
                if not capsfilter.link(vaapipostproc):
                    logger.error("Failed to link capsfilter to vaapipostproc")
                    return
                last_elem = vaapipostproc
            
            if not last_elem.link(queue):
                logger.error("Failed to link to queue")
                return
            if not queue.link(vaapih264enc):
                logger.error("Failed to link queue to vaapih264enc")
                return
            if not vaapih264enc.link(rtph264pay):
                logger.error("Failed to link vaapih264enc to rtph264pay")
                return
            rtph264pay.link(self.webrtcbin)

        except (Exception, GLib.GError) as e:
            logger.error(f"Failed to build pipeline: {e}")
            sys.exit(1)

        self.webrtcbin.connect('on-ice-candidate', self.on_ice_candidate)
        self.webrtcbin.connect('on-negotiation-needed', self.on_negotiation_needed)
        self.webrtcbin.connect('on-data-channel', self.on_data_channel)
        self.webrtcbin.connect('notify::ice-connection-state', self.on_ice_connection_state_notify)
        
        # Moved create-data-channel to start() to ensure element is ready
        
        rtppay_src_pad = rtph264pay.get_static_pad('src')
        rtppay_src_pad.add_probe(Gst.PadProbeType.BUFFER, self.fps_probe, None)

    def fps_probe(self, pad, info, user_data):
        self.frame_count += 1
        current_time = time.time()
        if current_time - self.last_time >= 1.0:
            fps = self.frame_count / (current_time - self.last_time)
            print(f"Pipeline active - Streaming at {fps:.2f} FPS")
            self.frame_count = 0
            self.last_time = current_time
        return Gst.PadProbeReturn.OK

    def bus_call(self, bus, msg, *args):
        if msg.type == Gst.MessageType.ERROR:
            err, debug = msg.parse_error()
            logger.error(f"Pipeline error: {err}, {debug}")
            sys.exit(1)
        elif msg.type == Gst.MessageType.WARNING:
            err, debug = msg.parse_warning()
            logger.warning(f"Pipeline warning: {err}, {debug}")
        elif msg.type == Gst.MessageType.EOS:
            logger.info("End of stream")
            sys.exit(0)
        return True

    def start(self):
        Gst.init(None)
        self.build_pipeline()
        
        bus = self.pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect('message', self.bus_call, None)
        
        self.pipeline.set_state(Gst.State.PLAYING)
        logger.info("Pipeline started")
        
        # Create data channel after pipeline is playing to avoid assertion errors
        # self.webrtcbin.emit('create-data-channel', 'cursor', None) 
        # Defer data channel creation until we actually connect? 
        # Or just create it here and hope it persists through restart? 
        # If we restart pipeline in client_connected, this one is lost anyway.
        # So let's just leave it here for initial state, but handle re-creation later.
        self.webrtcbin.emit('create-data-channel', 'cursor', None)
        
        # Start GLib loop in a separate thread
        t = threading.Thread(target=self.start_glib_loop)
        t.daemon = True
        t.start()

    async def run(self):
        self.start()
        await self.connect_signaling()
        
        # Keep running
        while True:
            await asyncio.sleep(1)

async def main():
    parser = argparse.ArgumentParser(description="WebRTC Host Streamer")
    parser.add_argument('--signaling', default='http://127.0.0.1:8080/ws', help="Signaling server URL")
    args = parser.parse_args()

    streamer = WebRTCStreamer(args.signaling)
    try:
        await streamer.run()
    finally:
        streamer.display_manager.restore()

if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass