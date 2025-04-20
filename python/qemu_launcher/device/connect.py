def configure_connection(args, env, opts, connect_info):
    procid = env["procid"]
    ssh_port = env["ssh_port"]
    hostip = env["hostip"]
    localip = env["localip"]

    if args.connect == "ssh":
        opts += ["-nographic", "-serial", "mon:stdio"]
        connect_info["method"] = "ssh"
        connect_info["target"] = f"{hostip} -p {ssh_port}" if args.net == "user" else localip
        connect_info["command"] = [f"ssh {args.uname}@{connect_info['target']}"]
    elif args.connect == "spice":
        opts += ["-monitor", "stdio"]
        connect_info["method"] = "spice"
        connect_info["command"] = [f"remote-viewer -t {procid} spice://{hostip}:{env['spice_port']}"]
    else:
        opts += ["-monitor", "stdio"]
        connect_info["method"] = "qemu"
        connect_info["command"] = None
