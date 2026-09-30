#!/usr/bin/env python3
"""Exercise the production formatter and real ESP-IDF FatFs on sparse test media."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
IDF = Path(os.environ["IDF_PATH"])
SRC = IDF / "components/fatfs/src"
TEST = ROOT / "tests/native/card_fs"
with tempfile.TemporaryDirectory(prefix="ember-card-tests-") as temp:
    binary = Path(temp) / "card"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-D_DEFAULT_SOURCE",
                    "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I", str(TEST), "-I", str(ROOT / "tests/native"), "-I", str(SRC),
                    "-I", str(ROOT / "firmware/main"),
                    str(ROOT / "firmware/main/card_fs.c"),
                    str(ROOT / "firmware/main/card_layout.c"), str(SRC / "ff.c"),
                    str(TEST / "test_card_fs.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
