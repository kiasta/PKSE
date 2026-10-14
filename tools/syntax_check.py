#!/usr/bin/env python3
"""Ground-truth syntax check using the real devkitA64 cross compiler.

IntelliSense is an approximation; only GCC decides whether the code actually compiles. This runs
``aarch64-none-elf-g++ -fsyntax-only`` with the same flags the Makefile uses -- no optimisation, no
codegen, no linking, no MSys2 shell -- so checking the whole project takes seconds instead of a
full build.

    python tools/syntax_check.py                     # every TU either build type compiles
    python tools/syntax_check.py src/UI/UI.cpp ...   # only these files
    python tools/syntax_check.py -j 4                # cap parallelism
    python tools/syntax_check.py --print-flags       # dump the flags (for IDE config)

Flags are derived from the Makefile (APP_VERSION, SOURCES, INCLUDES, ARCH, DEFINES) and from
portlibs' sdl2.pc, so this tracks the build instead of duplicating it. Exits non-zero if any file
fails, so it also works as a task/pre-commit gate.

Both build types are checked. Every TU is checked as the production build compiles it. The
development build's own sources (DEVELOPMENT_SOURCES) are checked with DEVELOPMENT_DEFINES, and so
is every production TU that mentions the gate (PKSE_DEVELOPMENT_BUILD / SELF_TEST_AVAILABLE),
because those compile differently in the two builds. Those rows are labelled [development].
"""

from __future__ import annotations

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MAKEFILE = REPO / "Makefile"

# The devkitPro installer points DEVKITPRO at the MSys2 mount (/opt/devkitpro), which only resolves
# inside an MSys2 shell. Probe for a path that actually exists on this side.
DEVKITPRO_CANDIDATES = ("C:/devkitPro", "/opt/devkitpro", "/usr/local/devkitpro")

# What Makefile SDL_CFLAGS contributes if sdl2.pc can't be read.
SDL_CFLAGS_FALLBACK = ("-I{portlibs}/include/SDL2", "-ftls-model=local-exec", "-D_REENTRANT")


# --------------------------------------------------------------------------------------- toolchain

def find_devkitpro() -> Path:
    candidates = [os.environ["DEVKITPRO"]] if os.environ.get("DEVKITPRO") else []
    candidates += list(DEVKITPRO_CANDIDATES)
    for candidate in candidates:
        root = Path(candidate)
        if (root / "libnx" / "include").is_dir():
            return root
    sys.exit(
        "error: could not locate a devkitPro install with libnx.\n"
        "       Tried: " + ", ".join(candidates) + "\n"
        "       Set DEVKITPRO to the native path (e.g. C:/devkitPro), not the MSys2 mount."
    )


def find_compiler(devkitpro: Path, cxx: bool) -> str:
    name = "aarch64-none-elf-g++" if cxx else "aarch64-none-elf-gcc"
    for suffix in (".exe", ""):
        exe = devkitpro / "devkitA64" / "bin" / (name + suffix)
        if exe.is_file():
            return str(exe)
    found = shutil.which(name)
    if found:
        return found
    sys.exit(f"error: {name} not found under {devkitpro / 'devkitA64' / 'bin'}")


# ---------------------------------------------------------------------------------------- makefile

def read_makefile_vars() -> dict[str, str]:
    """Grab simple ``NAME := value`` assignments. '+=' lines are deliberately ignored -- the
    variables this script needs (APP_VERSION, SOURCES, INCLUDES, ARCH, DEFINES) are all plain."""
    text = MAKEFILE.read_text(encoding="utf-8", errors="replace")
    variables: dict[str, str] = {}
    for match in re.finditer(r"^([A-Za-z_]\w*)[ \t]*:?=[ \t]*(.*)$", text, re.M):
        variables.setdefault(match.group(1), match.group(2).strip())
    return variables


def expand(value: str, variables: dict[str, str], depth: int = 0) -> str:
    if depth > 8:
        return value
    return re.sub(
        r"\$[({]([A-Za-z_]\w*)[)}]",
        lambda m: expand(variables.get(m.group(1), ""), variables, depth + 1),
        value,
    )


# --------------------------------------------------------------------------------------------- SDL

