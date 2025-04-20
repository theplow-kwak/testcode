def configure_usb(arch, args, params, index):
    if args.nousb:
        return index

    if arch == "x86_64":
        params += ["-device qemu-xhci,id=xhci1"]
        for i in range(1, 4):
            params += [
                f"-chardev spicevmc,name=usbredir,id=usbredirchardev{i}",
                f"-device usb-redir,bus=xhci1.0,chardev=usbredirchardev{i},id=usbredirdev{i}",
            ]
    else:
        params += ["-device qemu-xhci,id=usb3", "-device usb-kbd", "-device usb-tablet"]
    return index
