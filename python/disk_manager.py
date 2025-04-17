import json
import logging
import os
import re
from typing import Dict, List, Optional

from command_runner import VM_LOG_MSG_TYPE_DEBUG, VM_LOG_MSG_TYPE_ERROR, VM_LOG_MSG_TYPE_WARN, VmCommon

logging.basicConfig(level=logging.INFO, format="[%(levelname)s] %(message)s")


def get_base_disk(path: str) -> str:
    """Extract the base disk name from a partition."""
    basename = os.path.basename(path)
    return path.rsplit("p", 1)[0] if basename.startswith("nvme") else re.sub(r"\d+$", "", basename)


def resolve_disk_path(hostRoot, disk: str) -> str:
    """
    Resolve the full path of a disk and ensure it is a valid block device.
    If the input is a partition, return the corresponding base disk name.
    """

    def is_block_device(path: str) -> bool:
        """Check if the given path is a block device using a bash command."""
        hostRoot.VmExec(f"test -b {path}")
        return hostRoot.VmExecStatus() == "0"

    # Resolve and validate the disk path
    if not disk.startswith("/dev/"):
        disk = f"/dev/{disk}"
    if not is_block_device(disk):
        raise ValueError(f"'{disk}' is not a valid block device.")

    base_disk = get_base_disk(disk)
    return base_disk if base_disk != disk and is_block_device(base_disk) else disk


def list_available_disks(hostRoot) -> List[str]:
    """List all available disks that are not mounted."""
    output = hostRoot.VmExec("lsblk -P -e7 -dn -o NAME,TYPE,MOUNTPOINT")
    parsed_output = [{k: v.strip('"') for k, v in (item.split("=", 1) for item in line.split() if "=" in item)} for line in output.strip().splitlines()]
    available_disks = [entry["NAME"] for entry in parsed_output if entry["TYPE"] == "disk" and not entry["MOUNTPOINT"]]
    return [f"/dev/{disk}" for disk in available_disks]


def get_partition_name(disk: str, idx: int) -> str:
    """Generate a partition name based on the disk and index."""
    basename = os.path.basename(disk)
    suffix = f"p{idx + 1}" if basename[-1].isdigit() or basename.startswith(("nvme", "loop")) else f"{idx + 1}"
    return f"{disk}{suffix}"


def convert_size_to_mb(size_str: str, total_mb: int) -> int:
    size_str = size_str.lower().strip()
    if size_str.endswith("%"):
        percent = float(size_str[:-1])
        return int((percent / 100.0) * total_mb)
    elif size_str.endswith("tib"):
        return int(float(size_str[:-3]) * 1024 * 1024)
    elif size_str.endswith("gib"):
        return int(float(size_str[:-3]) * 1024)
    elif size_str.endswith("mib"):
        return int(float(size_str[:-3]))
    elif size_str.endswith("gb"):
        return int(float(size_str[:-2]) * 1000)
    elif size_str.endswith("mb"):
        return int(float(size_str[:-2]))
    else:
        raise int(size_str)


