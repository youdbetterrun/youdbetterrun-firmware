from pathlib import Path
import subprocess

import requests

Import("env")


def generate_cert(source, target, env):
    _ = source
    _ = target
    _ = env
    certs = Path() / "src/certs.h"
    script = Path() / ".pio/cert.py"
    URL = "https://raw.githubusercontent.com/esp8266/Arduino/refs/heads/master/tools/cert.py"

    if not script.exists():
        response = requests.get(URL)

        with open(script, "w") as f:
            f.write(response.text)

    with open(certs, "w") as f:
        f.write(subprocess.run([
            "python",
            str(script),
            "-s",
            "fahrtauskunft.avv-augsburg.de",
        ], stdout=subprocess.PIPE).stdout.decode("utf-8"))


env.AddPreAction("$BUILD_DIR/src/main.cpp.o", generate_cert)
