from pathlib import Path
from qemu_launcher.shell import run_command


def check_or_create_file(filename, size_gb):
    if not Path(filename).exists():
        run_command(f"qemu-img create -f qcow2 {filename} {size_gb}G")
    return run_command(f"lsof -w {filename}").returncode != 0


def configure_nvme(args, env, params):
    nvme_files = env.get("nvme_images", [])
    if not nvme_files:
        return

    vmuid = env["uid"]
    num_queues = args.num_queues or 32
    nssize = args.nssize or 1
    nvmes = args.nvme.split(",") if args.nvme else [f"nvme{vmuid}"]

    index = 1
    nvme_params = ["-device ioh3420,bus=pcie.0,id=root1.0,slot=1", "-device x3130-upstream,bus=root1.0,id=upstream1.0"]

    for i, nvme_path in enumerate(nvme_files, 1):
        nvme_name = Path(nvme_path).stem
        ctrl_id = index
        base_file = f"{nvme_name}n1.qcow2"
        did_opt = f",did={args.did}" if args.did else ""
        mn_opt = f",mn={args.mn}" if args.mn else ""
        fdp_opt = ",fdp=on,fdp.runs=96M,fdp.nrg=1,fdp.nruh=16" if args.fdp else ""
        fdp_nsid = ",fdp.ruhs=1-15" if args.fdp else ""

        nvme_params += [
            f"-device xio3130-downstream,bus=upstream1.0,id=downstream1.{ctrl_id},chassis={ctrl_id},multifunction=on",
            f"-device nvme-subsys,id=nvme-subsys-{ctrl_id},nqn=subsys{ctrl_id}{fdp_opt}",
            f"-device nvme,serial=beef{nvme_name},ocp=on,id={nvme_name},subsys=nvme-subsys-{ctrl_id},bus=downstream1.{ctrl_id},max_ioqpairs={num_queues}{did_opt}{mn_opt}",
        ]

        for nsid in range(1, args.numns + 1):
            ns_file = base_file.replace("n1", f"n{nsid}")
            if check_or_create_file(ns_file, nssize):
                nvme_params += [
                    f"-drive file={ns_file},id={nvme_name}{nsid},if=none,cache=none",
                    f"-device nvme-ns,drive={nvme_name}{nsid},bus={nvme_name},nsid={nsid}{fdp_nsid if nsid == 1 else ''}",
                ]

        index += 1

    params += nvme_params
