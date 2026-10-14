#!/usr/bin/env python3
"""Copy a module binary and its non-system shared libraries into one directory.

Linux loads the copies through LD_LIBRARY_PATH. macOS loads them through
@loader_path, and absolute Homebrew paths are rewritten to @rpath.
"""

import os
import shutil
import subprocess
import sys

LINUX_SKIP = {
    "linux-vdso.so.1",
    "ld-linux-x86-64.so.2",
    "ld-linux-aarch64.so.1",
    "libc.so.6",
    "libm.so.6",
    "libpthread.so.0",
    "libdl.so.2",
    "librt.so.1",
    "libresolv.so.2",
    "libstdc++.so.6",
    "libgcc_s.so.1",
}


def run(cmd):
    return subprocess.check_output(cmd, text=True, stderr=subprocess.DEVNULL)


def index_libraries(root):
    found = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [name for name in dirnames if name != "pack"]
        for filename in filenames:
            if filename.endswith(".dylib") or ".so" in filename:
                found.setdefault(filename, os.path.join(dirpath, filename))
    return found


def bundle_linux(binary, dest):
    queued = [binary]
    seen = {binary}

    while queued:
        current = queued.pop()
        for line in run(["ldd", current]).splitlines():
            if "=>" not in line:
                continue
            soname, rest = line.split("=>", 1)
            soname = soname.strip()
            rest = rest.strip()
            if soname in LINUX_SKIP or rest.startswith("not found"):
                continue
            source = rest.split()[0]
            if not source.startswith("/"):
                continue
            target = os.path.join(dest, soname)
            if soname in seen:
                continue
            os.makedirs(dest, exist_ok=True)
            shutil.copy2(source, target)
            seen.add(soname)
            queued.append(target)


def darwin_id(path):
    lines = run(["otool", "-D", path]).splitlines()
    if len(lines) >= 2 and lines[1].strip():
        return lines[1].strip()
    return None


def darwin_deps(path):
    own_id = darwin_id(path)
    deps = []
    for line in run(["otool", "-L", path]).splitlines()[1:]:
        dep = line.strip().split()[0]
        if dep != own_id:
            deps.append(dep)
    return deps


def is_darwin_system(path):
    return path.startswith("/usr/lib/") or path.startswith("/System/")


def bundle_darwin(binary, dest, search_root):
    library_index = index_libraries(search_root)
    homebrew_lib = "/opt/homebrew/lib"
    if os.path.isdir(homebrew_lib):
        library_index.update(index_libraries(homebrew_lib))
    queued = [binary]
    processed = set()

    while queued:
        current = queued.pop()
        if current in processed:
            continue
        processed.add(current)
        for dep in darwin_deps(current):
            if is_darwin_system(dep):
                continue
            filename = os.path.basename(dep)
            target = os.path.join(dest, filename)
            if os.path.exists(target):
                source = target
            elif dep.startswith("/") and os.path.exists(dep):
                source = dep
            else:
                source = library_index.get(filename)
            if source is None:
                raise SystemExit(f"could not resolve {dep} required by {current}")
            if not os.path.exists(target):
                shutil.copy2(os.path.realpath(source), target)
                queued.append(target)
            if dep != f"@rpath/{filename}":
                subprocess.check_call(
                    ["install_name_tool", "-change", dep, f"@rpath/{filename}", current]
                )

    for path in processed:
        ident = darwin_id(path)
        filename = os.path.basename(path)
        if ident and ident != f"@rpath/{filename}":
            subprocess.check_call(["install_name_tool", "-id", f"@rpath/{filename}", path])
        rpaths = run(["otool", "-l", path])
        if "@loader_path" not in rpaths:
            subprocess.check_call(["install_name_tool", "-add_rpath", "@loader_path", path])
        subprocess.check_call(["codesign", "-s", "-", "-f", path])


def main():
    if len(sys.argv) != 4:
        raise SystemExit(f"usage: {sys.argv[0]} BINARY DEST SEARCH_ROOT")
    binary, dest, search_root = sys.argv[1:]
    os.makedirs(dest, exist_ok=True)
    packed = os.path.join(dest, os.path.basename(binary))
    shutil.copy2(binary, packed)
    if sys.platform == "darwin":
        bundle_darwin(packed, dest, search_root)
    elif sys.platform.startswith("linux"):
        bundle_linux(packed, dest)
    else:
        raise SystemExit(f"unsupported platform {sys.platform}")


if __name__ == "__main__":
    main()
