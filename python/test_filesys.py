#!/usr/bin/python3

import subprocess
import unittest
from unittest.mock import patch, MagicMock
from filesys import CommandRunner, DiskManager, PartitionManager

class TestCommandRunner(unittest.TestCase):
    @patch("filesys.subprocess.run")
    def test_execute_success(self, mock_run):
        mock_run.return_value = MagicMock(stdout="output", returncode=0)
        result = CommandRunner.execute("echo test")
        self.assertEqual(result, "output")
        mock_run.assert_called_once()

    @patch("filesys.subprocess.run")
    def test_execute_failure(self, mock_run):
        mock_run.side_effect = subprocess.CalledProcessError(
            returncode=1, cmd="echo test", output="error", stderr="stderr"
        )
        with self.assertRaises(RuntimeError):
            CommandRunner.execute("echo test")

    @patch("filesys.subprocess.Popen")
    def test_execute_background(self, mock_popen):
        CommandRunner.execute("echo test", background=True)
        mock_popen.assert_called_once()


class TestDiskManager(unittest.TestCase):
    @patch("filesys.CommandRunner.execute")
    def test_validate_disk(self, mock_execute):
        mock_execute.return_value = "/dev/nvme0n1\n/dev/nvme0n2"
        with self.assertRaises(ValueError):
            DiskManager("/dev/nvme0n1")  # Ensure the disk is in the list

    @patch("filesys.CommandRunner.execute")
    def test_create_partition(self, mock_execute):
        mock_execute.return_value = ""
        disk_manager = DiskManager("/dev/nvme0n2")
        disk_manager.create_partition(partition_type="gpt", num_partitions=2)
        self.assertEqual(mock_execute.call_count, 4)  # 1 for mklabel, 3 for mkpart (2 partitions + confirmation)

    @patch("filesys.CommandRunner.execute")
    def test_delete_partition(self, mock_execute):
        mock_execute.return_value = "nvme0n2p1\nnvme0n2p2"
        disk_manager = DiskManager("/dev/nvme0n2")
        disk_manager.delete_partition()
        self.assertGreaterEqual(mock_execute.call_count, 2)  # At least 2 partitions deleted

    @patch("filesys.CommandRunner.execute")
    def test_get_partitions(self, mock_execute):
        mock_execute.return_value = "nvme0n2p1 10G part\nnvme0n2p2 20G part"
        disk_manager = DiskManager("/dev/nvme0n2")
        partitions = disk_manager.get_partitions()
        self.assertEqual(len(partitions), 2)
        self.assertEqual(partitions[0]["name"], "/dev/nvme0n2p1")
        self.assertEqual(partitions[1]["size"], "20G")


class TestPartitionManager(unittest.TestCase):
    @patch("filesys.CommandRunner.execute")
    def test_format_partition(self, mock_execute):
        partition_manager = PartitionManager("/dev/nvme0n2p1")
        partition_manager.format_partition(filesystem="ext4", block_size="4096")
        mock_execute.assert_called_once_with("mkfs.ext4 -b 4096 /dev/nvme0n2p1")

    @patch("filesys.CommandRunner.execute")
    def test_mount_partition(self, mock_execute):
        partition_manager = PartitionManager("/dev/nvme0n2p1")
        partition_manager.mount_partition("/mnt/test")
        self.assertEqual(mock_execute.call_count, 2)  # mkdir and mount

    @patch("filesys.CommandRunner.execute")
    def test_unmount_partition(self, mock_execute):
        partition_manager = PartitionManager("/dev/nvme0n2p1")
        partition_manager.unmount_partition("/mnt/test")
        mock_execute.assert_called_once_with("umount /mnt/test")


if __name__ == "__main__":
    unittest.main()