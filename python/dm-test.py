#!/usr/bin/env python3

import argparse
import json
import os

from command_runner import CommandRunner
from disk_manager import DiskManager, get_partition_name, list_available_disks


def main():
    parser = argparse.ArgumentParser(description="Disk Partition Manager")
    parser.add_argument("commands", nargs="+", help="Actions to perform: wipe, create, format, mount, unmount, delete")
    parser.add_argument("--disk", help="Target disk (e.g., /dev/sdb or nvme0)")
    parser.add_argument("--tool", choices=["parted", "fdisk"], default="parted")
    parser.add_argument("--table", choices=["gpt", "msdos"], default="gpt", help="Partition table type")
    parser.add_argument("--partitions", help="JSON list of partitions with start/end/type")
    parser.add_argument("--fstype", choices=["ext4", "xfs"], default="ext4", help="Filesystem type")
    parser.add_argument("--blocksize", type=int, default=4096, help="Filesystem block size")
    parser.add_argument("--wipe", action="store_true", help="Wipe the disk before creating partition table")
    parser.add_argument("--force", action="store_true", help="Force operations even if partitions are mounted")

    args = parser.parse_args()

    if not args.disk:
        print("Available disks:")
        for d in list_available_disks():
            print(f"  {d}")
        return

    try:
        hostRoot = CommandRunner()
        manager = DiskManager(hostRoot, args.disk, args.tool, force=args.force)

        partitions = json.loads(args.partitions) if args.partitions else []

        actions = {
            "wipe": lambda: manager.delete_all_partitions(force=args.force),
            "create": lambda: (
                manager.create_partition_table(args.table, force=args.force) if args.table else None,
                manager.create_partitions(partitions, force=args.force) if partitions else None,
            ),
            "format": lambda: (
                [manager.format_partition(get_partition_name(manager.disk, idx), args.fstype, args.blocksize, force=args.force) for idx, _ in enumerate(partitions)]
                if args.fstype
                else ValueError("Filesystem type must be specified for format")
            ),
            "delete": lambda: (
                partition_index := int(input("Enter the partition index to delete (e.g., 1 for the first partition): ")) - 1,
                part_name := get_partition_name(manager.disk, partition_index),
                print(f"[ACTION] Deleting partition {part_name}..."),
                manager.delete_partition(part_name, force=args.force),
            ),
            "mount": lambda: [manager.mount_partition(get_partition_name(manager.disk, idx), f"/mnt/partition{idx+1}") for idx, _ in enumerate(partitions)],
            "unmount": lambda: [manager.unmount_partition(get_partition_name(manager.disk, idx)) for idx, _ in enumerate(partitions)],
        }

        for cmd in args.commands or []:
            if cmd == "wipe" or args.wipe:
                print(f"[ACTION] Wiping {args.disk}...")
                manager.delete_all_partitions(force=args.force)

            elif cmd == "create":
                if args.table:
                    print(f"[ACTION] Creating partition table {args.table}...")
                    manager.create_partition_table(args.table, force=args.force)
                if partitions:
                    print(f"[ACTION] Creating partitions...")
                    manager.create_partitions(partitions, force=args.force)

            elif cmd == "format":
                if not args.fstype:
                    raise ValueError("Filesystem type must be specified for format")
                info = manager.get_partition_info(refresh=True)
                for part_name in info.keys():
                    if part_name != os.path.basename(manager.disk):
                        print(f"[ACTION] Formatting {part_name} with {args.fstype}...")
                        manager.format_partition(part_name, args.fstype, args.blocksize, force=args.force)

                # for idx, _ in enumerate(partitions):
                #     part_name = get_partition_name(manager.disk, idx)
                #     print(f"[ACTION] Formatting {part_name} with {args.fstype}...")
                #     manager.format_partition(part_name, args.fstype, args.blocksize, force=args.force)

            elif cmd == "delete":
                partition_index = int(input("Enter the partition index to delete (e.g., 1 for the first partition): ")) - 1
                part_name = get_partition_name(manager.disk, partition_index)
                print(f"[ACTION] Deleting partition {part_name}...")
                manager.delete_partition(part_name, force=args.force)

            elif cmd == "mount":
                for idx, _ in enumerate(partitions):
                    part_name = get_partition_name(manager.disk, idx)
                    mount_point = f"/mnt/partition{idx+1}"
                    print(f"[ACTION] Mounting {part_name} to {mount_point}...")
                    manager.mount_partition(part_name, mount_point)

            elif cmd == "unmount":
                for idx, _ in enumerate(partitions):
                    part_name = get_partition_name(manager.disk, idx)
                    print(f"[ACTION] Unmounting {part_name}...")
                    manager.unmount_partition(part_name)

        print("\n[INFO] Final partition state:")
        print(json.dumps(manager.get_partition_info(refresh=True), indent=2))

    except Exception as e:
        print(f"[ERROR] {e}")


if __name__ == "__main__":
    main()
