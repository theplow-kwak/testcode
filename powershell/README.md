# /etc/udev/rules.d/99-ssandroid-mtp.rules
# SUBSYSTEM=="usb", ATTR{idVendor}=="04e8", ATTR{idProduct}=="6860", ENV{ID_MTP_DEVICE}="0", SYMLINK+="galuxy", MODE="0666"
# sudo udevadm test /dev/bus/usb/003/010

#!/bin/bash
# gio mount -u "mtp://SAMSUNG_SAMSUNG_Android_R3CW10AS61H/"


첨부한 엑셀파일을 분석하고 아래 요구 사항을 처리하는 powershell script를 작성해줘
1. smart_before, smart_after 두개의 데이터 값을 비교할꺼야
2. 비교하는 조건은 첨부의 엑셀 파일에 define 되어있으니, COMObject를 사용하여 Excel 파일을 읽어서 비교 조건을 확인하도록 하고
3. customer별로 나누어서 비교하고, customer가 지정되면 해당 customer와 NVME 항목에 대해서만 비교해줘, 예를 들어 customer가 DELL이면 NVME, DELL에 해당하는 항목만 비교하도록 할꺼야
4. byte_offset에 해당하는 값이 비교 조건에 부합하면 criteria에 있는대로 판정하면 돼
5. 비교 조건은 product 별로 지정되어 있고, 여러개의 조건이 ';'로 분리되어 하나의 셀에 define 되어 있을수 있어