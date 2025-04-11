import subprocess
import argparse
import os
import json
import logging
from typing import List, Dict, Optional

# Configure logging
logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s")
logger = logging.getLogger(__name__)


class CommandRunner:
    def __init__(self, ignore_error: bool = False, return_stdout: bool = True, background: bool = False):
        self.ignore_error = ignore_error
        self.return_stdout = return_stdout
        self.background = background

    def run_command(self, cmd: str) -> Optional[str]:
        try:
            logger.debug(f"Running command: {cmd}")
            if self.background:
                subprocess.Popen(cmd, shell=True)
                return None
            result = subprocess.run(cmd, shell=True, text=True, stdout=subprocess.PIPE if self.return_stdout else None, stderr=subprocess.PIPE)
            if result.returncode != 0 and not self.ignore_error:
                logger.error(f"Command failed: {cmd}\nError: {result.stderr}")
                raise RuntimeError(f"Command failed: {cmd}\nError: {result.stderr}")
            return result.stdout if self.return_stdout else None
        except Exception as e:
            logger.exception(f"Exception while running command: {cmd}")
            if not self.ignore_error:
                raise e
            return None


def get_partition_name(disk: str, idx: int) -> str:
    basename = os.path.basename(disk)
    if basename.startswith("nvme") or basename.startswith("loop") or basename[-1].isdigit():
        return f"{disk}p{idx + 1}"
    return f"{disk}{idx + 1}"


class DiskManager:
    def __init__(self, disk: str, tool: str = "parted"):
        self.disk = disk
        self.tool = tool
        self.cmd_runner = CommandRunner()
        self.partition_info = {}

        self.check_protected_disk()
        self.refresh_partition_info()

    def refresh_partition_info(self):
        try:
            logger.info(f"Refreshing partition info for {self.disk}")
            output = self.cmd_runner.run_command(f"lsblk -P -e7 -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT {self.disk}")
            devices = {}
            if output:
                for line in output.strip().splitlines():
                    entry = dict(item.split("=", 1) for item in line.split() if "=" in item)
                    name = entry.get("NAME", "").strip('"')
                    if name:
                        devices[name] = {k: v.strip('"') for k, v in entry.items()}
            self.partition_info = devices
        except Exception as e:
            raise RuntimeError(f"Failed to retrieve partition info: {e}")

    def get_partition_info(self, refresh: bool = False):
        if refresh:
            self.refresh_partition_info()
        return self.partition_info

    def check_protected_disk(self):
        try:
            mounts = self.cmd_runner.run_command("lsblk -P -e7 -o NAME,MOUNTPOINT")
            if mounts:
                for line in mounts.strip().splitlines():
                    entry = dict(item.split("=", 1) for item in line.split() if "=" in item)
                    name = entry.get("NAME", "").strip('"')
                    mountpoint = entry.get("MOUNTPOINT", "").strip('"')
                    if name == os.path.basename(self.disk) and mountpoint:
                        raise RuntimeError(f"{self.disk} is currently in use or may have OS installed.")
        except Exception as e:
            raise RuntimeError(f"Failed to check protected disk: {e}")

    def check_and_warn_mounted_partitions(self):
        try:
            output = self.cmd_runner.run_command("lsblk -P -e7 -o NAME,MOUNTPOINT")
            if output:
                for line in output.strip().splitlines():
                    entry = dict(item.split("=", 1) for item in line.split() if "=" in item)
                    name = entry.get("NAME", "").strip('"')
                    mountpoint = entry.get("MOUNTPOINT", "").strip('"')
                    if name.startswith(os.path.basename(self.disk)) and mountpoint:
                        raise RuntimeError(f"Partition /dev/{name} is mounted at {mountpoint}. Unmount before modifying partitions.")
        except Exception as e:
            raise RuntimeError(f"Failed to check mounted partitions: {e}")

    def create_partition_table(self, table_type: str):
        try:
            logger.info(f"Creating partition table {table_type} on {self.disk}")
            self.check_and_warn_mounted_partitions()
            self.cmd_runner.run_command(f"parted -s {self.disk} mklabel {table_type}")
            self.refresh_partition_info()
        except Exception as e:
            raise RuntimeError(f"Failed to create partition table: {e}")

    def create_partitions_with_parted(self, partitions: List[Dict]):
        try:
            for part in partitions:
                part_type = part.get("type", "primary")
                start = part["start"]
                end = part["end"]
                logger.info(f"Creating partition {part_type} from {start} to {end} using parted")
                self.cmd_runner.run_command(f"parted -s {self.disk} mkpart {part_type} {start} {end}")
            self.refresh_partition_info()
        except Exception as e:
            raise RuntimeError(f"Failed to create partitions with parted: {e}")

    def create_partitions_with_fdisk(self, partitions: List[Dict]):
        try:
            script = ""
            for part in partitions:
                script += "n\n\n"
                script += f"{part['start']}\n"
                script += f"{part['end']}\n"
            script += "w\n"
            with open("/tmp/fdisk_script.txt", "w") as f:
                f.write(script)
            logger.info("Running fdisk script")
            self.cmd_runner.run_command(f"fdisk {self.disk} < /tmp/fdisk_script.txt")
            self.refresh_partition_info()
        except Exception as e:
            raise RuntimeError(f"Failed to create partitions with fdisk: {e}")

    def create_partitions(self, partitions: List[Dict]):
        try:
            self.check_and_warn_mounted_partitions()
            if self.tool == "parted":
                self.create_partitions_with_parted(partitions)
            elif self.tool == "fdisk":
                self.create_partitions_with_fdisk(partitions)
        except Exception as e:
            raise RuntimeError(f"Failed to create partitions: {e}")

    def delete_all_partitions(self):
        try:
            logger.info(f"Wiping all partitions on {self.disk}")
            self.check_and_warn_mounted_partitions()
            self.cmd_runner.run_command(f"wipefs -a {self.disk}")
            self.refresh_partition_info()
        except Exception as e:
            raise RuntimeError(f"Failed to delete all partitions: {e}")

    def format_partition(self, partition: str, fstype: str, block_size: Optional[int] = None):
        try:
            logger.info(f"Formatting {partition} as {fstype} with block size {block_size}")
            if fstype == "ext4":
                cmd = f"mkfs.ext4 {'-b ' + str(block_size) if block_size else ''} {partition}"
            elif fstype == "xfs":
                cmd = f"mkfs.xfs {'-b size=' + str(block_size) if block_size else ''} {partition}"
            else:
                raise ValueError("Unsupported filesystem type")
            self.cmd_runner.run_command(cmd)
        except Exception as e:
            raise RuntimeError(f"Failed to format partition {partition}: {e}")

    def mount_partition(self, partition: str, mount_point: str):
        try:
            os.makedirs(mount_point, exist_ok=True)
            output = self.cmd_runner.run_command("lsblk -P -e7 -o NAME,MOUNTPOINT")
            already_mounted = False
            if output:
                for line in output.strip().splitlines():
                    entry = dict(item.split("=", 1) for item in line.split() if "=" in item)
                    name = entry.get("NAME", "").strip('"')
                    mountpoint = entry.get("MOUNTPOINT", "").strip('"')
                    if f"/dev/{name}" == partition and mountpoint:
                        logger.warning(f"{partition} is already mounted at {mountpoint}. Skipping mount.")
                        already_mounted = True
                        break
            if not already_mounted:
                self.cmd_runner.run_command(f"mount {partition} {mount_point}")
        except Exception as e:
            raise RuntimeError(f"Failed to mount partition {partition} on {mount_point}: {e}")

    def unmount_partition(self, partition: str):
        try:
            logger.info(f"Unmounting {partition}")
            self.cmd_runner.run_command(f"umount {partition}")
        except Exception as e:
            raise RuntimeError(f"Failed to unmount partition {partition}: {e}")


