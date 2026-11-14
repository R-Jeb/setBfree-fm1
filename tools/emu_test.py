#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""EXPERIMENT, not working yet: the firmware runs on real FM-1s (old and new flash chip); in this emulator it does not get going
(the second core is probably not emulated the way the firmware starts it). Nothing depends on this script.

Plays a scripted test on the firmware in Joakim Eriksson's FM-1 emulator (fm1-live, headless) and writes the sound as a WAV and the
last screen as a PNG, so a change can be heard and measured before it goes onto an FM-1.

  python3 tools/emu_test.py [--fwsc build/b3organ.fwsc] [--out build/emu] SCENARIO [--strict]

  chord    seven notes one after the other (F4 A4 C5 E5 F5 A5 C6), four drawbars out, chorus C3, Leslie stop, overdrive off, expression 100 %
  chordod  the same with the overdrive on
  leslie   the chord F5 A5 C6 E6: Leslie stop, then slow, then fast (overdrive off, no vibrato)
  keys     four of the FM-1's own keys, no MIDI: a check that the sound comes at all

It needs no MIDI tools and no sound card: the emulator runs headless and the MIDI comes from the script. The first run downloads fm1-live
(Linux, x86-64) from the emulator's releases into ~/.fm1-emu. --strict counts the cycles of both cores as an FM-1 does (slow, no JIT): only
then the times on the TEST screen mean something. The encoder numbers of the script (e2 = KNOB 1) are the ones of the firmware's HAL.
"""
import argparse, json, os, pathlib, platform, stat, struct, subprocess, sys, urllib.request, wave, zlib

HOME = pathlib.Path.home() / ".fm1-emu"
API = "https://api.github.com/repos/joakimeriksson/fm1-emu/releases/latest"


def get_emulator():
    exe = HOME / "fm1-live"
    if exe.exists():
        return str(exe)
    HOME.mkdir(parents=True, exist_ok=True)
    rel = json.load(urllib.request.urlopen(API))
    arch = {"x86_64": ("x86_64", "x86-64", "amd64"), "aarch64": ("aarch64", "arm64")}.get(platform.machine(), ())
    pick = [a for a in rel["assets"] if "linux" in a["name"].lower() and any(t in a["name"].lower() for t in arch)]
    if not pick:
        sys.exit("no Linux fm1-live in the latest release (%s): download it from github.com/joakimeriksson/fm1-emu/releases into %s" %
                 (", ".join(a["name"] for a in rel["assets"]), HOME))
    name = pick[0]["name"]
    print("downloading", name)
    data = urllib.request.urlopen(pick[0]["browser_download_url"]).read()
    path = HOME / name
    path.write_bytes(data)
    if name.endswith((".tar.gz", ".tgz", ".tar.xz", ".zip")):
        subprocess.check_call(["tar", "-xf", str(path), "-C", str(HOME)] if not name.endswith(".zip") else ["unzip", "-o", str(path), "-d", str(HOME)])
        found = [p for p in HOME.rglob("fm1-live") if p.is_file()]
        if not found:
            sys.exit("fm1-live not found in " + name)
        found[0].rename(exe)
    else:
        path.rename(exe)
    exe.chmod(exe.stat().st_mode | stat.S_IXUSR)
    return str(exe)


def midi(t, *b):                                  # bytes into the DIN jack at t ms
    return "%d:m%s" % (t, "".join("%02x" % x for x in b))


def cc(t, n, v):
    return midi(t, 0xB0, n, v)


def setup(od=False, chorus=False, leslie=None):
    """The registration as MIDI controllers (setBfree's numbers; the drawbars are inverted: 127 = 0, 0 = 8)."""
    s = []
    for i, d in enumerate((8, 8, 8, 8, 0, 0, 0, 0, 0)):
        s.append(cc(2000 + 5 * i, 70 + i, 127 - round(d * 127 / 8)))
    s += [cc(2100, 66, 0),                        # percussion off
          cc(2105, 65, 127 if od else 0),         # overdrive
          cc(2110, 11, 127),                      # expression 100 %
          cc(2115, 91, 4),                        # reverb 4
          cc(2120, 30, 0), cc(2125, 31, 127 if chorus else 0), cc(2130, 92, 115)]   # vibrato on the upper manual, type C3
    s.append(cc(2135, 1, {"stop": 64, "slow": 20, "fast": 120}[leslie or "stop"]))
    return s


def notes(t0, pitches, gap):
    return [midi(t0 + i * gap, 0x90, p, 100) for i, p in enumerate(pitches)], t0 + (len(pitches) - 1) * gap


def scenario(name):
    """(events, ms to run). The notes stay on until the end, so that the last screen (the TEST screen, KNOB 1 turned three clicks at 2.3 s)
    shows the values of the playing."""
    screen = ["1500:e2:3", "2500:e2:3"]   # the clamp at screen 4 makes the second one harmless
    if name in ("chord", "chordod"):
        n, last = notes(3000, [65, 69, 72, 76, 77, 81, 84], 400)
        return setup(od=name == "chordod", chorus=True) + screen + n, last + 2000
    if name == "leslie":
        n, last = notes(3000, [77, 81, 84, 88], 0)
        return setup(leslie="stop") + screen + n + [cc(7000, 1, 20), cc(11000, 1, 120)], 14500   # slow after 4 s, fast after 8 s
    if name == "keys":                          # the FM-1's own keys, no MIDI: does the sound path work at all?
        return screen + ["3000:C4@100", "3300:E4@100", "3600:G4@100", "3900:C5@100"], 6000
    sys.exit("scenarios: chord, chordod, leslie, keys")


def png(path, w, h, rgb):
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    pathlib.Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                                   chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def ppm_to_png(src, dst):
    d = pathlib.Path(src).read_bytes()
    parts, pos = [], 0
    while len(parts) < 4:                         # P6 w h max
        while d[pos:pos + 1].isspace():
            pos += 1
        if d[pos:pos + 1] == b"#":
            pos = d.index(b"\n", pos)
            continue
        e = pos
        while not d[e:e + 1].isspace():
            e += 1
        parts.append(d[pos:e]); pos = e
    pos += 1
    w, h = int(parts[1]), int(parts[2])
    png(dst, w, h, d[pos:pos + w * h * 3])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario")
    ap.add_argument("--fwsc", default="build/b3organ.fwsc")
    ap.add_argument("--out", default="build/emu")
    ap.add_argument("--strict", action="store_true")
    a = ap.parse_args()
    keys, ms = scenario(a.scenario)
    out = pathlib.Path(a.out); out.mkdir(parents=True, exist_ok=True)
    raw, ppm = out / (a.scenario + ".raw"), out / (a.scenario + ".ppm")
    env = dict(os.environ, FM1_BENCH_KEYS=",".join(keys), FM1_BENCH_WAV=str(raw), FM1_BENCH_PANEL=str(ppm), FM1_BENCH_NOKEYS="1")
    if a.strict:
        env["FM1_STRICT"] = "1"
    cmd = [get_emulator(), "--fwsc", a.fwsc, "--midi", "none", "--bench", str(ms)]
    print(" ".join(cmd)); print("script: %d events, %d ms" % (len(keys), ms))
    subprocess.check_call(cmd, env=env)
    data = raw.read_bytes() if raw.exists() else b""
    n = len(data) // 2
    pk = max([abs(x) for x in struct.unpack("<%dh" % n, data[:n * 2])] or [0])
    print("audio: %.1f s, peak %d of 32767 (%s)" % (n / 2 / 44100.0, pk, "SILENT" if pk == 0 else "ok"))
    with wave.open(str(out / (a.scenario + ".wav")), "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100); w.writeframes(data)
    if ppm.exists():
        ppm_to_png(ppm, out / (a.scenario + ".png"))
    print("written:", out / (a.scenario + ".wav"), out / (a.scenario + ".png"))


main()
