def configure_uefi(args, env, params):
    """Configure UEFI firmware for the VM."""
    ovmf_path = f"{env["home"]}/qemu/share/qemu"
    uefi_params = {
        "x86_64": [
            f"-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M{args.secboot}.fd",
            f"-drive if=pflash,format=raw,file={env["home"]}/vm/OVMF_VARS_4M.ms{env["boot_type"]}.fd",
        ],
        "aarch64": [
            f"-drive if=pflash,format=raw,readonly=on,file={ovmf_path}/edk2-aarch64-code.fd",
            f"-drive if=pflash,format=raw,file={env["home"]}/vm/edk2-arm-vars.fd",
        ],
    }
    params += uefi_params.get(args.arch, [])
