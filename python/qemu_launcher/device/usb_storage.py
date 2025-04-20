from pathlib import Path


def configure_usb_storage(args, params, index=0):
    if args.stick and Path(args.stick).exists():
        params += [
            f"-drive file={args.stick},if=none,format=raw,id=stick{index}",
            f"-device usb-storage,drive=stick{index}",
        ]
