#!/usr/bin/env python3

import argparse
import asyncio
import json
import logging
import time
import threading
from nats.aio.client import Client as NATS

logging.basicConfig(level=logging.INFO, format="%(levelname)s - %(message)s")


class NATSClient:
    def __init__(self, server_url: str = "nats://localhost:4222"):
        self.server_url = server_url
        self.nc = NATS()
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self._start_loop, daemon=True)
        self.heartbeat_task = None
        self.thread.start()
        self.loop.call_soon_threadsafe(self.loop.create_task, self._connect())

    def _start_loop(self):
        """Run asyncio event loop in separate thread"""
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    async def _connect(self):
        """Connect to NATS server"""
        await self.nc.connect(self.server_url)
        logging.info(f"Connected to NATS server at {self.server_url}")

    async def _async_request(self, subject: str, message: str, timeout: int = 10):
        """Send asynchronous request and await response"""
        try:
            response = await self.nc.request(subject, message.encode(), timeout=timeout)
            return response.data.decode()
        except asyncio.TimeoutError:
            logging.warning(f"Request to {subject} timed out")
        except Exception as e:
            logging.debug(f"No responders available for subject: {subject} ({e})")
        return None

    def request(self, subject: str, message: str, timeout: int = 10):
        """Synchronous request interface"""
        future = asyncio.run_coroutine_threadsafe(self._async_request(subject, message, timeout), self.loop)
        return future.result()

    def publish(self, subject: str, message: str):
        """Publish message"""
        self.loop.call_soon_threadsafe(asyncio.create_task, self.nc.publish(subject, message.encode()))

    async def _async_subscribe(self, subject: str, callback):
        """Set up asynchronous subscription"""

        async def message_handler(msg):
            data = msg.data.decode()
            logging.debug(f">>> Received message on {msg.subject}: {data}")
            if msg.reply:
                response = callback(msg.subject, data)
                if response:
                    await msg.respond(response.encode())
                    logging.debug(f"<<< Respond message on {msg.subject}: {response}")
            await asyncio.sleep(0)

        await self.nc.subscribe(subject, cb=message_handler)

    def subscribe(self, subject: str, callback):
        """Synchronous subscription interface"""
        self.loop.call_soon_threadsafe(asyncio.create_task, self._async_subscribe(subject, callback))

    def heartbeat(self, subject: str, interval: int):
        """Start sending heartbeat message periodically"""

        async def _send_heartbeat():
            try:
                while True:
                    await asyncio.sleep(interval)
                    await self.nc.publish(subject, json.dumps({"toolType": "squid"}).encode())
                    logging.debug(f"Heartbeat sent to {subject}")
            except asyncio.CancelledError:
                logging.warning("[HEARTBEAT] Heartbeat task cancelled")

        def _start_heartbeat():
            self.heartbeat_task = asyncio.create_task(_send_heartbeat())

        self.loop.call_soon_threadsafe(_start_heartbeat)

    def close(self):
        """Close NATS connection and stop loop"""

        async def _shutdown():
            if self.heartbeat_task:
                self.heartbeat_task.cancel()
                try:
                    await self.heartbeat_task
                except asyncio.CancelledError:
                    pass
            await self.nc.drain()
            await self.nc.close()
            self.loop.stop()

        self.loop.call_soon_threadsafe(asyncio.create_task, _shutdown())
        self.thread.join()
        logging.debug("NATS connection closed")


def request_handler(subject, data):
    """Callback function to handle request message"""
    print(f"Handling request on {subject} with data: {data}")
    return f"Processed: {data}"


def main():
    parser = argparse.ArgumentParser(description="NATS Client")
    parser.add_argument("-s", "--server", type=str, default="nats://localhost:4222", help="NATS server address")
    args = parser.parse_args()

    nats_client = NATSClient(server_url=args.server)

    # Subscribe to handle incoming requests
    nats_client.subscribe("cero.*.*.*", request_handler)

    # Start heartbeat
    nats_client.heartbeat("squid.squid.system.heartbeat", 5)

    time.sleep(1)

    step = 0
    try:
        while True:
            # Send a request
            subjects = ["squid.sero.dut.getTestProgressCycle", "squid.sero.dut.getTestProgressTime", "squid.sero.dut.getTestProgress"]
            request_subject = subjects[step % len(subjects)]
            step += 1
            response = nats_client.request(request_subject, "Request data")
            if response:
                print(f"Main received response: {response}")

            # Publish test
            nats_client.publish("squid.sero.dut.sendTestProgressTime", "Hello from publisher!")
            time.sleep(2)

    except KeyboardInterrupt:
        print("\nShutting down...")
        nats_client.close()


if __name__ == "__main__":
    main()
