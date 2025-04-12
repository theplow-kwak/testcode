#!/usr/bin/python3

import subprocess
import argparse
import os
import json
import logging
import stat
from typing import List, Dict, Optional

logging.basicConfig(level=logging.INFO, format="[%(levelname)s] %(message)s")


class CommandRunner:
    def __init__(self, ignore_error: bool = False, return_stdout: bool = True, background: bool = False):
        self.ignore_error = ignore_error
        self.return_stdout = return_stdout
        self.background = background
        self.sudo_cmd = "sudo " if os.geteuid() != 0 else ""

    def run_command(self, cmd: str, ignore_error=None) -> Optional[str]:
        """Run a shell command and handle errors."""
        cmd = f"{self.sudo_cmd}{cmd}"
        ignore_error = ignore_error or self.ignore_error
        try:
            logging.debug(f"Executing command: {cmd}")
            if self.background:
                subprocess.Popen(cmd, shell=True)
                return None
            result = subprocess.run(cmd, shell=True, text=True, stdout=subprocess.PIPE if self.return_stdout else None, stderr=subprocess.PIPE)
            if result.returncode != 0 and not ignore_error:
                raise RuntimeError(f"Command failed: {cmd}\nError: {result.stderr}")
            return result.stdout.strip() if self.return_stdout else None
        except Exception as e:
            if not ignore_error:
                raise RuntimeError(f"Error executing command: {cmd}\n{e}")
            return None

    @staticmethod
    def command_exists(cmd: str) -> bool:
        """Check if a command exists on the system."""
        return subprocess.call(f"type {cmd}", shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE) == 0


def resolve_disk_path(disk: str) -> str:
    """
    Resolve the full path of a disk and ensure it is a valid block device.
    If the input is a partition, return the corresponding base disk name.
    """

    def is_block_device(path: str) -> bool:
        """Check if the given path is a block device."""
        return os.path.exists(path) and stat.S_ISBLK(os.stat(path).st_mode)

    def get_base_disk(path: str) -> str:
        """Extract the base disk name from a partition."""
        basename = os.path.basename(path)
        return path.rsplit("p", 1)[0] if "p" in basename and basename.startswith("nvme") else path.rstrip("0123456789")

    # Resolve and validate the disk path
    if not disk.startswith("/dev/"):
        disk = f"/dev/{disk}"
    if not is_block_device(disk):
        raise ValueError(f"'{disk}' is not a valid block device.")

    base_disk = get_base_disk(disk)
    return base_disk if base_disk != disk and is_block_device(base_disk) else disk


def list_available_disks() -> List[str]:
    """List all available disks that are not mounted."""
    runner = CommandRunner()
    output = runner.run_command("lsblk -dn -o NAME,TYPE,MOUNTPOINT")
    return [f"/dev/{name}" for line in (output or "").strip().splitlines() for name, dtype, mountpoint in [line.split()] if dtype == "disk" and not mountpoint]


def get_partition_name(disk: str, idx: int) -> str:
    """Generate a partition name based on the disk and index."""
    basename = os.path.basename(disk)
    suffix = f"p{idx + 1}" if basename[-1].isdigit() or basename.startswith(("nvme", "loop")) else f"{idx + 1}"
    return f"{disk}{suffix}"


