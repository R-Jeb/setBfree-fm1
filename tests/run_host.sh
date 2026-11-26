#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run firmware/src/dinghy.c on this computer (tests/host.c): build/host/dinghy.wav and the screens as PNG.
#   tests/run_host.sh [script]                 the FM-1
#   BOARD=smk37 tests/run_host.sh [script]     the SMK-37 (pads, faders, wheels, 37 keys): build/host/smk37*, the pads under the screen
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
N=dinghy; D=
if [ "${BOARD:-fm1}" = smk37 ]; then N=smk37; D=-DBOARD_SMK37=1; fi
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST $D -DAPP_VER="\"${APP_VER:-dev}\"" -Ifirmware/src -Ifirmware/gen -o build/host/$N firmware/src/dinghy.c
build/host/$N build/host/$N "$@"
N=$N python3 - <<'PY'
import os
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/host").glob(os.environ["N"] + "-*.ppm")):
    im = Image.open(p); im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
    print("screen ->", p.with_suffix(".png"))
PY
