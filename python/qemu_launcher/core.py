import os
import time
import hashlib
from pathlib import Path

from qemu_launcher.logger import set_logger
from qemu_launcher.shell import run_command
from qemu_launcher.config import build_parser

# Device modules
from qemu_launcher.device.usb import configure_usb
from qemu_launcher.device.kernel import configure_kernel
from qemu_launcher.device.net import configure_network
from qemu_launcher.device.nvme import configure_nvme
from qemu_launcher.device.disks import configure_disks
from qemu_launcher.device.spice import configure_spice
from qemu_launcher.device.virtiofs import configure_virtiofs
from qemu_launcher.device.tpm import configure_tpm
from qemu_launcher.device.connect import configure_connection
from qemu_launcher.device.usb_storage import configure_usb_storage
from qemu_launcher.device.ssh import remove_ssh
from qemu_launcher.device.disk_image import resolve_disk_to_images
from qemu_launcher.device.pci import configure_pci_passthrough

logger = set_logger("QEMU")


class QemuLauncher:
    def __init__(self):
        self.args = None
        self.params = []
        self.opts = []
        self.kernel_args = []
        self.connect_info = {}
        self.env = {
            "home": f"/home/{os.getlogin()}",
            "uid": None,
            "guid": None,
            "procid": None,
            "vmname": None,
            "macaddr": None,
            "ssh_port": 5900,
            "spice_port": 5901,
            "hostip": "localhost",
            "localip": None,
            "memsize": "4G",
        }

    def setup(self):
        self._parse_args()
        resolve_disk_to_images(self.args)
        self._detect_boot_image()
        self._generate_identifiers()
        self._set_memory_size()
        self._resolve_paths()

        configure_usb(self.args.arch, self.args, self.params, index=0)
        configure_kernel(self.args, self.kernel_args, self.args.images, self.args.arch, self.args.connect)
        configure_disks(self.args, self.params)
        configure_nvme(self.args, self.env, self.params)
        configure_network(self.args, self.env, self.params)
        configure_spice(self.env, self.params)
        configure_virtiofs(self.args, self.env, self.params, self._sudo(), self._term())
        if self.args.tpm:
            configure_tpm(self.env, self.params)
        configure_usb_storage(self.args, self.params)
        configure_pci_passthrough(self.args, self.params)
        configure_connection(self.args, self.env, self.opts, self.connect_info)

        if self.args.ext:
            import shlex

            self.params += shlex.split(self.args.ext)

        if self.args.rmssh:
            remove_ssh(self.args, self.env)

    def run(self):
        if not self._find_proc(self.env["procid"]):
            qemu_cmd = self._build_qemu_command()
            if self.args.debug == "cmd":
                print(" ".join(qemu_cmd))
            else:
                result = run_command(qemu_cmd, console=self.args.consol)
                if result.returncode != 0:
                    logger.error("QEMU execution failed")
                    return
                self._wait_and_connect()
        else:
            logger.info("QEMU already running, connecting only.")
            self._wait_and_connect()

    def _wait_and_connect(self):
        if self.connect_info.get("method") == "ssh":
            self._wait_for_ssh(self.connect_info["target"], timeout=60)
        if self.connect_info.get("command"):
            run_command(self.connect_info["command"], async_=True, console=self.args.consol)

    def _wait_for_ssh(self, host, timeout=10):
        logger.info(f"Waiting for SSH to {host}")
        while timeout > 0:
            result = run_command(f"ping -c 1 {host}")
            if result.returncode == 0:
                return True
            timeout -= 1
            time.sleep(1)
        logger.warning("SSH connection timed out")
        return False

    def _find_proc(self, name, timeout=0):
        while timeout >= 0:
            result = run_command(f"ps -C {name}")
            if result.returncode == 0:
                return True
            timeout -= 1
            time.sleep(1)
        return False

    def _parse_args(self):
        parser = build_parser()
        self.args = parser.parse_args()
        logger.setLevel(self.args.debug.upper())

    def _detect_boot_image(self):
        if not self.args.images:
            raise RuntimeError("No boot image provided")
        self.env["boot"] = self.args.images[0]
        self.env["vmname"] = Path(self.env["boot"]).stem

    def _generate_identifiers(self):
        boot_str = "".join(self.args.images)
        guid = hashlib.md5(boot_str.encode()).hexdigest()
        self.env["guid"] = guid
        self.env["uid"] = guid[:2]
        self.env["procid"] = f"{self.env['vmname'][:12]}_{self.env['uid']}"

    def _set_memory_size(self):
        try:
            pages = os.sysconf("SC_PHYS_PAGES")
            page_size = os.sysconf("SC_PAGE_SIZE")
            mem_gb = (pages * page_size) / (1024 * 1024 * 1024)
            self.env["memsize"] = "8G" if mem_gb > 8 else "4G"
        except Exception:
            self.env["memsize"] = "4G"

    def _resolve_paths(self):
        Path("/tmp").mkdir(parents=True, exist_ok=True)

    def _build_qemu_command(self):
        arch = self.args.arch
        qemu_bin = f"qemu-system-{arch}"
        qemu_path = qemu_bin if self.args.qemu else f"{self.env['home']}/qemu/bin/{qemu_bin}"
        return self._sudo() + self._term() + [qemu_path] + self.params + self.opts + self.kernel_args

    def _sudo(self):
        return ["sudo"] if os.getuid() != 0 else []

    def _term(self):
        if self.args.consol or self.args.debug == "debug":
            return []
        return ["gnome-terminal", "--title", self.env["procid"], "--"]
