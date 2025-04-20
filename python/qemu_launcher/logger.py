import logging
import os
from pathlib import Path


def set_logger(log_name="QEMU", log_file="/tmp/qemu.log"):
    logger = logging.getLogger(log_name)
    if logger.handlers:
        return logger

    log_file = Path(log_file)
    log_file.parent.mkdir(parents=True, exist_ok=True)

    file_handler = logging.FileHandler(log_file)
    stream_handler = logging.StreamHandler()

    formatter = logging.Formatter("%(asctime)s [%(name)s] %(levelname)s: %(message)s")
    file_handler.setFormatter(formatter)
    stream_handler.setFormatter(formatter)

    logger.addHandler(file_handler)
    logger.addHandler(stream_handler)
    logger.setLevel(logging.WARNING)

    return logger