# Refactored entry point with subcommands


def main():
    parser = argparse.ArgumentParser(description="Disk Partition and Format Tool")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # Partitioning command
    pparser = subparsers.add_parser("partition", help="Create or modify partitions")
    pparser.add_argument("--disk", required=True, help="Target disk device (e.g., /dev/sdb)")
    pparser.add_argument("--tool", choices=["parted", "fdisk"], default="parted", help="Partitioning tool")
    pparser.add_argument("--table", choices=["gpt", "msdos"], help="Partition table type")
    pparser.add_argument("--partitions", type=str, help="Partition definitions as JSON string")
    pparser.add_argument("--wipe", action="store_true", help="Wipe existing partitions before proceeding")
    pparser.add_argument("--format", action="store_true", help="Format partitions after creation")
    pparser.add_argument("--fstype", choices=["ext4", "xfs"], help="Filesystem type")
    pparser.add_argument("--blocksize", type=int, help="Block size for filesystem")
    pparser.add_argument("--mount", action="store_true", help="Mount the partitions")

    # Mount/Unmount command
    mparser = subparsers.add_parser("mount", help="Mount or unmount existing partitions")
    mparser.add_argument("--disk", required=True, help="Target disk device (e.g., /dev/sdb)")
    mparser.add_argument("--tool", choices=["parted", "fdisk"], default="parted", help="Partitioning tool")
    mparser.add_argument("--mount", action="store_true", help="Mount partitions")
    mparser.add_argument("--unmount", action="store_true", help="Unmount partitions")

    args = parser.parse_args()

    try:
        manager = DiskManager(args.disk, args.tool)

        if args.command == "partition":
            if args.wipe:
                manager.delete_all_partitions()

            if args.table:
                manager.create_partition_table(args.table)

            partitions = []
            if args.partitions:
                partitions = json.loads(args.partitions)
                manager.create_partitions(partitions)

                if args.format:
                    for idx, _ in enumerate(partitions):
                        partition_name = get_partition_name(args.disk, idx)
                        manager.format_partition(partition_name, args.fstype, args.blocksize)

            if args.mount:
                for idx, _ in enumerate(partitions):
                    mount_point = f"/mnt/partition{idx + 1}"
                    partition_name = get_partition_name(args.disk, idx)
                    manager.mount_partition(partition_name, mount_point)

        elif args.command == "mount":
            partitions_info = manager.get_partition_info(refresh=True)
            for idx, name in enumerate(partitions_info.keys()):
                if name != os.path.basename(args.disk):
                    partition_name = f"/dev/{name}"
                    if args.mount:
                        mount_point = f"/mnt/partition{idx}"
                        manager.mount_partition(partition_name, mount_point)
                    if args.unmount:
                        manager.unmount_partition(partition_name)

        logger.info("Final Partition Info:")
        logger.info(json.dumps(manager.get_partition_info(), indent=2))

    except Exception as e:
        logger.error(f"Error: {e}")


if __name__ == "__main__":
    main()