def sdl_cflags(devkitpro: Path) -> list[str]:
    """Mirror ``pkg-config --cflags SDL2`` by reading sdl2.pc directly (pkg-config itself is an
    MSys2 shell script). Paths inside the .pc are MSys2-style and get rewritten to native ones."""
    portlibs = devkitpro / "portlibs" / "switch"
    pc_file = portlibs / "lib" / "pkgconfig" / "sdl2.pc"
    if not pc_file.is_file():
        return [f.format(portlibs=portlibs.as_posix()) for f in SDL_CFLAGS_FALLBACK]

    defines: dict[str, str] = {}
    cflags_line = ""
    for line in pc_file.read_text(encoding="utf-8", errors="replace").splitlines():
        if match := re.match(r"^([A-Za-z_]\w*)=(.*)$", line):
            defines[match.group(1)] = match.group(2).strip()
        elif line.lower().startswith("cflags:"):
            cflags_line = line.split(":", 1)[1]

    def resolve(text: str, depth: int = 0) -> str:
        if depth > 8:
            return text
        return re.sub(r"\$\{(\w+)\}", lambda m: resolve(defines.get(m.group(1), ""), depth + 1), text)

    flags = shlex.split(resolve(cflags_line))
    # /opt/devkitpro is baked into the .pc; point it at wherever devkitPro actually lives.
    return [re.sub(r"/opt/devkitpro\b", devkitpro.as_posix(), flag) for flag in flags]


# ------------------------------------------------------------------------------------------- flags

def build_flags(devkitpro: Path) -> tuple[list[str], list[str]]:
    """Return (common_flags, cxx_only_flags) matching Makefile CFLAGS / CXXFLAGS."""
    variables = read_makefile_vars()
    portlibs = devkitpro / "portlibs" / "switch"
    libnx = devkitpro / "libnx"

    includes = expand(variables.get("INCLUDES", "include nanovg"), variables).split()
    include_flags = [f"-I{(REPO / d).as_posix()}" for d in includes]
    include_flags += [f"-I{portlibs.as_posix()}/include", f"-I{libnx.as_posix()}/include"]
    include_flags.append(f"-I{(REPO / variables.get('BUILD', 'build')).as_posix()}")

    arch = shlex.split(expand(variables.get("ARCH", ""), variables))
    # shlex strips the shell quoting around -DPKSE_VERSION='"x.y.z"', leaving the literal the
    # compiler needs. Passing argv directly (no shell) keeps it intact.
    defines = shlex.split(expand(variables.get("DEFINES", ""), variables))

    common = [
        "-fsyntax-only",
        "-Wall",
        "-fmax-errors=20",
        "-fdiagnostics-color=never",
        *arch,
        *defines,
        *include_flags,
        "-D__SWITCH__",
        *sdl_cflags(devkitpro),
    ]
    return common, ["-fno-rtti", "-fno-exceptions", "-std=c++20"]


# ------------------------------------------------------------------------------------- file lookup

def rel(path: Path) -> str:
    """Repo-relative when possible -- an explicitly passed file may live outside the tree."""
    try:
        return path.relative_to(REPO).as_posix()
    except ValueError:
        return path.as_posix()


# A production TU that names one of these compiles differently in the development build.
DEVELOPMENT_GATE = re.compile(r"\b(PKSE_DEVELOPMENT_BUILD|SELF_TEST_AVAILABLE)\b")


def translation_units(variable: str) -> list[Path]:
    variables = read_makefile_vars()
    sources = expand(variables.get(variable, ""), variables).split()
    files: list[Path] = []
    for directory in sources:
        base = REPO / directory
        if not base.is_dir():
            continue
        files += sorted(base.glob("*.cpp")) + sorted(base.glob("*.c"))
    return files


def development_sources() -> list[Path]:
    return [path.resolve() for path in translation_units("DEVELOPMENT_SOURCES")]


def build_types_for(path: Path, development_only: list[Path]) -> list[bool]:
    """[False] for production only, [True] for development only, [False, True] for both."""
    if path.resolve() in development_only:
        return [True]
    text = path.read_text(encoding="utf-8", errors="replace")
    return [False, True] if DEVELOPMENT_GATE.search(text) else [False]


