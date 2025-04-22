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
        params += ["-device qemu-xhci,id=xhci2", "-device usb-host,bus=xhci2.0,vendorid=0x04e8,productid=0x6860"]
    else:
        params += ["-device qemu-xhci,id=usb3", "-device usb-kbd", "-device usb-tablet"]
    return index
