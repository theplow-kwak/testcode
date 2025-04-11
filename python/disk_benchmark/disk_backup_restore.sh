#!/bin/bash

set -e

COMMAND="$1"
shift || true

OPTS=$(getopt -o d:i:f --long disk:,image:,force -n 'disk_backup_restore.sh' -- "$@")
if [ $? != 0 ]; then
    echo "Failed to parse options."
    exit 1
fi
eval set -- "$OPTS"

FORCE=0
DISK=""
IMAGE=""

while true; do
    case "$1" in
        -d|--disk) DISK="$2"; shift 2 ;;
        -i|--image) IMAGE="$2"; shift 2 ;;
        -f|--force) FORCE=1; shift ;;
        --) shift; break ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

case "$COMMAND" in
    backup)
        if [ -z "$DISK" ] || [ -z "$IMAGE" ]; then
            echo "Usage: $0 backup --disk <device> --image <output.qcow2>"
            exit 1
        fi

        if [ ! -b "$DISK" ]; then
            echo "[ERROR] Disk $DISK does not exist."
            exit 1
        fi

        echo "[*] Backing up $DISK to $IMAGE ..."
        sudo qemu-img convert -c -f raw -O qcow2 "$DISK" "$IMAGE"
        echo "[DONE] Backup completed."
        ;;

    restore)
        if [ -z "$DISK" ] || [ -z "$IMAGE" ]; then
            echo "Usage: $0 restore --disk <device> --image <input.qcow2> [--force]"
            exit 1
        fi

        if [ ! -f "$IMAGE" ]; then
            echo "[ERROR] Image file $IMAGE does not exist."
            exit 1
        fi

        if [ "$FORCE" -ne 1 ]; then
            echo "[WARNING] All data on $DISK will be erased. Restoring in 5 seconds..."
            sleep 5
        fi

        echo "[*] Restoring $IMAGE to $DISK ..."
        sudo qemu-img convert -f qcow2 -O raw "$IMAGE" "$DISK"
        echo "[DONE] Restore completed."
        ;;

    list)
        echo "[*] Available block devices:"
        lsblk -o NAME,SIZE,TYPE,MOUNTPOINT
        ;;

    verify)
        if [ -z "$IMAGE" ]; then
            echo "Usage: $0 verify --image <image.qcow2>"
            exit 1
        fi

        if [ ! -f "$IMAGE" ]; then
            echo "[ERROR] Image file $IMAGE does not exist."
            exit 1
        fi

        echo "[*] Verifying $IMAGE ..."
        qemu-img check "$IMAGE"
        ;;

    compress-only)
        if [ -z "$DISK" ] || [ -z "$IMAGE" ]; then
            echo "Usage: $0 compress-only --disk <input.raw> --image <output.qcow2>"
            exit 1
        fi

        if [ ! -f "$DISK" ]; then
            echo "[ERROR] Input raw image $DISK does not exist."
            exit 1
        fi

        echo "[*] Compressing $DISK to $IMAGE ..."
        qemu-img convert -c -f raw -O qcow2 "$DISK" "$IMAGE"
        echo "[DONE] Compression completed."
        ;;

    *)
        echo "[ERROR] Unknown command: $COMMAND"
        echo
        echo "Usage:"
        echo "  $0 backup        --disk <device> --image <output.qcow2>"
        echo "  $0 restore       --disk <device> --image <input.qcow2> [--force]"
        echo "  $0 list"
        echo "  $0 verify        --image <qcow2 file>"
        echo "  $0 compress-only --disk <raw file> --image <output.qcow2>"
        exit 1
        ;;
esac
