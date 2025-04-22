import os


def configure_machine(args, env, opts, params):
    """Set QEMU executable and base parameters."""
    params += [f"-name {env['vmname']},process={env["procid"]}"]

    def _set_x86_64_params():
        """Set parameters specific to x86_64 architecture."""
        params = [
            f"-machine type={args.machine},accel=kvm,usb=on -device intel-iommu",
            (
                "-cpu Skylake-Client-v3,hv_stimer,hv_synic,hv_relaxed,hv_reenlightenment,hv_spinlocks=0xfff,hv_vpindex,hv_vapic,hv_time,hv_frequencies,hv_runtime,+kvm_pv_unhalt,+vmx --enable-kvm"
                if args.hvci
                else "-cpu host --enable-kvm"
            ),
            "-object rng-random,id=rng0,filename=/dev/urandom -device virtio-rng-pci,rng=rng0",
        ]
        if not args.hvci and args.vender:
            params.append(f"-smbios type=1,manufacturer={args.vender},product='{args.vender} Notebook PC'")
        opts.append(f"-vga {args.vga}")
        return params

    arch_params = {
        "riscv64": ["-machine virt -bios none"],
        "arm": ["-machine virt -cpu cortex-a53 -device ramfb"],
        "aarch64": ["-machine virt,virtualization=true -cpu cortex-a72 -device ramfb"],
        "x86_64": _set_x86_64_params(),
    }
    params.extend(arch_params.get(args.arch, []))
    params.extend([f"-m {env["memsize"]}", f"-smp {os.cpu_count() // 2},sockets=1,cores={os.cpu_count() // 2},threads=1", "-nodefaults", "-rtc base=localtime"])