class DiskManager:
    def __init__(self, disk: str, tool: str = "parted", force: bool = False):
        self.disk = resolve_disk_path(disk)
        self.tool = tool  # 'parted' or 'fdisk'
        self.force = force  # Allow forced operations
        self.cmd_runner = CommandRunner()
        self.partition_info = {}
        self.mount_info = []  # Cache for lsblk mount info
        self.refresh_lsblk_info(refresh_partition=True, refresh_mount=True)
        self.is_bootdevice = self.check_protected_disk()

    def _refresh_lsblk_info(self, fields: str) -> List[Dict[str, str]]:
        """
        Refresh and parse `lsblk` output for the specified fields.
        :param fields: Comma-separated fields to fetch from `lsblk`.
        :return: A list of dictionaries with the parsed `lsblk` output.
        """
        cmd = f"lsblk -P -e7 -o {fields} {self.disk}"
        output = self.cmd_runner.run_command(cmd)
        if not output:
            return []
        return [{k: v.strip('"') for k, v in (item.split("=", 1) for item in line.split() if "=" in item)} for line in output.strip().splitlines()]

    def refresh_lsblk_info(self, refresh_partition: bool = False, refresh_mount: bool = False):
        """
        Refresh partition and/or mount information for the disk.
        :param refresh_partition: If True, refresh partition information.
        :param refresh_mount: If True, refresh mount information.
        """
        if refresh_partition:
            self.partition_info = {entry["NAME"]: entry for entry in self._refresh_lsblk_info("NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT")}
            logging.debug(f"Partition info: {json.dumps(self.partition_info, indent=2)}")

        if refresh_mount:
            self.mount_info = self._refresh_lsblk_info("NAME,MOUNTPOINT")
            logging.debug(f"Mount info: {self.mount_info}")

    def get_mount_info(self, refresh: bool = False) -> List[Dict[str, str]]:
        """Get cached mount information, optionally refreshing it."""
        if refresh or not self.mount_info:
            self.refresh_lsblk_info(refresh_mount=True)
        return self.mount_info

    def check_protected_disk(self) -> bool:
        """
        Check if the disk is protected (e.g., mounted, contains the root filesystem, or is a boot disk).
        """
        mount_info = self.get_mount_info(refresh=True)
        if any(entry.get("MOUNTPOINT", "") in ["/", "/boot", "/boot/efi"] for entry in mount_info):
            logging.warning(f"{self.disk} contains the root filesystem and cannot be modified.")
            return True

        boot_device = self.cmd_runner.run_command("lsblk -no NAME,PARTLABEL | grep -i boot", ignore_error=True)
        if boot_device and os.path.basename(self.disk) in boot_device:
            logging.warning(f"{self.disk} is a boot disk and cannot be modified.")
            return True
        return False

    def check_and_warn_mounted_partitions(self, force: bool = False) -> bool:
        """
        Check for mounted partitions and return True if any are mounted.
        :param force: If True, ignore warnings and proceed despite mounted partitions.
        :return: True if mounted partitions are found, False otherwise.
        """
        mount_info = self.get_mount_info()
        mounted_partitions = [
            f"/dev/{entry['NAME']} is mounted at {entry['MOUNTPOINT']}" for entry in mount_info if entry["NAME"].startswith(os.path.basename(self.disk)) and entry.get("MOUNTPOINT")
        ]
        if mounted_partitions:
            for warning in mounted_partitions:
                logging.warning(warning)
        return bool(mounted_partitions)

    def create_partition_table(self, table_type: str, force: bool = False):
        """Create a partition table on the disk."""
        if self.check_and_warn_mounted_partitions(force=force):
            logging.error(f"Cannot create partition table on {self.disk} because some partitions are mounted.")
            return
        try:
            self.cmd_runner.run_command(f"parted -s {self.disk} mklabel {table_type}")
            self.refresh_lsblk_info(refresh_partition=True, refresh_mount=True)
            logging.info(f"Created {table_type} partition table on {self.disk}")
        except Exception as e:
            logging.error(f"Failed to create partition table: {e}")

    def create_partitions(self, partitions: List[Dict], force: bool = False):
        """
        Create partitions on the disk.
        :param partitions: A list of dictionaries defining partitions with keys:
                           - "start": Start of the partition (e.g., "1MiB").
                           - "end": End of the partition (e.g., "500MiB").
                           - "type": (Optional) Partition type (e.g., "primary").
        :param force: If True, ignore warnings and proceed despite mounted partitions.
        """
        if not partitions:
            raise ValueError("No partitions defined for creation.")

        # Validate partition definitions
        for idx, part in enumerate(partitions):
            if "start" not in part or "end" not in part:
                raise ValueError(f"Partition {idx + 1} is missing required keys ('start' and 'end').")

        if self.check_and_warn_mounted_partitions(force=force):
            logging.error(f"Cannot create partitions on {self.disk} because some partitions are mounted.")
            return

        try:
            if self.tool == "parted":
                for idx, part in enumerate(partitions):
                    part_type = part.get("type", "primary")
                    start = part["start"]
                    end = part["end"]
                    logging.info(f"Creating partition {idx + 1}: type={part_type}, start={start}, end={end}")
                    self.cmd_runner.run_command(f"parted -s {self.disk} mkpart {part_type} {start} {end}")
            elif self.tool == "fdisk":
                script = "\n".join(f"n\n\n{part['start']}\n{part['end']}" for part in partitions) + "\nw\n"
                logging.info(f"Creating partitions using fdisk script:\n{script}")
                self.cmd_runner.run_command(f"printf '{script}' | fdisk {self.disk}")
            self.refresh_lsblk_info(refresh_partition=True)
            logging.info(f"Successfully created partitions on {self.disk}")
        except Exception as e:
            logging.error(f"Failed to create partitions: {e}")

    def delete_all_partitions(self, force: bool = False):
        """Delete all partitions on the disk."""
        mount_info = self.get_mount_info(refresh=True)
        if self.check_and_warn_mounted_partitions(force=force):
            if force:
                logging.warning("Force mode enabled. Attempting to unmount all mounted partitions.")
                for entry in mount_info:
                    name = entry.get("NAME", "")
                    mountpoint = entry.get("MOUNTPOINT", "")
                    if name.startswith(os.path.basename(self.disk)) and mountpoint:
                        self.unmount_partition(mountpoint)
            else:
                logging.error(f"Cannot delete partitions on {self.disk} because some partitions are mounted.")
                return
        try:
            self.cmd_runner.run_command(f"wipefs -a {self.disk}")
            self.refresh_lsblk_info(refresh_partition=True, refresh_mount=True)
            logging.info(f"Deleted all partitions and wiped filesystem signatures on {self.disk}")
        except Exception as e:
            logging.error(f"Failed to delete partitions: {e}")

    def format_partition(self, partition: str, fstype: str, block_size: Optional[int] = None, force: bool = False):
        """Format a partition with the specified filesystem."""
        mount_info = self.get_mount_info(refresh=True)
        for entry in mount_info:
            name = entry.get("NAME", "")
            mountpoint = entry.get("MOUNTPOINT", "")
            if f"/dev/{name}" == partition and mountpoint:
                if force:
                    logging.warning(f"{partition} is mounted at {mountpoint}. Force mode enabled. Attempting to unmount.")
                    self.unmount_partition(partition)
                else:
                    logging.error(f"Cannot format {partition} because it is mounted at {mountpoint}.")
                    return
        cmd = {
            "ext4": f"mkfs.ext4 {'-b ' + str(block_size) if block_size else ''} {partition}",
            "xfs": f"mkfs.xfs {'-b size=' + str(block_size) if block_size else ''} {'-f' if force else ''} {partition}",
        }.get(fstype)
        if not cmd:
            raise ValueError("Unsupported filesystem type")
        self.cmd_runner.run_command(cmd)
        logging.info(f"Formatted {partition} with {fstype}")

    def mount_partition(self, partition: str, mount_point: str):
        """Mount a partition to a mount point."""
        os.makedirs(mount_point, exist_ok=True)
        mount_info = self.get_mount_info(refresh=True)
        for entry in mount_info:
            name = entry.get("NAME", "")
            mountpoint = entry.get("MOUNTPOINT", "")
            if f"/dev/{name}" == partition and mountpoint:
                logging.warning(f"{partition} is already mounted at {mountpoint}. Skipping mount.")
                return
        self.cmd_runner.run_command(f"mount {partition} {mount_point}")
        logging.info(f"Mounted {partition} to {mount_point}")

    def unmount_partition(self, partition: str):
        """Unmount a partition."""
        try:
            self.cmd_runner.run_command(f"umount {partition}")
            logging.info(f"Unmounted {partition}")
        except Exception as e:
            logging.error(f"Failed to unmount {partition}: {e}")

    def get_partition_info(self, refresh: bool = False) -> Dict[str, Dict[str, str]]:
        """
        Get partition information for the disk.
        :param refresh: If True, refresh the partition information before returning it.
        :return: A dictionary containing partition information.
        """
        if refresh:
            self.refresh_lsblk_info(refresh_partition=True)
        return self.partition_info


