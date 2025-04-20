def configure_spice(env, params):
    spice_port = env["spice_port"]
    params += [
        f"-spice port={spice_port},disable-ticketing=on",
        "-audiodev spice,id=audio0",
        "-device intel-hda",
        "-device hda-duplex,audiodev=audio0,mixer=off",
        "-chardev spicevmc,id=vdagent,name=vdagent",
        "-device virtio-serial",
        "-device virtserialport,chardev=vdagent,name=com.redhat.spice.0",
    ]
