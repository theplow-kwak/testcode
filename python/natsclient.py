#!/usr/bin/env python3

import argparse
import asyncio
import json
import logging
import threading
import time

from nats.aio.client import Client as NATS

# 로깅 설정
logging.basicConfig(level=logging.INFO, format="%(levelname)s - %(message)s")
# logging.getLogger("nats").setLevel(logging.FATAL)


class NATSClient:
    def __init__(self, server_url: str = "nats://localhost:4222"):
        self.server_url = server_url
        self.nc = NATS()
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self._start_loop, daemon=True)
        self.heartbeat_task = None
        self.connected_event = threading.Event()
        self.thread.start()
        self.loop.call_soon_threadsafe(self.loop.create_task, self._connect())

    def _start_loop(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    async def _error_cb(self, msg):
        raise Exception(msg)

    async def _connect(self, retries: int = 3, delay: int = 1):
        for attempt in range(retries):
            try:
                await self.nc.connect(servers=[self.server_url], error_cb=self._error_cb, allow_reconnect=False)
                logging.info(f"Connected to NATS server attempt {attempt + 1} at {self.server_url}")
                self.connected_event.set()
                return
            except Exception as e:
                logging.warning(f"NATS connection attempt {attempt + 1} failed: {e}")
                await asyncio.sleep(delay)

        self.connected_event.clear()
        logging.warning("NATS connection failed.")

    def is_connected(self):
        return self.nc.is_connected

    def wait_until_ready(self, timeout: int = 10):
        self.connected_event.wait(timeout)

    def request(self, subject: str, message: str, timeout: int = 10):
        if not self.is_connected():
            return None
        future = asyncio.run_coroutine_threadsafe(self._async_request(subject, message, timeout), self.loop)
        return future.result()

    def publish(self, subject: str, message: str):
        if not self.is_connected():
            return
        self.loop.call_soon_threadsafe(asyncio.create_task, self.nc.publish(subject, message.encode()))

    def subscribe(self, subject: str, callback):
        if not self.is_connected():
            return
        self.loop.call_soon_threadsafe(asyncio.create_task, self._async_subscribe(subject, callback))

    def heartbeat(self, subject: str, interval: int):
        if not self.is_connected():
            return

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

    async def _async_request(self, subject: str, message: str, timeout: int = 10):
        try:
            response = await self.nc.request(subject, message.encode(), timeout=timeout)
            return response.data.decode()
        except Exception as e:
            logging.debug(f"Request to {subject} failed: {e}")
            return None

    async def _async_subscribe(self, subject: str, callback):
        async def message_handler(msg):
            response = callback(msg)
            if response:
                await msg.respond(response.encode())
            await asyncio.sleep(0)

        await self.nc.subscribe(subject, cb=message_handler)

    def close(self):
        async def _shutdown():
            if self.heartbeat_task:
                self.heartbeat_task.cancel()
                try:
                    await self.heartbeat_task
                except asyncio.CancelledError:
                    pass
            if self.nc.is_connected:
                await self.nc.drain()
                await self.nc.close()
            self.loop.stop()

        self.loop.call_soon_threadsafe(asyncio.create_task, _shutdown())
        self.thread.join()
        logging.debug("NATS connection closed")


def request_handler(msg):
    data = msg.data.decode()
    subject = msg.subject
    if msg.reply:
        print(f"Handling request on {subject} with data: {data}")
        return f"Processed: {data}"
    else:
        print(f"Received published message on {subject} with data: {data}")
        return None


def main():
    parser = argparse.ArgumentParser(description="NATS Client")
    parser.add_argument("-s", "--server", type=str, default="nats://localhost:4222", help="NATS server address")
    args = parser.parse_args()

    nats_client = NATSClient(server_url=args.server)

    nats_client.wait_until_ready(timeout=10)
    if not nats_client.is_connected():
        logging.warning("Initial connection failed. Continuing...")
        nats_client.close()

    nats_client.subscribe("srv.*.*.*", request_handler)
    nats_client.heartbeat("client.test.system.heartbeat", 5)

    step = 0
    try:
        while True:
            subjects = ["client.test.dut.getTestProgressCycle", "client.test.dut.getTestProgressTime", "client.test.dut.getTestProgress"]
            subject = subjects[step % len(subjects)]
            step += 1

            response = nats_client.request(subject, "Request data")
            if response:
                print(f"Main received response: {response}")

            nats_client.publish("client.test.dut.sendTestProgressTime", "Hello from publisher!")
            print("Main routine working...")
            time.sleep(2)

    except KeyboardInterrupt:
        print("\nShutting down...")
        nats_client.close()


if __name__ == "__main__":
    main()
