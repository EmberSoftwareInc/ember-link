#!/usr/bin/env python3
"""Run production C validation/storage code on the host, with ASan and UBSan."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "firmware/main"
TEST = ROOT / "tests/native"
CJSON = ROOT / "firmware/managed_components/espressif__cjson/cJSON"


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def main():
    if not (CJSON / "cJSON.c").exists():
        raise SystemExit("Run idf.py -C firmware reconfigure to resolve cJSON first.")
    cc = os.environ.get("CC", "cc")
    flags = ["-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-g",
             "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", MAIN, "-I", TEST]
    crypto = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "openssl"], text=True))
    with tempfile.TemporaryDirectory(prefix="ember-link-tests-") as temp:
        tmp = Path(temp)
        run(cc, *flags, "-Wno-deprecated-declarations", "-I", CJSON, "-c", CJSON / "cJSON.c", "-o", tmp / "cjson.o")
        run(cc, *flags, "-I", CJSON, MAIN / "link_protocol.c", MAIN / "cloud_protocol.c",
            tmp / "cjson.o", TEST / "test_protocol.c", "-lm", "-o", tmp / "protocol")
        run(tmp / "protocol")
        run(cc, *flags, MAIN / "usb_mode_state.c", TEST / "test_usb_mode.c",
            "-o", tmp / "usb_mode")
        run(tmp / "usb_mode")
        run(cc, *flags, MAIN / "display_state.c", MAIN / "display_render.c", TEST / "test_display.c", "-o", tmp / "display")
        run(tmp / "display")
        run(cc, *flags, "-I", CJSON, MAIN / "display_settings.c", MAIN / "display_cloud.c",
            MAIN / "link_protocol.c", MAIN / "cloud_protocol.c", tmp / "cjson.o",
            TEST / "test_display_settings.c", "-lm", "-o", tmp / "display_settings")
        run(tmp / "display_settings")
        run(cc, *flags, MAIN / "led.c", TEST / "test_led.c", "-o", tmp / "led")
        run(tmp / "led")
        includes = [v for v in crypto if v.startswith("-I")]
        run(cc, *flags, *includes, "-include", TEST / "file_faults.h", "-c", MAIN / "link_files.c", "-o", tmp / "files.o")
        run(cc, *flags, tmp / "files.o", MAIN / "link_protocol.c", TEST / "test_files.c",
            *crypto, "-lm", "-o", tmp / "files")
        run(tmp / "files")
        run(cc, *flags, "-I", CJSON, "-include", TEST / "local_file_faults.h", "-c", MAIN / "local_files.c", "-o", tmp / "local_files.o")
        run(cc, *flags, "-I", CJSON, tmp / "local_files.o", tmp / "cjson.o",
            TEST / "test_local_files.c", "-o", tmp / "local_files")
        run(tmp / "local_files")
        run(cc, *flags, "-I", CJSON, MAIN / "link_protocol.c", MAIN / "cloud_protocol.c",
            MAIN / "display_settings.c", MAIN / "display_cloud.c", tmp / "cjson.o", TEST / "test_cloud.c", *crypto, "-lm", "-o", tmp / "cloud")
        run(tmp / "cloud")
        run(cc, *flags, "-I", CJSON, MAIN / "link_protocol.c", MAIN / "cloud_protocol.c",
            tmp / "cjson.o", TEST / "test_updates.c", TEST / "display_stubs.c", *crypto, "-lm", "-o", tmp / "updates")
        run(tmp / "updates")


if __name__ == "__main__":
    main()
