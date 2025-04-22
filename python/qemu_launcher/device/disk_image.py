from qemu_launcher.shell import run_command


def resolve_disk_to_images(args):
    if not args.disk:
        return
    disk_parts = args.disk.lower().split(":")
    base_disk = disk_parts.pop(0)
    result = run_command("lsblk -d -o NAME,MODEL,SERIAL --sort NAME -n -e7")
    if result.returncode != 0:
        return
    disks = [line.split()[0] for line in result.stdout.splitlines() if base_disk in line.lower()]
    for d in disks:
        part = disk_parts.pop(0) if disk_parts else ""
        args.images.append(f"/dev/{d}{part}")
