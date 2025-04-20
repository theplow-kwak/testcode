def configure_pci_passthrough(args, params):
    if args.pcihost:
        params.append(f"-device vfio-pci,host={args.pcihost},multifunction=on")
