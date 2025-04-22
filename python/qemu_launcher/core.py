import os
import shlex
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
from qemu_launcher.device.uefi import configure_uefi

logger = set_logger("QEMU")


class QemuLauncher:
    def __init__(self):
        self.args = None
        self.params = []
        self.opts = []
        self.kernel_args = []
        self.connect_info = {}
        self.env = {
            "home": str(Path.home()),
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
        self._configure_base_devices()
        if self._find_proc(self.env["procid"]):
            configure_network(self.args, self.env, self.params)
            configure_connection(self.args, self.env, self.opts, self._term(), self.connect_info)
        else:
            configure_uefi(self.args, self.env, self.params)
            configure_kernel(self.args, self.kernel_args, self.args.images, self.args.arch, self.args.connect)
            configure_disks(self.args, self.params)
            configure_nvme(self.args, self.env, self.params)
            configure_network(self.args, self.env, self.params)
            configure_spice(self.args, self.env, self.params)
            configure_virtiofs(self.args, self.env, self.params, self._sudo(), self._term("--geometry=80x24+5+5"))
            configure_tpm(self.args, self.env, self.params)
            configure_usb(self.args.arch, self.args, self.params, index=0)
            configure_usb_storage(self.args, self.params)
            configure_pci_passthrough(self.args, self.params)
            configure_connection(self.args, self.env, self.opts, self._term(), self.connect_info)
            if self.args.ext:
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

    def _wait_and_connect(self):
        if self.args.debug == "cmd":
            print(" ".join(self.connect_info["command"]))
            return
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
            time.sleep(5)
        logger.warning("SSH connection timed out")
        return False

    def _find_proc(self, name, timeout=0):
        while run_command(f"ps -C {name}").returncode:
            if timeout <= 0:
                return False
            timeout -= 1
            time.sleep(5)
        return True

    def _parse_args(self):
        parser = build_parser()
        self.args = parser.parse_args()
        logger.setLevel("INFO" if self.args.debug == "cmd" else self.args.debug.upper())

    def _detect_boot_image(self):
        if not self.args.images:
            raise RuntimeError("No boot image provided")
        self.env["boot"] = self.args.images[0]
        self.env["boot_type"] = "1" if self.env["boot"].startswith("nvme") else ""
        self.env["vmname"] = Path(self.env["boot"]).stem

    def _generate_identifiers(self):
        boot_str = "".join(self.args.images)
        guid = hashlib.md5(boot_str.encode()).hexdigest()
        self.env["guid"] = guid
        self.env["uid"] = guid[:2]
        self.env["procid"] = f"{self.env['vmname'][:12]}_{self.env['uid']}"

    def _set_memory_size(self):
        phy_mem = int(os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES") / (1024 * 1024 * 1000))
        self.env["memsize"] = "8G" if phy_mem > 8 else "4G"

    def _configure_base_devices(self):
        self.params += [f"-name {self.env['vmname']},process={self.env['procid']}"]
        if self.args.ext:
            self.params += shlex.split(self.args.ext)

        def _set_x86_64_params():
            """Set parameters specific to x86_64 architecture."""
            params = [
                f"-machine type={self.args.machine},accel=kvm,usb=on -device intel-iommu",
                (
                    "-cpu Skylake-Client-v3,hv_stimer,hv_synic,hv_relaxed,hv_reenlightenment,hv_spinlocks=0xfff,hv_vpindex,hv_vapic,hv_time,hv_frequencies,hv_runtime,+kvm_pv_unhalt,+vmx --enable-kvm"
                    if self.args.hvci
                    else "-cpu host --enable-kvm"
                ),
                "-object rng-random,id=rng0,filename=/dev/urandom -device virtio-rng-pci,rng=rng0",
            ]
            if not self.args.hvci and self.args.vender:
                params.append(f"-smbios type=1,manufacturer={self.args.vender},product='{self.args.vender} Notebook PC'")
            if self.args.connect != "ssh":
                self.opts.append(f"-vga {self.args.vga}")
            return params

        arch_params = {
            "riscv64": ["-machine virt -bios none"],
            "arm": ["-machine virt -cpu cortex-a53 -device ramfb"],
            "aarch64": ["-machine virt,virtualization=true -cpu cortex-a72 -device ramfb"],
            "x86_64": _set_x86_64_params(),
        }
        self.params.extend(arch_params.get(self.args.arch, []))
        self.params.extend([f"-m {self.env["memsize"]}", f"-smp {os.cpu_count() // 2},sockets=1,cores={os.cpu_count() // 2},threads=1", "-nodefaults", "-rtc base=localtime"])

    def _build_qemu_command(self):
        arch = self.args.arch
        qemu_bin = f"qemu-system-{arch}"
        qemu_path = qemu_bin if self.args.qemu else f"{self.env['home']}/qemu/bin/{qemu_bin}"
        return self._sudo() + self._term() + [qemu_path] + self.params + self.opts + self.kernel_args

    def _sudo(self):
        return ["sudo"] if os.getuid() != 0 else []

    def _term(self, option=""):
        if not option and (self.args.consol or self.args.debug == "debug"):
            return []
        return ["gnome-terminal", f"--title={self.env["procid"]}", option, "--"]