def main():
    parser = argparse.ArgumentParser(description="Disk Partition Manager")
    parser.add_argument("commands", nargs="+", help="Actions to perform: wipe, create, format, mount, unmount")
    parser.add_argument("--disk", help="Target disk (e.g., /dev/sdb or nvme0)")
    parser.add_argument("--tool", choices=["parted", "fdisk"], default="parted")
    parser.add_argument("--table", choices=["gpt", "msdos"], help="Partition table type")
    parser.add_argument("--partitions", help="JSON list of partitions with start/end/type")
    parser.add_argument("--fstype", choices=["ext4", "xfs"], help="Filesystem type")
    parser.add_argument("--blocksize", type=int, help="Filesystem block size")
    parser.add_argument("--wipe", action="store_true", help="Wipe the disk before creating partition table")
    parser.add_argument("--force", action="store_true", help="Force operations even if partitions are mounted")

    args = parser.parse_args()

    if not args.disk:
        print("Available disks:")
        for d in list_available_disks():
            print(f"  {d}")
        return

    try:
        manager = DiskManager(args.disk, args.tool, force=args.force)

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
                for idx, _ in enumerate(partitions):
                    part_name = get_partition_name(manager.disk, idx)
                    print(f"[ACTION] Formatting {part_name} with {args.fstype}...")
                    manager.format_partition(part_name, args.fstype, args.blocksize, force=args.force)

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
