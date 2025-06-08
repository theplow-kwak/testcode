import logging
import os
import re
import shlex
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
    def LogMsg(msg: str, msgType: int = VM_LOG_MSG_TYPE_INFO, logfd: Optional[object] = None):
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
        self.lastOut = ""
        self.sudo_cmd = "sudo " if os.geteuid() != 0 else ""

    def run_command(self, cmd: str, ignoreError: Optional[bool] = None) -> Optional[str]:
        """Run a shell command and handle errors."""
        cmd_args = shlex.split(cmd)
        ignoreError = ignoreError or self.ignoreError
        try:
            logging.debug(f"Executing command: {cmd_args}")
            if self.background:
                subprocess.Popen(cmd_args)
                return None
            result = subprocess.run(cmd_args, text=True, stdout=subprocess.PIPE if self.return_stdout else None, stderr=subprocess.PIPE)
            self.returncode = result.returncode
            if result.returncode != 0 and not ignoreError:
                raise RuntimeError(f"Command failed: {cmd}\nError: {result.stderr}")
            self.lastOut = result.stdout.strip() if self.return_stdout else None
            return self.lastOut
        except Exception as e:
            if not ignoreError:
                raise RuntimeError(f"Error executing command: {cmd}\n{e}")
            return None

    def sudo_run(self, cmd: str, ignoreError: Optional[bool] = None) -> Optional[str]:
        """Run a shell command with sudo and handle errors."""
        cmd = f"{self.sudo_cmd}{cmd}"
        return self.run_command(cmd, ignoreError=ignoreError)

    def VmExec(self, cmd: str, ignoreError: bool = True) -> str:
        result = self.run_command(cmd, ignoreError=ignoreError)
        return result if result is not None else ""

    def VmExecStatus(self):
        return str(self.returncode)

    def CheckResponse(
        self, pat: str, errMsg: str, string: str = "", exitOnFailure: bool = False, isRegEx: bool = False, ignoreError: bool = False, logfd: Optional[object] = None
    ) -> bool:
        string = string or (self.lastOut or "")

        VmCommon.LogMsg(f'Looking for expected string "{pat}"')
        errMsg = f"{errMsg}\n"
        msgType = VM_LOG_MSG_TYPE_ERROR if not ignoreError else VM_LOG_MSG_TYPE_DIAG
        search_func = re.findall if isRegEx else str.__contains__
        found = bool(search_func(".*%s.*" % pat, string) if isRegEx else search_func(string, pat))

        if not found:
            if exitOnFailure:
                VmCommon.LogMsg(errMsg, msgType, logfd)
                exit(1)
            else:
                VmCommon.LogMsg(errMsg, msgType, logfd)
        else:
            VmCommon.LogMsg("Success - found expected string")

        return found

    def CheckResponse_cnt(
        self, pat: str, pat_count: int, errMsg: str, string: str = "", exitOnFailure: bool = False, isRegEx: bool = False, ignoreError: bool = False, logfd: Optional[object] = None
    ) -> bool:
        string = string or (self.lastOut or "")
        VmCommon.LogMsg(f'Looking for expected string "{pat}" {pat_count} times')
        errMsg = f"{errMsg}\n"
        msgType = VM_LOG_MSG_TYPE_ERROR if not ignoreError else VM_LOG_MSG_TYPE_DIAG
        if isRegEx:
            count = len(re.findall(pat, string))
        else:
            count = string.count(pat)
        found = count == pat_count

        if not found:
            if exitOnFailure:
                VmCommon.LogMsg(errMsg, msgType, logfd)
                exit(1)
            else:
                VmCommon.LogMsg(errMsg, msgType, logfd)
        else:
            VmCommon.LogMsg(f'Success - string "{pat}" occurred {pat_count} times !!')

        return found

    from typing import Callable, Any, List, Optional, Union

    def PackExec(
        self,
        func_cmd: Union[str, Callable[..., Any]],
        pass_word: str = "",
        pass_word_cnt: int = 1,
        arg_list: Optional[List[Any]] = None,
        tcId: str = "",
        tcDesc: str = "testpart",
        fail_check_word: str = "",
        timeoutVal: int = 1200000,
    ) -> Optional[Any]:
        if isinstance(func_cmd, str):
            fail_word = "%s FAILED" % func_cmd
            if not tcId:
                tcId = func_cmd
            logging.info(f"BeginTestCase: {tcId} - {tcDesc}")
            if func_cmd:
                self.VmExec(func_cmd)
            if pass_word:
                if pass_word_cnt == 1:
                    self.CheckResponse(pass_word, fail_word, exitOnFailure=True)
                else:
                    self.CheckResponse_cnt(pass_word, pass_word_cnt, fail_word, exitOnFailure=True)
            if fail_check_word:
                self.CheckNoResponse(fail_check_word, fail_word, exitOnFailure=True)
            logging.info(f"EndTestCase")
        else:
            if not tcId:
                tcId = func_cmd.__name__
            logging.info(f"BeginTestCase: {tcId} - {tcDesc}")
            result = func_cmd(*arg_list) if arg_list else func_cmd()
            logging.info(f"EndTestCase")
            return result

    def CheckNoResponse(
        self, pat: str, errMsg: str, string: str = "", exitOnFailure: bool = False, isRegEx: bool = False, ignoreError: bool = False, logfd: Optional[object] = None
    ) -> bool:
        string = string or (self.lastOut or "")
        VmCommon.LogMsg(f'Ensuring string "{pat}" is NOT present')
        errMsg = f"{errMsg}\n"
        msgType = VM_LOG_MSG_TYPE_ERROR if not ignoreError else VM_LOG_MSG_TYPE_DIAG
        search_func = re.findall if isRegEx else str.__contains__
        found = bool(search_func(".*%s.*" % pat, string) if isRegEx else search_func(string, pat))

        if found:
            if exitOnFailure:
                VmCommon.LogMsg(errMsg, msgType, logfd)
                exit(1)
            else:
                VmCommon.LogMsg(errMsg, msgType, logfd)
        else:
            VmCommon.LogMsg(f'Success - string "{pat}" not found')

        return not found

    @staticmethod
    def command_exists(cmd: str) -> bool:
        """Check if a command exists on the system."""
        return subprocess.call(f"type {cmd}", shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE) == 0
