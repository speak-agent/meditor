# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Sunrisepeak & mcpp-community
"""Drive the native Linux editor with X11 input against a synthetic workspace."""
import argparse
import ctypes as c
import ctypes.util
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


def wait_for(check, seconds=15):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.1)
    raise RuntimeError("Native GUI condition timed out")


class ClientMessage(c.Structure):
    _fields_ = [("type", c.c_int), ("serial", c.c_ulong), ("sent", c.c_int),
                ("display", c.c_void_p), ("window", c.c_ulong), ("message", c.c_ulong),
                ("format", c.c_int), ("data", c.c_long * 5)]


class Event(c.Union):
    _fields_ = [("client", ClientMessage), ("padding", c.c_long * 24)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--screenshot", type=Path)
    parser.add_argument("--language", choices=["en", "zh"], default="en")
    parser.add_argument("--syntax", choices=["cpp", "markdown", "json", "toml"], default="cpp")
    parser.add_argument("--theme", choices=["dark", "light"], default="dark")
    parser.add_argument("--find", action="store_true")
    parser.add_argument("--bom", action="store_true")
    parser.add_argument("--crlf", action="store_true")
    args = parser.parse_args()
    if args.binary:
        binary = args.binary.resolve()
    else:
        candidates = [p for p in Path("target").glob("**/bin/meditor-gui/meditor-gui") if p.is_file() and "dist" not in p.parts and "candidates" not in p.parts]
        if not candidates:
            raise SystemExit("Missing native GUI target")
        binary = max(candidates, key=lambda p: p.stat().st_mtime).resolve()
    project_root = Path(__file__).resolve().parents[1]
    for license_name in ["LICENSE", "LICENSING.md"]:
        installed = binary.parent / "licenses/meditor" / license_name
        if not installed.is_file() or installed.read_bytes() != (project_root / license_name).read_bytes():
            raise RuntimeError("Native GUI project license deployment is missing or changed: " + license_name)
    for name in ["HuxerUI-LICENSE.txt", "nlohmann-json-LICENSE.txt", "manifest.json"]:
        installed = binary.parent / "licenses/dependencies" / name
        if not installed.is_file() or installed.read_bytes() != (project_root / "resources/licenses" / name).read_bytes():
            raise RuntimeError("Native GUI direct dependency notice deployment is missing or changed: " + name)
    def library(name):
        soname = ctypes.util.find_library(name)
        if not soname:
            raise RuntimeError("Required X11 test library is unavailable")
        try:
            return c.CDLL(soname)
        except OSError:
            for base in [Path("/usr/lib"), Path("/lib")]:
                for candidate in base.glob("*/" + soname):
                    try:
                        return c.CDLL(str(candidate))
                    except OSError:
                        continue
        raise RuntimeError("Required X11 test library could not be loaded")
    x, xt = library("X11"), library("Xtst")
    signatures = {
        "XOpenDisplay": ([c.c_char_p], c.c_void_p),
        "XDefaultRootWindow": ([c.c_void_p], c.c_ulong),
        "XSetInputFocus": ([c.c_void_p, c.c_ulong, c.c_int, c.c_ulong], c.c_int),
        "XGetInputFocus": ([c.c_void_p, c.POINTER(c.c_ulong), c.POINTER(c.c_int)], c.c_int),
        "XRaiseWindow": ([c.c_void_p, c.c_ulong], c.c_int),
        "XFlush": ([c.c_void_p], c.c_int),
        "XSync": ([c.c_void_p, c.c_int], c.c_int),
        "XStringToKeysym": ([c.c_char_p], c.c_ulong),
        "XKeysymToKeycode": ([c.c_void_p, c.c_ulong], c.c_uint),
        "XInternAtom": ([c.c_void_p, c.c_char_p, c.c_int], c.c_ulong),
        "XSendEvent": ([c.c_void_p, c.c_ulong, c.c_int, c.c_long, c.POINTER(Event)], c.c_int),
        "XTranslateCoordinates": ([c.c_void_p, c.c_ulong, c.c_ulong, c.c_int, c.c_int, c.POINTER(c.c_int), c.POINTER(c.c_int), c.POINTER(c.c_ulong)], c.c_int),
        "XCloseDisplay": ([c.c_void_p], c.c_int),
    }
    for name, (parameters, result) in signatures.items():
        function = getattr(x, name); function.argtypes = parameters; function.restype = result
    xt.XTestFakeKeyEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
    xt.XTestFakeButtonEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
    xt.XTestFakeMotionEvent.argtypes = [c.c_void_p, c.c_int, c.c_int, c.c_int, c.c_ulong]
    display = x.XOpenDisplay(None)
    if not display:
        raise SystemExit("An X11 display is required")
    with tempfile.TemporaryDirectory(prefix="meditor-gui-test-") as folder:
        root = Path(folder)
        samples = {
            "cpp": ("main.cppm", "export module demo;\nexport int value() { return 42; }\n"),
            "markdown": ("README.md", "# Modern Modules\n\n- **Build** your first module with `import std`.\n- [Local guide](docs/guide.md)\n\n```cpp\nexport module hello;\nexport int answer() { return 42; }\n```\n"),
            "json": ("example.json", '{\n  "message": "Hello Modules",\n  "active": true,\n  "count": 42\n}\n'),
            "toml": ("mcpp.toml", '[package]\nname = "hello-modules"\nversion = "0.1.0"\n\n# Modules project\n[dependencies]\njson = "3.12.0"\n'),
        }
        filename, original = samples[args.syntax]
        if args.crlf:
            original = original.replace("\n", "\r\n")
        if args.bom:
            original = "\ufeff" + original
        replacement = ("\ufeff" if args.bom else "") + "x"
        source = root / filename
        source.write_bytes(original.encode())
        (root / "settings.json").write_text(json.dumps({"schemaVersion": 1, "revision": 1, "values": {"theme": args.theme, "reducedMotion": False, "language": args.language}}), encoding="utf-8")
        environment = os.environ.copy(); environment.update(GDK_BACKEND="x11", GSK_RENDERER="cairo")
        process = subprocess.Popen([str(binary), "--workspace", str(root), "--open", filename, "--settings-file", str(root / "settings.json"), "--data-root", str(root / "runtime")], env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        window = None
        pressed = set()
        try:
            def find_window():
                if process.poll() is not None:
                    raise RuntimeError("Native GUI exited before creating its window")
                tree = subprocess.check_output(["xwininfo", "-root", "-tree"], text=True)
                for identifier in re.findall(r'(0x[0-9a-fA-F]+) "meditor":', tree):
                    owner = subprocess.check_output(["xprop", "-id", identifier, "_NET_WM_PID"], text=True)
                    if re.search(rf"= {process.pid}\s*$", owner):
                        return int(identifier, 16)
                return None
            window = wait_for(find_window)
            time.sleep(1)
            if args.screenshot:
                args.screenshot.parent.mkdir(parents=True, exist_ok=True)
                subprocess.run(["import", "-window", hex(window), str(args.screenshot)], check=True, timeout=10)
            x.XRaiseWindow(display, window); x.XSetInputFocus(display, window, 2, 0)
            px, py, child = c.c_int(), c.c_int(), c.c_ulong()
            x.XTranslateCoordinates(display, window, x.XDefaultRootWindow(display), 500, 260, c.byref(px), c.byref(py), c.byref(child))
            xt.XTestFakeMotionEvent(display, -1, px.value, py.value, 0)
            xt.XTestFakeButtonEvent(display, 1, 1, 0); xt.XTestFakeButtonEvent(display, 1, 0, 0)
            x.XSync(display, 0); time.sleep(0.2)

            def require_focus():
                focused, revert = c.c_ulong(), c.c_int()
                x.XGetInputFocus(display, c.byref(focused), c.byref(revert))
                if focused.value != window:
                    raise RuntimeError("Native GUI lost keyboard focus; input injection stopped")

            def key(name, down):
                require_focus()
                code = x.XKeysymToKeycode(display, x.XStringToKeysym(name.encode()))
                if not code:
                    raise RuntimeError("Required keyboard mapping is missing")
                xt.XTestFakeKeyEvent(display, code, down, 0)
                if down:
                    pressed.add(code)
                else:
                    pressed.discard(code)

            def chord(name):
                key("Control_L", 1); key(name, 1); key(name, 0); key("Control_L", 0)
                x.XSync(display, 0); time.sleep(0.15)

            chord("a"); key("x", 1); key("x", 0); x.XSync(display, 0)
            chord("s")
            wait_for(lambda: source.read_bytes() == replacement.encode())
            chord("z"); chord("s")
            wait_for(lambda: source.read_bytes() == original.encode())
            chord("y"); chord("s")
            wait_for(lambda: source.read_bytes() == replacement.encode())
            chord("z"); chord("s")
            wait_for(lambda: source.read_bytes() == original.encode())
            if args.crlf:
                chord("a"); key("x", 1); key("x", 0); key("Return", 1); key("Return", 0); x.XSync(display, 0)
                chord("s"); wait_for(lambda: source.read_bytes() == (replacement + "\r\n").encode())
                chord("z"); chord("s"); wait_for(lambda: source.read_bytes() == replacement.encode())
                chord("z"); chord("s"); wait_for(lambda: source.read_bytes() == original.encode())
            if args.find:
                if args.syntax != "cpp":
                    raise RuntimeError("The native find scenario uses the C++ sample")
                chord("f"); time.sleep(.3)
                for character in "value":
                    key(character, 1); key(character, 0)
                x.XSync(display, 0); time.sleep(.3)
                if args.screenshot:
                    find_image = args.screenshot.with_name(args.screenshot.stem + "-find.png")
                    subprocess.run(["import", "-window", hex(window), str(find_image)], check=True, timeout=10)
                key("Tab", 1); key("Tab", 0); x.XSync(display, 0); time.sleep(.15)
                for character in "answer":
                    key(character, 1); key(character, 0)
                for _ in range(4):
                    key("Tab", 1); key("Tab", 0)
                key("Return", 1); key("Return", 0); x.XSync(display, 0); time.sleep(.3)
                if source.read_bytes() != original.encode():
                    raise RuntimeError("Find/replace unexpectedly saved the document")
                key("Escape", 1); key("Escape", 0); x.XSync(display, 0); time.sleep(.4)
                chord("s"); wait_for(lambda: source.read_bytes() == original.replace("value", "answer").encode())
                chord("z"); chord("s"); wait_for(lambda: source.read_bytes() == original.encode())
            if args.screenshot:
                args.screenshot.parent.mkdir(parents=True, exist_ok=True)
                subprocess.run(["import", "-window", hex(window), str(args.screenshot)], check=True, timeout=10)
            event = Event(); event.client.type = 33; event.client.display = display; event.client.window = window
            event.client.message = x.XInternAtom(display, b"WM_PROTOCOLS", 0); event.client.format = 32
            event.client.data[0] = x.XInternAtom(display, b"WM_DELETE_WINDOW", 0)
            x.XSendEvent(display, window, 0, 0, c.byref(event)); x.XFlush(display)
            process.wait(timeout=10)
            if process.returncode != 0:
                raise RuntimeError("Native GUI did not exit cleanly")
        except Exception:
            if args.screenshot and window and process.poll() is None:
                failure_image = args.screenshot.with_name(args.screenshot.stem + "-failure.png")
                subprocess.run(["import", "-window", hex(window), str(failure_image)], capture_output=True, timeout=10)
            raise
        finally:
            for code in pressed:
                xt.XTestFakeKeyEvent(display, code, 0, 0)
            x.XSync(display, 0)
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
            process.stderr.close()
            x.XCloseDisplay(display)
    print("Native GUI keyboard edit, save, undo, redo and close: passed")


if __name__ == "__main__":
    main()
