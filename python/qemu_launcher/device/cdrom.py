def configure_cdrom(args, env, params):
    cdroms = env.get("cdroms", [])
    for i, iso in enumerate(cdroms):
        params += [f"-cdrom {iso}"]
        if i == 0:
            params += ["-boot d"]
