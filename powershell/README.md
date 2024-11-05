# /etc/udev/rules.d/99-ssandroid-mtp.rules
# SUBSYSTEM=="usb", ATTR{idVendor}=="04e8", ATTR{idProduct}=="6860", ENV{ID_MTP_DEVICE}="0", SYMLINK+="galuxy", MODE="0666"
# sudo udevadm test /dev/bus/usb/003/010

#!/bin/bash
# gio mount -u "mtp://SAMSUNG_SAMSUNG_Android_R3CW10AS61H/"
