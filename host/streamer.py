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
from gi.repository import Gst, GstWebRTC, GstSdp, GLib

from aiohttp import ClientSession

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
                    logger.info("Client connected. Creating offer...")
                    promise = Gst.Promise.new_with_change_func(self.on_offer_created, None, None)
                    self.webrtcbin.emit('create-offer', None, promise)

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
            # Send x, y as 4-byte integers (big endian)
            data = struct.pack('>II', int(x), int(y))
            # GStreamer Data Channel send_string or send_data?
            # In python gi, it's often send_string for text, but for binary...
            # We need to create a GBytes
            gbytes = GLib.Bytes.new(data)
            self.data_channel.emit('send-data', gbytes)
            
            self.cursor_packet_count += 1
            if self.cursor_packet_count % 60 == 0:
                print(f"Sent cursor data: {x}, {y}")

    def build_pipeline(self):
        # Pipeline: pipewiresrc ! vp8enc (Verify Source)
        pipeline_str = (
            "pipewiresrc ! "
            "videoconvert ! "
            "vp8enc ! "
            "rtpvp8pay ! "
            "webrtcbin name=sendrecv bundle-policy=max-bundle stun-server=stun://stun.l.google.com:19302"
        )
        
        logger.info(f"Building pipeline: {pipeline_str}")
        self.pipeline = Gst.parse_launch(pipeline_str)
        self.webrtcbin = self.pipeline.get_by_name('sendrecv')
        
        self.webrtcbin.connect('on-ice-candidate', self.on_ice_candidate)
        self.webrtcbin.connect('on-negotiation-needed', self.on_negotiation_needed)
        self.webrtcbin.connect('on-data-channel', self.on_data_channel)
        
        # Create data channel
        self.webrtcbin.emit('create-data-channel', 'cursor', None)
        
        # Add probe to measure FPS
        sink_pad = self.webrtcbin.get_static_pad('sink_0')
        if not sink_pad:
             # Try to find the sink pad connected to rtph264pay
             rtppay = self.pipeline.get_by_name('rtph264pay0') # Gst.parse_launch might name it rtph264pay0
             if not rtppay:
                 # Iterate elements to find rtph264pay
                 iter = self.pipeline.iterate_elements()
                 while True:
                     res, elem = iter.next()
                     if res != Gst.IteratorResult.OK: break
                     if "rtph264pay" in elem.get_name():
                         rtppay = elem
                         break
             
             if rtppay:
                 src_pad = rtppay.get_static_pad('src')
                 src_pad.add_probe(Gst.PadProbeType.BUFFER, self.fps_probe, None)
             else:
                 logger.warning("Could not find rtph264pay to attach FPS probe")

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
    await streamer.run()

if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass