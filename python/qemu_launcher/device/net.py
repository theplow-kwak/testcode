import os
from pathlib import Path
from qemu_launcher.shell import run_command
import logging

logger = logging.getLogger("QEMU")


def configure_network(args, env, params):
    vmprocid = env["procid"]
    ssh_file = Path(f"/tmp/{vmprocid}_SSH")

    try:
        env["ssh_port"] = int(ssh_file.read_text())
    except FileNotFoundError:
        env["ssh_port"] = 5900

    env["spice_port"] = env["ssh_port"] + 1
    guid = env["guid"]

    mac = f"52:54:00:{guid[0:2]}:{guid[2:4]}:{guid[4:6]}"
    env["macaddr"] = mac

    result = run_command("ip r g 1.0.0.0")
    env["hostip"] = result.stdout.split()[6] if result.returncode == 0 else "localhost"

    result = run_command(f"virsh --quiet net-dhcp-leases default --mac {mac}")
    dhcp_line = sorted(result.stdout.strip().splitlines())[-1] if result.returncode == 0 and result.stdout else ""
    env["localip"] = args.ip or (dhcp_line.split()[4].split("/")[0] if dhcp_line else None)

    if args.net == "user":
        net_str = f"-nic user,model=virtio-net-pci,mac={mac},smb={env['home']},hostfwd=tcp::{env['ssh_port']}-:22"
    elif args.net == "tap":
        net_str = f"-nic tap,model=virtio-net-pci,mac={mac},script={env['home']}/projects/scripts/qemu-ifup"
    elif args.net == "bridge":
        net_str = f"-nic bridge,br=virbr0,model=virtio-net-pci,mac={mac}"
    else:
        net_str = ""

    if net_str:
        params.append(net_str)

    try:
        ssh_file.write_text(str(env["ssh_port"]))
    except Exception as e:
        logger.error(f"Failed to write SSH port: {e}")
