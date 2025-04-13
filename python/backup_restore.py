#!/usr/bin/env python3

import argparse
import logging
import shlex
import subprocess
import sys
from typing import Optional

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


class QemuImgManager:
    def __init__(self, runner=None):
        self.runner = runner or CommandRunner()

    def backup(self, disk, output):
        self.runner.run(f"qemu-img convert -O qcow2 {disk} {output}")
        print(f"[+] Backup complete: {output}")

    def restore(self, image, disk):
        self.runner.run(f"qemu-img convert -n -O raw {image} {disk}")
        print(f"[+] Restore complete: {disk}")

    def list_info(self, image):
        info = self.runner.run(f"qemu-img info {image}")
        print("[+] Image info:")
        print(info)

    def verify(self, image):
        print("[+] Verifying image...")
        result = self.runner.run(
            f"qemu-img check {image}",
        )
        print(result)

    def compress_only(self, image, output):
        self.runner.run(f"qemu-img convert -c -O qcow2 {image} {output}")
        print(f"[+] Compressed image written to: {output}")

    def resize(self, image, size):
        self.runner.run(f"qemu-img resize {image} {size}")
        print(f"[+] Resized {image} to {size}")

    def snapshot_create(self, image, name):
        self.runner.run(f"qemu-img snapshot -c {name} {image}")
        print(f"[+] Snapshot '{name}' created")

    def snapshot_list(self, image):
        out = self.runner.run(f"qemu-img snapshot -l {image}")
        print("[+] Snapshot list:")
        print(out)

    def snapshot_apply(self, image, name):
        self.runner.run(f"qemu-img snapshot -a {name} {image}")
        print(f"[+] Reverted to snapshot '{name}'")

    def mount_image(self, image, mount_point):
        self.runner.run(f"guestmount -a {image} -i {mount_point}")
        print(f"[+] Mounted {image} at {mount_point}")


def main():
    parser = argparse.ArgumentParser(description="Disk Backup and Restore Utility (Python Version)")
    subparsers = parser.add_subparsers(dest="command", required=True)

    p_backup = subparsers.add_parser("backup", help="Backup a raw disk to a qcow2 image")
    p_backup.add_argument("disk", help="Raw disk device path (e.g., /dev/sdX)")
    p_backup.add_argument("output", help="Output qcow2 image path")

    p_restore = subparsers.add_parser("restore", help="Restore a qcow2 image to a raw disk")
    p_restore.add_argument("image", help="Input qcow2 image path")
    p_restore.add_argument("disk", help="Target raw disk device path")

    p_list = subparsers.add_parser("list", help="Show image details")
    p_list.add_argument("image", help="Path to qcow2 image")

    p_verify = subparsers.add_parser("verify", help="Verify a qcow2 image")
    p_verify.add_argument("image", help="Path to qcow2 image")

    p_compress = subparsers.add_parser("compress-only", help="Compress a qcow2 image")
    p_compress.add_argument("image", help="Path to original image")
    p_compress.add_argument("output", help="Path to write compressed image")

    p_resize = subparsers.add_parser("resize", help="Resize a qcow2 image")
    p_resize.add_argument("image")
    p_resize.add_argument("size")

    p_snap_c = subparsers.add_parser("snapshot-create", help="Create snapshot")
    p_snap_c.add_argument("image")
    p_snap_c.add_argument("name")

    p_snap_l = subparsers.add_parser("snapshot-list", help="List snapshots")
    p_snap_l.add_argument("image")

    p_snap_a = subparsers.add_parser("snapshot-apply", help="Apply snapshot")
    p_snap_a.add_argument("image")
    p_snap_a.add_argument("name")

    p_mount = subparsers.add_parser("mount", help="Mount qcow2 image using guestmount")
    p_mount.add_argument("image")
    p_mount.add_argument("mount_point")

    args = parser.parse_args()
    manager = QemuImgManager()

    if args.command == "backup":
        manager.backup(args.disk, args.output)
    elif args.command == "restore":
        manager.restore(args.image, args.disk)
    elif args.command == "list":
        manager.list_info(args.image)
    elif args.command == "verify":
        manager.verify(args.image)
    elif args.command == "compress-only":
        manager.compress_only(args.image, args.output)
    elif args.command == "resize":
        manager.resize(args.image, args.size)
    elif args.command == "snapshot-create":
        manager.snapshot_create(args.image, args.name)
    elif args.command == "snapshot-list":
        manager.snapshot_list(args.image)
    elif args.command == "snapshot-apply":
        manager.snapshot_apply(args.image, args.name)
    elif args.command == "mount":
        manager.mount_image(args.image, args.mount_point)


if __name__ == "__main__":
    main()
