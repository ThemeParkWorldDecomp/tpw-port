#!/usr/bin/env python3
"""Which icon.png the launcher shows (launcher/app_icon.h), through --write-icon, which names it: the run's own data
folder's, else the per-user data folder's (launcher.ini's data_folder first), else the stand-in. Every case runs in a
fake home, so the player's own folder is never read. usage: icon_choice_test.py <tpw_launcher>"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

FOLDER = "STP-TPW Launcher"


def png(source):
    """A 2 x 2 8-bit RGBA PNG whose Source text is `source` ", 2x2, 8-bit" (assets/icon.h, icon.png)."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = (b"\0" + b"\xff\x80\x40\xff" * 2) * 2
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 0)) +
            chunk(b"tEXt", b"Source\0" + source.encode() + b", 2x2, 8-bit") + chunk(b"IDAT", zlib.compress(rows)) +
            chunk(b"IEND", b""))


def put(folder, source):
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, "icon.png"), "wb") as f:
        f.write(png(source))


def per_user(home):
    if sys.platform == "darwin":
        return os.path.join(home, "Library", "Application Support", FOLDER)
    if sys.platform == "win32":
        return os.path.join(home, "AppData", FOLDER)
    return os.path.join(home, ".local", "share", FOLDER)


def shows(launcher, home, data):
    """What --write-icon says it wrote, for the fake home and the --data folder `data` (None: no --data)."""
    env = dict(os.environ, HOME=home, APPDATA=os.path.join(home, "AppData"))
    env.pop("XDG_DATA_HOME", None)
    out = os.path.join(home, "out.png")
    args = [launcher] + (["--data", data] if data else []) + ["--write-icon", out]
    run = subprocess.run(args, env=env, cwd=home, capture_output=True, text=True, timeout=60)
    if run.returncode != 0 or not os.path.getsize(out):
        sys.exit(f"{args} failed, exit {run.returncode}: {run.stdout}{run.stderr}")
    return run.stdout


def main():
    launcher = os.path.abspath(sys.argv[1])
    failed = 0
    def check(name, home, data, want):
        nonlocal failed
        said = shows(launcher, home, data)
        ok = want in said
        print(("ok   " if ok else "FAIL ") + name + ": " + said.strip())
        failed += 0 if ok else 1
    with tempfile.TemporaryDirectory() as top:
        data = os.path.join(top, "run-data")
        os.makedirs(data)
        home = os.path.join(top, "home-empty")
        os.makedirs(home)
        check("nothing imported anywhere", home, data, "stand-in")
        check("no --data, nothing imported", home, None, "stand-in")
        home = os.path.join(top, "home-own")
        put(per_user(home), "PerUser.ico")
        check("per-user folder only", home, data, "PerUser.ico")
        check("no --data", home, None, "PerUser.ico")
        put(os.path.join(top, "run-data-own"), "RunData.ico")
        check("the run's own wins", home, os.path.join(top, "run-data-own"), "RunData.ico")
        home = os.path.join(top, "home-moved")
        put(per_user(home), "OldPlace.ico")
        put(os.path.join(top, "moved"), "Moved.ico")
        with open(os.path.join(per_user(home), "launcher.ini"), "w") as f:
            f.write("data_folder=" + os.path.join(top, "moved").replace("\\", "/") + "\n")
        check("launcher.ini's data folder first", home, data, "Moved.ico")
        os.remove(os.path.join(top, "moved", "icon.png"))
        check("then the per-user folder's own", home, data, "OldPlace.ico")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
