from pathlib import Path
from qemu_launcher.shell import run_command


def remove_ssh(args, env):
    ssh_file = Path(f"/tmp/{env['procid']}_SSH")
    ssh_file.unlink(missing_ok=True)
    host = env["hostip"] if args.net == "user" else env["localip"]
    port = env["ssh_port"] if args.net == "user" else ""
    cmd = f'ssh-keygen -R "[{host}]:{port}"' if port else f'ssh-keygen -R "{host}"'
    run_command(cmd)
