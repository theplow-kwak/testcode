from pathlib import Path


def configure_disks(args, env, params):
    index = 0
    scsi_needed = args.arch != "riscv64"
    if scsi_needed:
        params.append("-object iothread,id=iothread0")
        params.append("-device virtio-scsi-pci,id=scsi0,iothread=iothread0")

    for image in env.get("disks", []):
        suffix = Path(image).suffix.lower()
        if suffix == ".qcow2":
            params.append(f"-drive file={image},cache=writeback,id=drive-{index}")
        elif suffix == ".vhdx":
            params += [f"-drive file={image},if=none,id=drive-{index}", f"-device nvme,drive=drive-{index},serial=nvme-{index}"]
        else:
            params += [
                f"-drive file={image},if=none,format=raw,discard=unmap,aio=native,cache=none,id=drive-{index}",
                f"-device scsi-hd,scsi-id={index},drive=drive-{index},id=scsi0-{index}",
            ]
        index += 1
