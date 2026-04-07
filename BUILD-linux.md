# Linux prerequisites

We've personally tested:
- Debian 13
- Ubuntu 25.10
- Fedora 42
- Gentoo

Any distro needs to install:
- [All packages required by SDL](https://github.com/libsdl-org/SDL/blob/main/docs/README-linux.md).
- `clang` and `libc++-dev`, if you don't want to stick to GCC.

Debian/Ubuntu users should additionally install:
- `python3-dev`
- `python3-venv`
- `python-is-python3`

Gentoo requires:
 - `x11-apps/xrandr` (Mollytime will build without it, but it wont run)

Gentoo users wishing to use clang instead of gcc will also need:
 - `llvm-runtimes/libcxx` (aforementioned `libc++-dev`)

Webassembly has only been successfully built on Debian & Ubuntu, and not without hacks. See [BUILD-wasm.md](BUILD-wasm.md).
