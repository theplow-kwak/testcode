#!/usr/bin/env python3

import argparse
import asyncio
import datetime
import json
import logging
import time
import threading

from nats.aio.client import Client as NATS

logging.basicConfig(level=logging.INFO, format="%(levelname)s - %(message)s")
logger = logging.getLogger(__name__)


class NATSClient:
    def __init__(self, server_ip: str = "localhost"):
        self.server_url = f"nats://{server_ip}:4222"
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
                logger.info(f"Connected to NATS server attempt {attempt + 1} at {self.server_url}")
                self.connected_event.set()
                return
            except Exception as e:
                logger.warning(f"NATS connection attempt {attempt + 1} failed: {e}")
                await asyncio.sleep(delay)

        self.connected_event.clear()
        logger.warning("NATS connection failed.")

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

    def heartbeat(self, subject: str, callback, interval: int):
        if not self.is_connected():
            return

        async def _send_heartbeat():
            try:
                while True:
                    await asyncio.sleep(interval)
                    await self.nc.publish(subject, callback().encode())
                    logger.debug(f"Heartbeat sent to {subject}")
            except asyncio.CancelledError:
                logger.warning("[HEARTBEAT] Heartbeat task cancelled")

        self.loop.call_soon_threadsafe(asyncio.create_task, _send_heartbeat())

    async def _async_request(self, subject: str, message: str, timeout: int = 10):
        try:
            response = await self.nc.request(subject, message.encode(), timeout=timeout)
            return response.data.decode()
        except Exception as e:
            logger.debug(f"Request to {subject} failed: {e}")
            return None

    async def _async_subscribe(self, subject: str, callback):
        async def message_handler(msg):
            data = msg.data.decode()
            logger.debug(f">>> Received message on {msg.subject}: {data}")
            if msg.reply:
                response = callback(msg.subject, data)
                if response:
                    await msg.respond(response.encode())
                    logger.debug(f"<<< Responded: {response}")
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
        logger.debug("NATS connection closed")


test_start_time = datetime.datetime.now()
test_step = 0
test_loop = 5


def request_handler(subject, data):
    """Callback function to handle request message"""
    parts = subject.split(".")
    if len(parts) != 4:
        return "Invalid subject format"
    test_duration = datetime.datetime.now() - test_start_time
    test_duration = test_duration.total_seconds()
    hours, rest = divmod(test_duration, 3600)
    minutes, seconds = divmod(rest, 60)
    command = parts[3]
    if command == "getTestProgressTime":
        progress_data = {"testHours": int(hours), "testMinutes": int(minutes), "testSeconds": round(seconds, 3)}
        return json.dumps(progress_data)
    elif command == "getTestProgressCycle":
        return json.dumps({"currentTestCycle": test_step + 1, "totalTestCycle": test_loop})
    elif command == "getTestProgress":
        progress_data = {
            "mode": "cycle" if test_loop > 1 else "time",
            "currentTestCycle": test_step + 1,
            "totalTestCycle": test_loop,
            "testHours": int(hours),
            "testMinutes": int(minutes),
            "testSeconds": round(seconds, 3),
        }
        return json.dumps(progress_data)
    else:
        return "Unknown command"


def heartbeat_handler():
    return json.dumps({"toolType": "squid.test.client"})


def main():
    parser = argparse.ArgumentParser(description="NATS Client")
    parser.add_argument("-s", "--server", type=str, default="localhost", help="NATS server address")
    parser.add_argument("-c", "--client", help="Used client mode for testing", action="store_true")
    parser.add_argument("-l", "--log", type=str, default="info", help="Set logging level (debug, info, warning, error, critical)")
    args = parser.parse_args()
    logger.setLevel(args.log.upper())

    nats_client = NATSClient(server_ip=args.server)

    nats_client.wait_until_ready(timeout=10)
    if not nats_client.is_connected():
        logger.warning("Initial connection failed. Continuing...")
        nats_client.close()

    if args.client:
        server_name = "squid"
        client_name = "cero.test"
    else:
        server_name = "cero"
        client_name = "squid.test"

    nats_client.subscribe(f"{server_name}.*.*.*", request_handler)
    if args.client:
        nats_client.heartbeat(f"{client_name}.system.heartbeat", heartbeat_handler, 10)

    step = 0
    try:
        while True:
            if not args.client:
                subjects = [f"{client_name}.dut.getTestProgressCycle", f"{client_name}.dut.getTestProgressTime", f"{client_name}.dut.getTestProgress"]
                subject = subjects[step % len(subjects)]
                step += 1
                response = nats_client.request(subject, "Request data")
                if response:
                    print(f"Main received response: {response}")
            else:
                nats_client.publish(f"{client_name}.dut.sendTestProgressTime", "Hello from publisher!")

            time.sleep(2)

    except KeyboardInterrupt:
        print("\nShutting down...")
        nats_client.close()


if __name__ == "__main__":
    main()
