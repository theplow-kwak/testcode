#!/usr/bin/env python3

from qemu_launcher.core import QemuLauncher


def main():
    launcher = QemuLauncher()
    launcher.setup()
    launcher.run()


if __name__ == "__main__":
    import sys
    import traceback
    import logging

    from qemu_launcher.logger import set_logger

    set_logger("QEMU")
    logger = logging.getLogger("QEMU")

    try:
        main()
    except KeyboardInterrupt:
        logger.error("Keyboard Interrupted")
    except Exception as e:
        logger.error(f"QEMU terminated abnormally. {e}")
        ex_type, ex_value, ex_traceback = sys.exc_info()
        trace_back = traceback.extract_tb(ex_traceback)
        for trace in trace_back[1:]:
            logger.error(f"  File {trace[0]}, line {trace[1]}, in {trace[2]}")