#!/usr/bin/env python3

import argparse
import functools
import hashlib
import logging
import os
import re
import shlex
import subprocess
from pathlib import Path
from time import sleep
from typing import Optional


logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s")
logger = logging.getLogger(__name__)


class QEMU:
    def __init__(self):
        self.vmimages: list[str] = []
        self.vmcdimages: list[str] = []
        self.vmnvme: list[str] = []
        self.params: list[str] = []
        self.opts: list[str] = []
        self.kernel: list[str] = []
        self.index = 0
        self.home_folder = f"/home/{os.getlogin()}"
        self.sudo = ["sudo"] if os.getuid() else []

    def run_command(self, cmd: str | list[str], _async: bool = False, _consol: bool = False):
        """Run shell commands with optional async and console output."""
        if isinstance(cmd, list):
            cmd = " ".join(cmd)
        _cmd = shlex.split(cmd)
        logger.debug(f"runshell {'Async' if _async else ''}: {cmd}")
        if _consol:
            completed = subprocess.run(_cmd, text=True)
        elif _async:
            completed = subprocess.Popen(_cmd, text=True)
            sleep(1)
        else:
            completed = subprocess.run(_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            if completed.stdout:
                logger.debug(f"Return code: {completed.returncode}, stdout: {completed.stdout.rstrip()}")
        return completed

    def sudo_run(self, cmd: str | list[str], _async: bool = False, _consol: bool = False):
        """Run shell commands with sudo."""
        if isinstance(cmd, str):
            cmd = shlex.split(cmd)
        cmd = self.sudo + cmd
        return self.run_command(cmd, _async, _consol)

    def parse_disks(self):
        for disk in self.args.disk:
            _result = self.run_command(f"lsblk -d -o NAME,MODEL,SERIAL --sort NAME -n -e7")
            if _result.returncode == 0:
                _disk_param = disk.lower().split(":")
                _disk = _disk_param.pop(0)
                stdout_str: str = _result.stdout.decode() if isinstance(_result.stdout, bytes) else str(_result.stdout)
                lines: list[str] = stdout_str.splitlines()
                _images: list[str] = [line.split()[0] for line in lines if _disk in line.lower()]
                for _image in _images:
                    _part = _disk_param.pop(0) if _disk_param else ""
                    self.args.images.append(f"/dev/{_image}{_part}")

    def set_args(self):
        parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)
        parser.add_argument("--consol", action="store_true", help="Use current terminal as console I/O")
        parser.add_argument("--qemu", "-q", action="store_true", help="Use public QEMU distribution")
        parser.add_argument("--arch", "-a", default="x86_64", choices=["x86_64", "aarch64", "arm", "riscv64"], help="Target VM architecture")
        parser.add_argument("--debug", "-d", nargs="?", const="info", default="warning", choices=["cmd", "debug", "info", "warning"], help="Set logging level")
        parser.add_argument("images", metavar="IMAGES", nargs="*", help="Set VM images")
        parser.add_argument("--nvme", nargs="+", action="extend", help="Set NVMe images")
        parser.add_argument("--disk", nargs="+", action="extend", help="Set disk image")
        parser.add_argument("--kernel", dest="vmkernel", help="Set Linux Kernel image")
        parser.add_argument("--rootdev", help="Set root filesystem device")
        parser.add_argument("--initrd", help="Set initrd image")
        parser.add_argument("--numns", type=int, help="Set number of NVMe namespaces")
        parser.add_argument("--nssize", type=int, default=1, help="Set size of NVMe namespace")
        parser.add_argument("--num_queues", type=int, default=32, help="Set max number of queues")
        parser.add_argument("--sriov", action="store_true", help="Enable SR-IOV")
        parser.add_argument("--fdp", action="store_true", help="Enable FDP")
        parser.add_argument("--did", type=functools.partial(int, base=0), help="Set NVMe device ID")
        parser.add_argument("--mn", help="Set model name")
        self.args = parser.parse_args()
        if self.args.debug == "cmd":
            logger.setLevel("INFO")
        else:
            logger.setLevel(self.args.debug.upper())
        if self.args.disk:
            self.parse_disks()
        if self.args.nvme:
            self.vmnvme.extend(self.args.nvme)

    def set_images(self):
        print(f"nvmes: {self.args.nvme}")
        print(f"disks: {self.args.disk}")
        print(f"images: {self.args.images}")
        image_extensions = {".img", ".qcow2", ".vhdx"}
        for image in self.args.images:
            image_path = Path(image)
            if re.match(r"^(nvme\d+):?(\d+)?$", image):
                self.vmnvme.append(image)
            elif image_path.exists() and image_path.is_block_device():
                self.vmimages.append(image)
            elif image_path.exists():
                _, ext = os.path.splitext(image)
                ext = ext.lower()
                if ext in image_extensions:
                    self.vmimages.append(image)
                elif ext == ".iso":
                    self.vmcdimages.append(image)
            elif "vmlinuz" in image and not self.args.vmkernel:
                self.args.vmkernel = image

        _boot_dev = self.vmimages + self.vmnvme + self.vmcdimages
        if self.args.vmkernel:
            _boot_dev.append(self.args.vmkernel)
        logger.info(f"vmimages {self.vmimages} ")
        logger.info(f"vmcdimages {self.vmcdimages} ")
        logger.info(f"vmnvme {self.vmnvme} ")
        logger.info(f"vmkernel {self.args.vmkernel} ")
        logger.info(f"boot_dev {_boot_dev} ")
        if not _boot_dev:
            raise Exception("There is no Boot device!!")
        self.bootype = "1" if self.vmnvme and self.vmnvme[0] == _boot_dev[0] else ""
        boot_0 = Path(_boot_dev[0]).resolve()
        self.vmboot = _boot_dev[0]
        self.vmname = boot_0.stem
        self.vmguid = hashlib.md5(("".join([x for x in _boot_dev])).encode()).hexdigest()
        self.vmuid = self.vmguid[0:2]
        self.vmprocid = f"{self.vmname[0:12]}_{self.vmuid}"
        self.G_TERM = [f"gnome-terminal --title={self.vmprocid}"]

    def configure_disks(self):
        disks_params: list[str] = []
        for _image in self.vmimages:
            print(f"Processing image: {_image}")
            match _image.split("."):
                case ["wiftest", *_]:
                    disks_params += [
                        f"-drive if=none,cache=none,file=blkdebug:blkdebug.conf:{_image},format=qcow2,id=drive-{self.index}",
                        f"-device virtio-blk-pci,drive=drive-{self.index},id=virtio-blk-pci{self.index}",
                    ]
                case [*_, "qcow2" | "QCOW2"]:
                    disks_params += [f"-drive file={_image},cache=writeback,id=drive-{self.index}"]
                case [*_, "vhdx" | "VHDX"]:
                    disks_params += [
                        f"-drive file={_image},if=none,id=drive-{self.index}",
                        f"-device nvme,drive=drive-{self.index},serial=nvme-{self.index}",
                    ]
                case _:
                    # _disk_type = 'scsi-hd' if Path(_image).is_block_device and 'nvme' not in _image else 'scsi-hd'
                    disks_params += [
                        f"-drive file={_image},if=none,format=raw,discard=unmap,aio=native,cache=none,id=drive-{self.index}",
                        f"-device scsi-hd,scsi-id={self.index},drive=drive-{self.index},id=scsi0-{self.index}",
                    ]
            self.index += 1
        if disks_params:
            self.params += disks_params

    def configure_cdrom(self):
        """Configure CD-ROM drives for the VM."""
        _IF = "ide" if self.args.arch == "x86_64" else "none"
        for _image in self.vmcdimages:
            self.params.append(f"-drive file={_image},media=cdrom,readonly=on,if={_IF},index={self.index},id=cdrom{self.index}")
            if self.args.arch != "x86_64":
                self.params.append(f"-device usb-storage,drive=cdrom{self.index}")
            self.index += 1

    def check_file(self, filename: str, size: int) -> int:
        if not Path(filename).exists():
            self.run_command(f"qemu-img create -f qcow2 {filename} {size}G")
        if self.run_command(f"lsof -w {filename}").returncode == 0:
            return 0
        return 1

    def configure_nvme(self):
        """Set NVMe devices."""
        if not self.vmnvme:
            return

        def add_nvme_namespace(_NVME: str, ns_backend: str, _nsid: int = 1, nsid_params: str = "") -> list[str]:
            """Helper to add NVMe drive and namespace."""
            NVME: list[str] = []
            if self.check_file(ns_backend, _ns_size):
                NVME.append(f"-drive file={ns_backend},id={_NVME}{_nsid or ''},if=none,cache=none")
                NVME.append(f"-device nvme-ns,drive={_NVME}{_nsid},bus={_NVME},nsid={_nsid}{nsid_params}")
            return NVME

        nvme_params = [
            "-device ioh3420,bus=pcie.0,id=root1.0,slot=1",
            "-device x3130-upstream,bus=root1.0,id=upstream1.0",
        ]

        _ns_size = self.args.nssize
        _ctrl_id = 1
        for nvme in self.vmnvme:
            print(f"Processing nvme: {nvme}")
            match = re.match(r"^(?P<nvme_id>nvme\d+)(?::(?P<num_ns>\d+))?(?:n(?P<ns_id>\d+)(?P<ext>\.[a-zA-Z0-9]+))?", nvme)
            _nvme_id = match.group("nvme_id") or "nvme0"
            _num_ns = match.group("num_ns") or "1"
            _ns_id = match.group("ns_id")
            _ext = match.group("ext") or ".qcow2"
            print(f"{nvme}: NVME {_nvme_id}, ns_range {_num_ns}, ns_id {_ns_id}, extension {_ext}")

            _did = f",did={self.args.did}" if self.args.did else ""
            _mn = f",mn={self.args.mn}" if self.args.mn else ""
            _fdp_subsys = ",fdp=on,fdp.runs=96M,fdp.nrg=1,fdp.nruh=16" if self.args.fdp else ""
            _fdp_nsid = ",fdp.ruhs=1-15,mcl=2048,mssrl=256,msrc=7" if self.args.fdp else ""
            _sriov_params = f",msix_qsize=512,sriov_max_vfs={_num_ns},sriov_vq_flexible=508,sriov_vi_flexible=510" if self.args.sriov else ""
            _sriov_nsid = f",shared=false,detached=true" if self.args.sriov else ""
            _ioqpairs = f",max_ioqpairs={512 if self.args.sriov else self.args.num_queues}"

            nvme_params += [
                f"-device xio3130-downstream,bus=upstream1.0,id=downstream1.{_ctrl_id},chassis={_ctrl_id},multifunction=on",
                f"-device nvme-subsys,id=nvme-subsys-{_ctrl_id},nqn=subsys{_ctrl_id}{_fdp_subsys}",
                f"-device nvme,serial=beef{_nvme_id},ocp=on,id={_nvme_id},subsys=nvme-subsys-{_ctrl_id},bus=downstream1.{_ctrl_id}{_ioqpairs}{_sriov_params}{_did}{_mn}",
            ]

            ns_backend = ""
            for _nsid in range(1, int(_num_ns) + 1):
                ns_backend = f"{_nvme_id}n{_ns_id or _nsid}{_ext}"
                print(f"Processing namespace {_nsid} for {nvme} => {ns_backend}")
                nvme_params += add_nvme_namespace(_nvme_id, ns_backend, _nsid=_nsid, nsid_params=f"{_fdp_nsid if _nsid == 1 else ''}{_sriov_nsid}")
            _ctrl_id += 1

        if Path("./events").exists():
            nvme_params.append("--trace events=./events")

        self.params.extend(nvme_params)

    def setting(self):
        self.set_args()
        self.set_images()
        self.configure_disks()
        self.configure_cdrom()
        self.configure_nvme()

    def run(self):
        """Run the QEMU VM."""
        print(f"Boot: {self.vmboot:<15}")
        qemu_command = ([] if self.args.debug == "debug" else [] if self.args.consol else self.G_TERM + ["--"]) + self.params + self.opts + self.kernel
        print(" ".join(qemu_command))


def main():
    qemu = QEMU()
    qemu.setting()
    qemu.run()


import sys
import traceback

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        logger.error("Keyboard Interrupted")
    except Exception as e:
        logger.error(f"QEMU terminated abnormally. {e}")
        ex_type, ex_value, ex_traceback = sys.exc_info()
        trace_back = traceback.extract_tb(ex_traceback)
        for trace in trace_back[1:]:
            logger.error(f"  File {trace[0]}, line {trace[1]}, in {trace[2]}")
