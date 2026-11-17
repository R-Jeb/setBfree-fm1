#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The site's installer (docs/index.html) for a new release: copies the package to docs/firmware/ and sets the page's META
(version, file, identity, SHA-256), so that GitHub Pages offers it after the merge.

  python3 tools/make_site.py 0.6 path/to/b3organ-0.6.fwsc
"""
import hashlib, json, re, shutil, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def product_of(raw):                                  # the identity: one marker byte after each of the first 20 blocks
    return "".join(chr((raw[i * 0x30 + 0x2F] - i - 1) & 0xFF) for i in range(20) if raw[i * 0x30 + 0x2F] != 0x7D)


def main(version, pkg):
    raw = Path(pkg).read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d\d", product) or b"FELUCCA-LOADER-1" not in raw:
        sys.exit(f"{pkg}: not a B3 ORGAN package (identity {product!r})")
    name = f"b3organ-{version}.fwsc"
    fw = ROOT / "docs/firmware"
    fw.mkdir(exist_ok=True)
    for old in fw.glob("b3organ-*.fwsc"):             # one package on the site: the newest
        old.unlink()
    shutil.copyfile(pkg, fw / name)
    meta = {"version": version, "pkg": f"firmware/{name}", "product": product, "sha256": hashlib.sha256(raw).hexdigest()}
    page = ROOT / "docs/index.html"
    s = page.read_text()
    s, n = re.subn(r"/\*META\*/.*?/\*/META\*/", "/*META*/" + json.dumps(meta) + "/*/META*/", s, flags=re.S)
    if n != 1:
        sys.exit("docs/index.html: no META")
    s = re.sub(r'(<span class="chip" id="ver">)[^<]*(</span>)', r"\g<1>" + version + r"\2", s)
    page.write_text(s)
    print(f"docs/firmware/{name}, {product}, sha256 {meta['sha256'][:16]}...")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
