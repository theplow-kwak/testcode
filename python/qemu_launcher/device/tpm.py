# qemu_launcher/device/tpm.py

from pathlib import Path


def configure_tpm(env, params):
    cancel_path = f"/tmp/foo-cancel-{env['uid']}"
    Path(cancel_path).touch(exist_ok=True)
    params += [f"-tpmdev passthrough,id=tpm0,path=/dev/tpm0,cancel-path={cancel_path}", "-device tpm-tis,tpmdev=tpm0"]
