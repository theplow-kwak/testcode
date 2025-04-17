#!/usr/bin/python3

import argparse
import json
import os

from command_runner import CommandRunner
from disk_manager import DiskManager, get_partition_name


def main():
    parser = argparse.ArgumentParser(description="Disk Partition Manager")
    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    common = {
        "disk": {"help": "Target disk device (e.g., /dev/sdb)"},
        "tool": {"choices": ["parted", "fdisk"], "default": "parted", "help": "Partitioning tool"},
    }

    p_create = subparsers.add_parser("create", help="Create partition table and partitions")
    p_create.add_argument("--disk", required=True, **common["disk"])
    p_create.add_argument("--tool", **common["tool"])
    p_create.add_argument("--table", choices=["gpt", "msdos"], required=True, help="Partition table type")
    p_create.add_argument("--partitions", type=str, required=True, help="Partition definitions as JSON string")
    p_create.add_argument("--format", action="store_true", help="Format partitions after creation")
    p_create.add_argument("--fstype", choices=["ext4", "xfs"], help="Filesystem type")
    p_create.add_argument("--blocksize", type=int, help="Block size for filesystem")
    p_create.add_argument("--force", action="store_true", help="Force operation")

    p_mount = subparsers.add_parser("mount", help="Mount partitions")
    p_mount.add_argument("--disk", required=True, **common["disk"])
    p_mount.add_argument("--force", action="store_true", help="Force operation")

    p_unmount = subparsers.add_parser("unmount", help="Unmount partitions")
    p_unmount.add_argument("--disk", required=True, **common["disk"])
    p_unmount.add_argument("--force", action="store_true", help="Force operation")

    p_wipe = subparsers.add_parser("wipe", help="Wipe all partitions")
    p_wipe.add_argument("--disk", required=True, **common["disk"])
    p_wipe.add_argument("--force", action="store_true", help="Force operation")

    p_format = subparsers.add_parser("format", help="Format a specific partition")
    p_format.add_argument("--disk", required=True, **common["disk"])
    p_format.add_argument("--partition", type=int, default=1, help="Partition index to format (e.g., 1 for the first partition)")
    p_format.add_argument("--fstype", choices=["ext4", "xfs"], default="ext4", help="Filesystem type")
    p_format.add_argument("--blocksize", type=int, default=4096, help="Block size for filesystem")
    p_format.add_argument("--force", action="store_true", help="Force operation")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return

    try:
        hostRoot = CommandRunner()
        manager = DiskManager(hostRoot, args.disk, getattr(args, "tool", "parted"))

        if args.command == "create":
            manager.create_partition_table(args.table, force=args.force)
            partitions = json.loads(args.partitions)
            manager.create_partitions(partitions, force=args.force)

            if args.format and args.fstype:
                for idx, _ in enumerate(partitions):
                    part = get_partition_name(args.disk, idx)
                    manager.format_partition(part, args.fstype, args.blocksize, force=args.force)

        elif args.command == "format":
            part = get_partition_name(args.disk, args.partition - 1)
            manager.format_partition(part, args.fstype, args.blocksize, force=args.force)

        elif args.command in {"mount", "unmount"}:
            info = manager.get_partition_info(refresh=True)
            for idx, name in enumerate(info.keys()):
                if name != os.path.basename(args.disk):
                    if args.command == "mount":
                        manager.mount_partition(f"/dev/{name}", f"/mnt/partition{idx}")
                    else:
                        manager.unmount_partition(f"/dev/{name}")

        elif args.command == "wipe":
            manager.delete_all_partitions(force=args.force)

    except Exception as e:
        print(f"[ERROR] {e}")


if __name__ == "__main__":
    main()
