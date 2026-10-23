#!/bin/sh
# Builds web/organ.wasm from the firmware's organ engine. Needs clang with the wasm32 target and wasm-ld.
set -e
cd "$(dirname "$0")/.."
clang --target=wasm32 -O2 -ffreestanding -nostdlib -fno-builtin -Wall -Wno-unused-function \
  -Ifirmware/src -Wl,--no-entry -Wl,--export-dynamic -Wl,--allow-undefined -Wl,-z,stack-size=65536 \
  -o web/organ.wasm web/organ_wasm.c
ls -l web/organ.wasm
# the self-contained page: the wasm inlined as base64 (open web/index.html, or publish it as is, e.g. GitHub Pages)
python3 - <<'PY'
import base64, pathlib
t = pathlib.Path("web/index.template.html").read_text()
b = base64.b64encode(pathlib.Path("web/organ.wasm").read_bytes()).decode()
pathlib.Path("web/index.html").write_text(t.replace("/*WASM_B64*/", b))
print("web/index.html written")
PY