class DiskManager:
    def __init__(self, hostRoot, disk: str, force: bool = False):
        self.hostRoot = hostRoot
        self.disk = resolve_disk_path(self.hostRoot, disk)
        self.force = force  # Allow forced operations
        self.partition_info = {}
        self.refresh_lsblk_info(refresh_partition=True)
        self.is_bootdevice = self.check_protected_disk()

    def _refresh_lsblk_info(self, fields: str) -> List[Dict[str, str]]:
        """
        Refresh and parse `lsblk` output for the specified fields.
        :param fields: Comma-separated fields to fetch from `lsblk`.
        :return: A list of dictionaries with the parsed `lsblk` output.
        """
        cmd = f"lsblk -P -e7 -o {fields} {self.disk}"
        output = self.hostRoot.VmExec(cmd)
        if not output:
            return []
        return [{k: v.strip('"') for k, v in (item.split("=", 1) for item in line.split() if "=" in item)} for line in output.strip().splitlines()]

    def refresh_lsblk_info(self, refresh_partition: bool = False):
        """
        Refresh partition and/or mount information for the disk.
        :param refresh_partition: If True, refresh partition information.
        :param refresh_mount: If True, refresh mount information.
        """
        if refresh_partition:
            self.partition_info = {entry["NAME"]: entry for entry in self._refresh_lsblk_info("NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT") if entry["TYPE"] == "part"}
            VmCommon.LogMsg(f"Partition info: {json.dumps(self.partition_info, indent=2)}", msgType=VM_LOG_MSG_TYPE_DEBUG)

    def check_protected_disk(self) -> bool:
        """
        Check if the disk is protected (e.g., mounted, contains the root filesystem, or is a boot disk).
        """
        if any(entry.get("MOUNTPOINT", "") in ["/", "/boot", "/boot/efi"] for _name, entry in self.partition_info.items()):
            VmCommon.LogMsg(f"{self.disk} contains the root filesystem and cannot be modified.", msgType=VM_LOG_MSG_TYPE_WARN)
            return True

        boot_device = self.hostRoot.VmExec("lsblk -no NAME,PARTLABEL | grep -i boot", ignoreError=True)
        if boot_device and os.path.basename(self.disk) in boot_device:
            VmCommon.LogMsg(f"{self.disk} is a boot disk and cannot be modified.", msgType=VM_LOG_MSG_TYPE_WARN)
            return True
        return False

    def check_and_warn_mounted_partitions(self, force: bool = False) -> bool:
        """
        Check for mounted partitions and return True if any are mounted.
        :param force: If True, ignore warnings and proceed despite mounted partitions.
        :return: True if mounted partitions are found, False otherwise.
        """
        mounted_partitions = [
            f"/dev/{entry_name} is mounted at {entry['MOUNTPOINT']}"
            for entry_name, entry in self.partition_info.items()
            if entry_name.startswith(os.path.basename(self.disk)) and entry.get("MOUNTPOINT")
        ]
        if mounted_partitions:
            for warning in mounted_partitions:
                logging.warning(warning)
        return bool(mounted_partitions)

    def create_partition_table(self, table_type: str, force: bool = False):
        """Create a partition table on the disk."""
        if self.check_and_warn_mounted_partitions(force=force):
            VmCommon.LogMsg(f"Cannot create partition table on {self.disk} because some partitions are mounted.", msgType=VM_LOG_MSG_TYPE_ERROR)
            return
        try:
            self.hostRoot.VmExec(f"parted -s {self.disk} mklabel {table_type}")
            self.refresh_lsblk_info(refresh_partition=True)
            logging.info(f"Created {table_type} partition table on {self.disk}")
        except Exception as e:
            VmCommon.LogMsg(f"Failed to create partition table: {e}", msgType=VM_LOG_MSG_TYPE_ERROR)

    def create_partitions(self, partitions: List[Dict], force: bool = False):
        if not partitions:
            partitions = [{"size": "100%"}]

        if self.check_and_warn_mounted_partitions(force=force):
            VmCommon.LogMsg(f"Cannot create partitions on {self.disk} because some partitions are mounted.", msgType=VM_LOG_MSG_TYPE_ERROR)
            return self.get_partitions()

        try:
            start_mb = 1
            total_mb = self.get_disk_size_mib()
            for idx, part in enumerate(partitions):
                part_type = part.get("type", "primary")
                size_mb = convert_size_to_mb(part["size"], total_mb)
                end_mb = min(start_mb + size_mb, total_mb - 1)
                self.hostRoot.VmExec(f"parted -s {self.disk} mkpart {part_type} {start_mb}MiB {end_mb}MiB")
                start_mb = end_mb
            self.refresh_lsblk_info(refresh_partition=True)
            return self.get_partitions()
        except Exception as e:
            VmCommon.LogMsg(f"Failed to create partitions: {e}", msgType=VM_LOG_MSG_TYPE_ERROR)
            return None

    def get_disk_size_mib(self) -> int:
        """Get the size of the disk in MiB."""
        try:
            output = self.hostRoot.VmExec(f"lsblk -b -dn -o SIZE {self.disk}")
            return int(output.strip()) // (1024 * 1024)
        except Exception as e:
            VmCommon.LogMsg(f"Failed to get disk size: {e}", msgType=VM_LOG_MSG_TYPE_ERROR)
            return 0

    def delete_partition(self, partition: str, force: bool = False):
        """
        Delete a specific partition on the disk.
        :param partition: The partition to delete (e.g., /dev/sdb1).
        :param force: If True, unmount the partition if it is mounted before deleting.
        """
        mountpoint = self.partition_info.get(partition, {}).get("MOUNTPOINT", "")
        if mountpoint:
            if force:
                logging.warning(f"{partition} is mounted at {mountpoint}. Force mode enabled. Attempting to unmount.")
                self.unmount_partition(partition)
            else:
                VmCommon.LogMsg(f"Cannot delete {partition} because it is mounted at {mountpoint}.", msgType=VM_LOG_MSG_TYPE_ERROR)
                return

        try:
            base_name = get_base_disk(partition)
            if not base_name.startswith("/dev/"):
                base_name = f"/dev/{base_name}"
            logging.info(f"Deleting partition {partition} using parted.")
            self.hostRoot.VmExec(f"parted -s {base_name} rm {partition.split('p')[1]}")
            self.refresh_lsblk_info(refresh_partition=True)
            logging.info(f"Successfully deleted partition {partition}")
        except Exception as e:
            VmCommon.LogMsg(f"Failed to delete partition {partition}: {e}", msgType=VM_LOG_MSG_TYPE_ERROR)

    def delete_all_partitions(self, force: bool = False):
        """Delete all partitions on the disk."""
        if self.check_and_warn_mounted_partitions(force=force):
            if force:
                logging.warning("Force mode enabled. Attempting to unmount all mounted partitions.")
                for name, entry in self.partition_info.items():
                    mountpoint = entry.get("MOUNTPOINT", "")
                    if name.startswith(os.path.basename(self.disk)) and mountpoint:
                        self.unmount_partition(mountpoint)
            else:
                VmCommon.LogMsg(f"Cannot delete partitions on {self.disk} because some partitions are mounted.", msgType=VM_LOG_MSG_TYPE_ERROR)
                return
        try:
            self.hostRoot.VmExec(f"wipefs -a {self.disk}")
            self.refresh_lsblk_info(refresh_partition=True)
            logging.info(f"Deleted all partitions and wiped filesystem signatures on {self.disk}")
        except Exception as e:
            VmCommon.LogMsg(f"Failed to delete partitions: {e}", msgType=VM_LOG_MSG_TYPE_ERROR)

    def format_partition(self, partition: str, fstype: str, block_size: Optional[int] = 4096, force: bool = False):
        """Format a partition with the specified filesystem."""
        if partition == get_base_disk(partition):
            logging.warning(f"Cannot format the base disk {partition}. Please specify a partition.")
            return False
        mountpoint = self.partition_info.get(partition, {}).get("MOUNTPOINT", "")
        if mountpoint:
            if force:
                logging.warning(f"{partition} is mounted at {mountpoint}. Force mode enabled. Attempting to unmount.")
                self.unmount_partition(partition)
            else:
                VmCommon.LogMsg(f"Cannot format {partition} because it is mounted at {mountpoint}.", msgType=VM_LOG_MSG_TYPE_ERROR)
                return False
        if not partition.startswith("/dev/"):
            partition = f"/dev/{partition}"
        cmd = {
            "ext4": f"echo y | mkfs.ext4 {'-b ' + str(block_size) if block_size else ''} {partition}",
            "xfs": f"echo y | mkfs.xfs {'-b size=' + str(block_size) if block_size else ''} {'-f' if force else ''} {partition}",
        }.get(fstype)
        if not cmd:
            raise ValueError("Unsupported filesystem type")
        self.hostRoot.VmExec(cmd)
        logging.info(f"Formatted {partition} with {fstype}")
        self.refresh_lsblk_info(refresh_partition=True)
        return True

    def mount_partition(self, partition: str, mount_point: str = None):
        """Mount a partition to a mount point."""
        if partition == get_base_disk(partition):
            logging.warning(f"Cannot mount the base disk {partition}. Please specify a partition.")
            return ""
        partition_info = self.partition_info.get(partition, {})
        if mountpoint := partition_info.get("MOUNTPOINT", ""):
            logging.warning(f"{partition} is already mounted at {mountpoint}. Skipping mount.")
            return mountpoint
        mount_path = mount_point or f"/root/mnt/{partition}_{partition_info.get("FSTYPE", "")}"
        self.hostRoot.VmExec(f"mkdir -p {mount_path}", ignoreError=True)
        if not partition.startswith("/dev/"):
            partition = f"/dev/{partition}"
        self.hostRoot.VmExec(f"mount {partition} {mount_path}")
        logging.info(f"Mounted {partition} to {mount_path}")
        self.refresh_lsblk_info(refresh_partition=True)
        return mount_path

    def unmount_partition(self, mount_path: str):
        """Unmount a partition."""
        try:
            self.hostRoot.VmExec(f"umount {mount_path}")
            logging.info(f"Unmounted {mount_path}")
            self.refresh_lsblk_info(refresh_partition=True)
        except Exception as e:
            VmCommon.LogMsg(f"Failed to unmount {mount_path}: {e}", msgType=VM_LOG_MSG_TYPE_ERROR)

    def get_partitions(self, refresh: bool = False) -> Dict[str, Dict[str, str]]:
        """
        Get partition information for the disk.
        :param refresh: If True, refresh the partition information before returning it.
        :return: A dictionary containing partition information.
        """
        if refresh:
            self.refresh_lsblk_info(refresh_partition=True)
        return self.partition_info

    def get_partition_by_index(self, index: int, refresh: bool = False) -> Optional[Dict[str, str]]:
        """
        Get information about a specific partition by its index.
        :param index: The index of the partition (0-based).
        :param refresh: If True, refresh the partition information before retrieving it.
        :return: A dictionary containing information about the specified partition, or None if not found.
        """
        if refresh:
            self.refresh_lsblk_info(refresh_partition=True)

        partition_name = get_partition_name(self.disk, index)
        return self.partition_info.get(partition_name)
