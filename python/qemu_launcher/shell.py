import shlex
import subprocess
import logging
from time import sleep

logger = logging.getLogger("QEMU")


def run_command(cmd, async_=False, console=False):
    if isinstance(cmd, list):
        cmd = " ".join(cmd)

    logger.debug(f"Running shell command: {cmd}")
    parsed_cmd = shlex.split(cmd)

    if async_:
        proc = subprocess.Popen(parsed_cmd) if not console else subprocess.run(parsed_cmd, text=True)
        sleep(1)
        return proc
    else:
        proc = subprocess.run(parsed_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) if not console else subprocess.run(parsed_cmd, text=True)

        if proc.stdout:
            logger.debug(f"Return code: {proc.returncode}, output: {proc.stdout.strip()}")
        return proc
