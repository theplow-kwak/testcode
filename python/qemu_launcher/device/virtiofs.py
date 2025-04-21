from pathlib import Path
from time import sleep
from qemu_launcher.shell import run_command
import logging

logger = logging.getLogger("QEMU")


def configure_virtiofs(args, env, params, sudo_prefix, terminal_prefix):
    if args.noshare:
        return

    sock_path = f"/tmp/virtiofs_{env['uid']}.sock"
    virtiofs_bin = f"{env['home']}/qemu/libexec/virtiofsd"
    if not Path(virtiofs_bin).exists():
        virtiofs_bin = "/usr/libexec/virtiofsd"

    virtiofsd_cmd = sudo_prefix + terminal_prefix + [f"{virtiofs_bin} --socket-path={sock_path} -o source={env['home']}"]

    run_command(virtiofsd_cmd)
    while not Path(sock_path).exists():
        logger.debug(f"Waiting for {sock_path}")
        sleep(1)

    params += [
        f"-chardev socket,id=char{env['uid']},path={sock_path}",
        f"-device vhost-user-fs-pci,chardev=char{env['uid']},tag=hostfs",
        f"-object memory-backend-memfd,id=mem,size={env['memsize']},share=on",
        "-numa node,memdev=mem",
    ]
