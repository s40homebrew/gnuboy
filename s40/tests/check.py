#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the maintained core and port using generated ROMs, with no game assets."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sdk = Path(os.environ.get("S40_SDK_ROOT", root.parent / "main"))
sources = [root / (name + ".c") for name in
           ("cpu", "mem", "lcd", "lcdc", "refresh", "palette", "hw", "sound", "rtc", "debug")]
sources += [root / "s40/native" / (name + ".c") for name in ("cartridge", "emulation", "state")]
sources += [root / "s40/tests/test_port.c"]
with tempfile.TemporaryDirectory(prefix="gnuboy-test-") as temporary:
    executable = Path(temporary) / "port-test"
    # The legacy core deliberately uses biased address-map pointers. ASan
    # checks actual accesses; UBSan also covers the new loader/state code.
    objects = []
    for source in sources:
        obj = Path(temporary) / (source.stem + ".o")
        sanitizers = "address,undefined" if "s40" in source.parts else "address"
        subprocess.run([os.environ.get("HOST_CC", "cc"), "-std=gnu11", "-O1", "-g", "-fwrapv", "-fno-strict-aliasing",
                        "-Wno-deprecated-declarations", "-fsanitize=" + sanitizers,
                        "-DIS_LITTLE_ENDIAN", "-DGNUBOY_S40", "-I" + str(root), "-I" + str(root / "s40/native"),
                        "-c", str(source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    subprocess.run([os.environ.get("HOST_CC", "cc"), "-fsanitize=address,undefined", *objects, "-o", str(executable)], check=True)
    subprocess.run([str(executable), *(str(Path(p).resolve()) for p in sys.argv[1:])], cwd=temporary,
                   check=True, env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
    audio_test = Path(temporary) / "audio-test"
    subprocess.run([os.environ.get("HOST_CC", "cc"), "-std=gnu11", "-O1", "-g", "-fsanitize=address,undefined",
                    "-I" + str(root), "-I" + str(root / "s40/native"),
                    "-I" + str(sdk / "homebrew/include"), str(root / "s40/native/audio_output.c"),
                    str(root / "s40/tests/test_audio.c"), "-o", str(audio_test)], check=True)
    subprocess.run([str(audio_test)], cwd=temporary, check=True,
                   env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
