import logging
import os
import subprocess
from typing import Optional

logging.basicConfig(level=logging.INFO, format="[%(levelname)s] %(message)s")


VM_LOG_MSG_TYPE_CRITICAL = 50
VM_LOG_MSG_TYPE_FATAL = VM_LOG_MSG_TYPE_CRITICAL
VM_LOG_MSG_TYPE_ERROR = 40
VM_LOG_MSG_TYPE_WARNING = 30
VM_LOG_MSG_TYPE_WARN = VM_LOG_MSG_TYPE_WARNING
VM_LOG_MSG_TYPE_INFO = 20
VM_LOG_MSG_TYPE_DEBUG = 10
VM_LOG_MSG_TYPE_DIAG = VM_LOG_MSG_TYPE_DEBUG
VM_LOG_MSG_TYPE_NOTSET = 0


class VmCommon:
    @staticmethod
    def LogMsg(msg: str, msgType: int):
        if msgType in (VM_LOG_MSG_TYPE_CRITICAL, VM_LOG_MSG_TYPE_FATAL):
            logging.critical(msg)
        elif msgType == VM_LOG_MSG_TYPE_ERROR:
            logging.error(msg)
        elif msgType in (VM_LOG_MSG_TYPE_WARNING, VM_LOG_MSG_TYPE_WARN):
            logging.warning(msg)
        elif msgType == VM_LOG_MSG_TYPE_INFO:
            logging.info(msg)
        elif msgType in (VM_LOG_MSG_TYPE_DEBUG, VM_LOG_MSG_TYPE_DIAG, VM_LOG_MSG_TYPE_NOTSET):
            logging.debug(msg)
        else:
            logging.info(msg)


class CommandRunner:
    def __init__(self, ignoreError: bool = False, return_stdout: bool = True, background: bool = False):
        self.ignoreError = ignoreError
        self.return_stdout = return_stdout
        self.background = background
        self.returncode = None
        self.sudo_cmd = "sudo " if os.geteuid() != 0 else ""

    def run_command(self, cmd: str, ignoreError=None) -> Optional[str]:
        """Run a shell command and handle errors."""
        cmd = f"{self.sudo_cmd}{cmd}"
        ignoreError = ignoreError or self.ignoreError
        try:
            logging.debug(f"Executing command: {cmd}")
            if self.background:
                subprocess.Popen(cmd, shell=True)
                return None
            result = subprocess.run(cmd, shell=True, text=True, stdout=subprocess.PIPE if self.return_stdout else None, stderr=subprocess.PIPE)
            self.returncode = result.returncode
            if result.returncode != 0 and not ignoreError:
                raise RuntimeError(f"Command failed: {cmd}\nError: {result.stderr}")
            return result.stdout.strip() if self.return_stdout else None
        except Exception as e:
            if not ignoreError:
                raise RuntimeError(f"Error executing command: {cmd}\n{e}")
            return None

    def VmExec(self, cmd: str, ignoreError: bool = True) -> str:
        return self.run_command(cmd, ignoreError=ignoreError)

    def VmExecStatus(self):
        return str(self.returncode)

    @staticmethod
    def command_exists(cmd: str) -> bool:
        """Check if a command exists on the system."""
        return subprocess.call(f"type {cmd}", shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE) == 0
