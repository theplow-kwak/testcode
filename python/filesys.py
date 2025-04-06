#!/usr/bin/python3

import subprocess
import argparse
import os
import shlex
import sys


class CommandRunner:
    @staticmethod
    def execute(cmd: str, background=False, ignore_errors=False, expect_output=None, return_output=True):
        """
        Execute a shell command with advanced options.
        :param command: Command to execute.
        :param background: Run the command in the background.
        :param ignore_errors: Ignore errors during execution.
        :param expect_output: Expected output to validate against.
        :param return_stdout: Whether to return stdout.
        """
        if os.geteuid() != 0:
            cmd = f"sudo {cmd}"
        print(f">>> Executing command: {cmd}")
        args = shlex.split(cmd)
        try:
            if background:
                subprocess.Popen(args)
                return None

            result = subprocess.run(args, capture_output=return_output, text=True, check=not ignore_errors)
            if return_output:
                output = result.stdout.strip()
                if expect_output and expect_output not in output:
                    raise ValueError(f"Expected output '{expect_output}' not found in command output")
                return output
        except subprocess.CalledProcessError as e:
            if not ignore_errors:
                raise RuntimeError(f"Command failed: {e.cmd}\nReturn code: {e.returncode}\nOutput: {e.output}\nError: {e.stderr}")
        return None


# Update DiskManager and PartitionManager to use CommandRunner.execute
class DiskManager:
    def __init__(self, disk):
        self.disk = disk
        self._validate_disk()

    def _validate_disk(self):
        """Ensure the specified disk is not the system disk."""
        # Use the 'lsblk' command to list system disks
        system_disks = CommandRunner.execute("lsblk -o NAME,TYPE,MOUNTPOINT -n", return_output=True)
        sanitized_disks = [f"/dev/{line.split()[0].lstrip('├─└─')}" for line in system_disks.splitlines() if line.endswith("/")]  # Remove special characters like '├─' or '└─'
        if self.disk in sanitized_disks:
            raise ValueError(f"Error: The specified disk '{self.disk}' is a system disk and cannot be modified.")

    def create_partition(self, partition_type="gpt", num_partitions=1):
        """Create partitions on the specified disk."""
        print(f"Creating {partition_type} partition table on {self.disk}...")
        CommandRunner.execute(f"parted -s {self.disk} mklabel {partition_type}")

        for i in range(num_partitions):
            start = f"{i * 10}%"  # Example: Start at 0%, 10%, etc.
            end = f"{(i + 1) * 10}%"
            print(f"Creating partition {i + 1} from {start} to {end}...")
            CommandRunner.execute(f"parted -s {self.disk} mkpart primary {start} {end}")

    def create_partition_fdisk(self, partition_type="gpt", num_partitions=1):
        """Create partitions on the specified disk using fdisk."""
        print(f"Creating {partition_type} partition table on {self.disk} using fdisk...")
        CommandRunner.execute(f"echo -e 'o\nw' | fdisk {self.disk}")  # Create a new partition table

        for i in range(num_partitions):
            print(f"Creating partition {i + 1} using fdisk...")
            start = f"{i * 10}%"  # Example: Start at 0%, 10%, etc.
            end = f"{(i + 1) * 10}%"
            CommandRunner.execute(
                f"echo -e 'n\np\n\n{start}\n{end}\nw' | fdisk {self.disk}"
            )  # Create a primary partition with start and end

    def delete_partition(self, partition_name=None):
        """
        Delete partitions on the specified disk.
        :param partition_name: Specify the partition name to delete (e.g., /dev/sdb1).
                               If None, all partitions will be deleted.
        """
        if partition_name is None:
            print(f"Deleting all partitions on {self.disk}...")
            partitions = self.get_partitions()
            for partition in partitions:
                partition_name = partition["name"]
                partition_number = partition_name.replace(self.disk, "").lstrip("p")  # Extract partition number
                print(f"Deleting partition {partition_name}...")
                CommandRunner.execute(f"parted -s {self.disk} rm {partition_number}")
        else:
            print(f"Deleting partition {partition_name}...")
            partition_number = partition_name.replace(self.disk, "").lstrip("p")  # Extract partition number
            CommandRunner.execute(f"parted -s {self.disk} rm {partition_number}")

    def delete_partition_fdisk(self, partition_name=None):
        """
        Delete partitions on the specified disk using fdisk.
        :param partition_name: Specify the partition name to delete (e.g., /dev/nvme0n2p1).
                               If None, all partitions will be deleted.
        """
        if partition_name is None:
            print(f"Deleting all partitions on {self.disk} using fdisk...")
            partitions = self.get_partitions()
            for partition in partitions:
                partition_number = partition["name"].replace(self.disk, "").lstrip("p")  # Extract partition number
                print(f"Deleting partition {partition['name']}...")
                CommandRunner.execute(f"echo -e 'd\n{partition_number}\nw' | fdisk {self.disk}")
        else:
            print(f"Deleting partition {partition_name} using fdisk...")
            partition_number = partition_name.replace(self.disk, "").lstrip("p")  # Extract partition number
            CommandRunner.execute(f"echo -e 'd\n{partition_number}\nw' | fdisk {self.disk}")

    def get_partitions(self):
        """Retrieve partition information for the specified disk."""
        print(f"Retrieving partition information for {self.disk}...")
        partition_info = CommandRunner.execute(f"lsblk -o NAME,SIZE,TYPE,MOUNTPOINT -n {self.disk}", return_output=True)
        partitions = []
        for line in partition_info.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[2] == "part":  # Ensure it's a partition
                sanitized_name = f"/dev/{parts[0].lstrip('├─└─')}"  # Remove special characters like '├─' or '└─'
                partition = {
                    "name": sanitized_name,
                    "size": parts[1],
                    "mountpoint": parts[3] if len(parts) > 3 else None
                }
                partitions.append(partition)
        return partitions


