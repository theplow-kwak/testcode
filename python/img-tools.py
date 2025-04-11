#!/usr/bin/env python3

import argparse
import subprocess
import sys
import shlex


class CommandRunner:
    def __init__(self, capture_output=True, check=True, print_command=True, print_output=False):
        self.capture_output = capture_output
        self.check = check
        self.print_command = print_command
        self.print_output = print_output

    def run(self, command):
        if self.print_command:
            print(f"[+] Running: {command}")
        args = shlex.split(command)
        result = subprocess.run(args, capture_output=self.capture_output, text=True)
        if self.capture_output and self.print_output:
            print(result.stdout)
        if self.check and result.returncode != 0:
            print(f"[!] Error:\n{result.stderr}")
            sys.exit(result.returncode)
        return result.stdout.strip() if self.capture_output else None


class DiskManager:
    def __init__(self, runner=None):
        self.runner = runner or CommandRunner()

    def execute_action(self, action, *args):
        """Map actions to methods dynamically."""
        actions = {
            "backup": self.backup,
            "restore": self.restore,
            "list": self.list_info,
            "verify": self.verify,
            "compress-only": self.compress_only,
            "resize": self.resize,
            "snapshot-create": self.snapshot_create,
            "snapshot-list": self.snapshot_list,
            "snapshot-apply": self.snapshot_apply,
            "mount": self.mount_image,
        }
        if action in actions:
            actions[action](*args)
        else:
            print(f"[!] Unknown action: {action}")
            sys.exit(1)

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
        result = self.runner.run(f"qemu-img check {image}")
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

    # Define a common parser for shared arguments
    common_parser = argparse.ArgumentParser(add_help=False)
    common_parser.add_argument("image", help="Path to qcow2 image")

    p_backup = subparsers.add_parser("backup", help="Backup a raw disk to a qcow2 image")
    p_backup.add_argument("disk", help="Raw disk device path (e.g., /dev/sdX)")
    p_backup.add_argument("output", help="Output qcow2 image path")

    p_restore = subparsers.add_parser("restore", help="Restore a qcow2 image to a raw disk")
    p_restore.add_argument("image", help="Input qcow2 image path")
    p_restore.add_argument("disk", help="Target raw disk device path")

    p_list = subparsers.add_parser("list", help="Show image details", parents=[common_parser])

    p_verify = subparsers.add_parser("verify", help="Verify a qcow2 image", parents=[common_parser])

    p_compress = subparsers.add_parser("compress-only", help="Compress a qcow2 image", parents=[common_parser])
    p_compress.add_argument("output", help="Path to write compressed image")

    p_resize = subparsers.add_parser("resize", help="Resize a qcow2 image", parents=[common_parser])
    p_resize.add_argument("size")

    p_snap_c = subparsers.add_parser("snapshot-create", help="Create snapshot", parents=[common_parser])
    p_snap_c.add_argument("name")

    p_snap_l = subparsers.add_parser("snapshot-list", help="List snapshots", parents=[common_parser])

    p_snap_a = subparsers.add_parser("snapshot-apply", help="Apply snapshot", parents=[common_parser])
    p_snap_a.add_argument("name")

    p_mount = subparsers.add_parser("mount", help="Mount qcow2 image using guestmount", parents=[common_parser])
    p_mount.add_argument("mount_point")

    args = parser.parse_args()
    manager = DiskManager()

    # Dynamically execute the appropriate action
    action_args = vars(args)
    command = action_args.pop("command")
    manager.execute_action(command, *action_args.values())


if __name__ == "__main__":
    main()
