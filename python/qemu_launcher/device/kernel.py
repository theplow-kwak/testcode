from pathlib import Path


def configure_kernel(args, kernel_args, vmimages, arch, connect):
    if not args.vmkernel:
        return

    kernel_args.append(f"-kernel {args.vmkernel}")

    initrd = args.initrd or args.vmkernel.replace("vmlinuz", "initrd.img")
    if Path(initrd).exists():
        kernel_args += ["-initrd", initrd]

    root_dev = "sda" if ".img" in vmimages[0] else "sda1"
    root_dev = args.rootdev or root_dev
    console_type = "console=ttyS0" if connect == "ssh" else "vga=0x300"
    kernel_args.append(f'-append "root=/dev/{root_dev} {console_type}"')