class PartitionManager:
    def __init__(self, partition):
        self.partition = partition

    def format_partition(self, filesystem="ext4", block_size="4096", force=False):
        """Format a partition with the specified filesystem and block size."""
        print(f"Formatting {self.partition} with {filesystem} and block size {block_size}...")
        force_option = "-F" if filesystem == "ext4" and force else "-f" if filesystem == "xfs" and force else ""
        if filesystem == "ext4":
            CommandRunner.execute(f"mkfs.ext4 {force_option} -b {block_size} {self.partition}")
        elif filesystem == "xfs":
            CommandRunner.execute(f"mkfs.xfs {force_option} -b size={block_size} {self.partition}")
        else:
            print("Unsupported filesystem. Use 'ext4' or 'xfs'.")

    def mount_partition(self, mount_point):
        """Mount a partition to the specified mount point."""
        print(f"Mounting {self.partition} to {mount_point}...")
        CommandRunner.execute(f"mkdir -p {mount_point}")
        CommandRunner.execute(f"mount {self.partition} {mount_point}")

    def unmount_partition(self, mount_point):
        """Unmount a partition from the specified mount point."""
        print(f"Unmounting {mount_point}...")
        CommandRunner.execute(f"umount {mount_point}")


def main():
    parser = argparse.ArgumentParser(description="Disk and Partition Management Tool")
    parser.add_argument("-d", "--disk", required=True, help="Specify the disk (e.g., /dev/sdb)")
    parser.add_argument("-m", "--mount-point", required=True, help="Specify the mount point (e.g., /mnt/mydisk)")
    parser.add_argument("--use-fdisk", action="store_true", help="Use fdisk instead of parted for partitioning")

    args = parser.parse_args()

    partition_types = ["gpt", "mbr"]
    filesystems = ["ext4", "xfs"]
    block_sizes = ["1024", "2048", "4096", "8192"]

    for partition_type in partition_types:
        for filesystem in filesystems:
            for block_size in block_sizes:
                # Validate block size for ext4
                if filesystem == "ext4" and block_size not in ["1024", "2048", "4096"]:
                    print(f"Skipping invalid block size {block_size} for filesystem {filesystem}")
                    continue

                print(f"\nTesting with Partition Type: {partition_type}, Filesystem: {filesystem}, Block Size: {block_size}")
                
                # Disk operations
                disk_manager = DiskManager(args.disk)
                if args.use_fdisk:
                    disk_manager.create_partition_fdisk(partition_type=partition_type, num_partitions=2)
                else:
                    disk_manager.create_partition(partition_type=partition_type, num_partitions=2)

                partitions = disk_manager.get_partitions()
                print("Partitions created:")
                print(partitions)

                # Partition operations
                for index, partition in enumerate(partitions, start=1):
                    mount_path = os.path.join(args.mount_point, f"partition{index}")  # Extend mount path for each partition
                    print(f"Partition: {partition['name']}, Size: {partition['size']}, Mountpoint: {partition['mountpoint']}")
                    partition_manager = PartitionManager(partition["name"])
                    partition_manager.format_partition(filesystem=filesystem, block_size=block_size, force=True)
                    partition_manager.mount_partition(mount_path)
                    out = CommandRunner.execute(f"ls -la {mount_path}")
                    print(out)

                # Unmount and delete partitions
                for index, partition in enumerate(partitions, start=1):
                    mount_path = os.path.join(args.mount_point, f"partition{index}")  # Extend mount path for each partition
                    print(f"Unmounting partition: {partition['name']} from {mount_path}")
                    partition_manager.unmount_partition(mount_path)

                if args.use_fdisk:
                    disk_manager.delete_partition_fdisk()
                else:
                    disk_manager.delete_partition()


if __name__ == "__main__":
    main()