# ------------------------------------------------------------------------------------------ driver

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="*", help="source files to check (default: the whole project)")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4, help="parallel jobs")
    parser.add_argument("-v", "--verbose", action="store_true", help="list files that pass too")
    parser.add_argument("--print-flags", action="store_true", help="dump the C++ flags and exit")
    parser.add_argument("--warnings", action="store_true",
                        help="full -O2 codegen pass instead of -fsyntax-only. Slower, but it is the "
                             "only way to see optimizer-dependent warnings (-Wstringop-overflow, "
                             "-Wmaybe-uninitialized, -Warray-bounds); a syntax-only check cannot "
                             "produce them at all, so a clean run there does NOT mean a clean build. "
                             "Any warning makes this mode exit non-zero.")
    args = parser.parse_args()

    devkitpro = find_devkitpro()
    common, cxx_only = build_flags(devkitpro)
    variables = read_makefile_vars()
    development_defines = shlex.split(expand(variables.get("DEVELOPMENT_DEFINES", ""), variables))

    objdir: Path | None = None
    if args.warnings:
        # -fsyntax-only stops before the optimizer, and the warnings that matter most for a release
        # build only exist after it. Match the Makefile's -O2 and emit real objects.
        common = [f for f in common if f != "-fsyntax-only"] + ["-O2"]
        objdir = Path(tempfile.mkdtemp(prefix="pkse-warncheck-"))

    if args.print_flags:
        print(" ".join(common + cxx_only))
        return 0

    if args.files:
        paths = [Path(f) if Path(f).is_absolute() else (REPO / f) for f in args.files]
        missing = [t for t in paths if not t.is_file()]
        if missing:
            sys.exit("error: no such file: " + ", ".join(str(m) for m in missing))
    else:
        paths = translation_units("SOURCES") + translation_units("DEVELOPMENT_SOURCES")
    development_only = development_sources()
    # (path, is_development_build) -- a gated TU appears once per build type.
    targets = [(path, development) for path in paths for development in build_types_for(path, development_only)]

    if not targets:
        print("nothing to check")
        return 0

    compilers = {True: find_compiler(devkitpro, True), False: find_compiler(devkitpro, False)}
    lock = threading.Lock()
    failed: list[str] = []
    warned: list[str] = []
    done = 0

    # The cross compiler writes its scratch files to TMPDIR; on Windows that can resolve somewhere
    # unwritable and fail the compile for reasons that have nothing to do with the source.
    env = dict(os.environ)
    if objdir is not None:
        env["TMPDIR"] = env["TMP"] = env["TEMP"] = str(objdir)

    def check(target: tuple[Path, bool]) -> None:
        nonlocal done
        path, development = target
        is_cxx = path.suffix != ".c"
        defines = development_defines if development else []
        cmd = [compilers[is_cxx], *common, *defines, *(cxx_only if is_cxx else []), str(path)]
        label = rel(path) + (" [development]" if development else "")
        if objdir is not None:
            # Flatten the repo-relative path: two dirs can hold the same basename (Legality.cpp),
            # and colliding object names would have them overwrite each other mid-run.
            obj = objdir / (rel(path).replace("/", "_") + ("-development" if development else "") + ".o")
            cmd += ["-c", "-o", str(obj)]
        result = subprocess.run(cmd, capture_output=True, text=True, cwd=REPO, env=env)
        output = (result.stdout + result.stderr).strip()

        with lock:
            done += 1
            if result.returncode != 0:
                failed.append(label)
                print(f"\n[{done}/{len(targets)}] FAIL {label}")
            elif output:
                warned.append(label)
                print(f"\n[{done}/{len(targets)}] warn {label}")
            elif args.verbose:
                print(f"[{done}/{len(targets)}] ok   {label}")
            if output:
                print(output)

    try:
        with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            list(pool.map(check, targets))
    finally:
        if objdir is not None:
            shutil.rmtree(objdir, ignore_errors=True)

    print(f"\n{len(targets) - len(failed)}/{len(targets)} passed", end="")
    if failed:
        print(f", {len(failed)} FAILED:")
        for label in failed:
            print(f"  {label}")
        return 1
    if warned:
        # In --warnings mode a warning IS the failure: the point of the pass is a clean build.
        print(f" - no errors, {len(warned)} file(s) WITH WARNINGS:")
        for label in warned:
            print(f"  {label}")
        return 1 if args.warnings else 0
    print(" - no errors" + (", no warnings" if args.warnings else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
