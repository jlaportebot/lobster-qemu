#!/usr/bin/env python3
"""Copy non-system dylibs next to the QEMU binaries and rewrite install names.

usage: bundle-macos.py <outdir>   (outdir/bin holds the executables)
"""
import glob
import os
import shutil
import subprocess
import sys

out = os.path.abspath(sys.argv[1])
bindir, libdir = os.path.join(out, "bin"), os.path.join(out, "lib")
os.makedirs(libdir, exist_ok=True)
brew = subprocess.check_output(["brew", "--prefix"], text=True).strip()
search = [os.path.join(brew, "lib")] + glob.glob(os.path.join(brew, "opt", "*", "lib"))


def run(*cmd):
    subprocess.check_call(cmd)


def is_system(path):
    return path.startswith(("/usr/lib/", "/System/"))


def deps(path):
    lines = subprocess.check_output(["otool", "-L", path], text=True).splitlines()[1:]
    me = os.path.basename(path)
    result = []
    for line in lines:
        dep = line.strip().split(" (")[0]
        if os.path.basename(dep) != me and not is_system(dep):
            result.append(dep)
    return result


def resolve(dep):
    if dep.startswith("/") and os.path.exists(dep):
        return os.path.realpath(dep)
    name = os.path.basename(dep)
    for d in search:
        cand = os.path.join(d, name)
        if os.path.exists(cand):
            return os.path.realpath(cand)
    sys.exit("cannot resolve " + dep)


queue = [os.path.join(bindir, f) for f in sorted(os.listdir(bindir))]
bundled = {}
while queue:
    f = queue.pop()
    is_exe = os.path.dirname(f) == bindir
    for dep in deps(f):
        name = os.path.basename(dep)
        if name not in bundled:
            dst = os.path.join(libdir, name)
            shutil.copy(resolve(dep), dst)
            os.chmod(dst, 0o755)
            run("install_name_tool", "-id", "@rpath/" + name, dst)
            run("install_name_tool", "-add_rpath", "@loader_path", dst)
            bundled[name] = dst
            queue.append(dst)
        run("install_name_tool", "-change", dep, "@rpath/" + name, f)
    if is_exe:
        run("install_name_tool", "-add_rpath", "@executable_path/../lib", f)

for f in glob.glob(os.path.join(out, "bin", "*")) + glob.glob(os.path.join(libdir, "*")):
    run("codesign", "--force", "--sign", "-", f)
print("bundled %d libraries" % len(bundled))
