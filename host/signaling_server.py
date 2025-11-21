import argparse
import asyncio
import json
import logging
import socket
from aiohttp import web

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

class SignalingServer:
    def __init__(self):
        self.host_ws = None
        self.client_ws = None

    async def handle_websocket(self, request):
        ws = web.WebSocketResponse()
        await ws.prepare(request)

        peer_type = "unknown"
        
        try:
            async for msg in ws:
                if msg.type == web.WSMsgType.TEXT:
                    data = json.loads(msg.data)
                    msg_type = data.get('type')
                    
                    if msg_type == 'register':
                        role = data.get('role')
                        if role == 'host':
                            self.host_ws = ws
                            peer_type = "host"
                            logger.info("Host registered")
                        elif role == 'client':
                            self.client_ws = ws
                            peer_type = "client"
                            logger.info("Client registered")
                            # Notify host that a client connected
                            if self.host_ws:
                                width = data.get('width', 1920)
                                height = data.get('height', 1080)
                                await self.host_ws.send_json({
                                    'type': 'client_connected',
                                    'width': width,
                                    'height': height
                                })
                        
                        await ws.send_json({'type': 'registered', 'role': role})

                    elif msg_type in ['offer', 'answer', 'ice-candidate']:
                        target_ws = self.client_ws if peer_type == 'host' else self.host_ws
                        if target_ws:
                            logger.info(f"Forwarding {msg_type} from {peer_type}")
                            await target_ws.send_str(msg.data)
                        else:
                            logger.warning(f"Cannot forward {msg_type}: Target peer not connected")
                    
                    else:
                        logger.warning(f"Unknown message type: {msg_type}")

                elif msg.type == web.WSMsgType.ERROR:
                    logger.error(f'Websocket connection closed with exception {ws.exception()}')

        finally:
            if peer_type == 'host':
                self.host_ws = None
                logger.info("Host disconnected")
                if self.client_ws:
                    try:
                        await self.client_ws.send_json({'type': 'host_disconnected'})
                    except Exception as e:
                        logger.error(f"Failed to notify client of host disconnection: {e}")

            elif peer_type == 'client':
                self.client_ws = None
                logger.info("Client disconnected")
                if self.host_ws:
                    try:
                        await self.host_ws.send_json({'type': 'client_disconnected'})
                    except Exception as e:
                        logger.error(f"Failed to notify host of client disconnection: {e}")
            
        return ws

def get_local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # doesn't even have to be reachable
        s.connect(('10.255.255.255', 1))
        IP = s.getsockname()[0]
    except Exception:
        IP = '127.0.0.1'
    finally:
        s.close()
    return IP

async def main():
    parser = argparse.ArgumentParser(description="WebRTC Signaling Server")
    parser.add_argument('--port', type=int, default=8080, help="Port to listen on")
    args = parser.parse_args()

    server = SignalingServer()
    app = web.Application()
    app.router.add_get('/ws', server.handle_websocket)

    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, '0.0.0.0', args.port)
    await site.start()
    
    local_ip = get_local_ip()
    print(f"\n{'='*40}")
    print(f"Connect to: {local_ip}")
    print(f"{'='*40}\n")
    
    logger.info(f"Signaling server started on 0.0.0.0:{args.port}")
    
    # Keep the server running
    await asyncio.Event().wait()

if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass