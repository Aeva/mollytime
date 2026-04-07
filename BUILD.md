# Build system documentation

Mollytime primarily uses the [Meson](https://mesonbuild.com/) build system. Build
tools are acquired via `pip`. Third-party C++ dependencies are downloaded and built
using Meson's "wrap" feature, if they can't be fulfilled by your system.  Mollytime
itself can be packaged both as a Wheel (via [`build`](https://pypi.org/project/build/),
using the [meson-python](https://mesonbuild.com/meson-python/) backend), and as a
self-contained executable (via [Pyinstaller](https://pyinstaller.org/en/stable/)).

As much of this as possible is automated using `mollybuild.py`, a project-
specific build script. End-users Shouldn't™ have to care about anything more
than:
- `python mollybuild.py develop` - Build an iterative development environment.
- `python -m mollytime` - Run from the development environment, auto-recompiling if needed.
- `python mollybuild.py package` - Package the project into source distribution, wheel, and self-contained executable.

If you want to, or must, care about more than this, here's what's really going on.

## 1. Getting Python & build system dependencies

Building a development environment for the project requires:
- `pip install meson` - [Primary build system for the project.](https://mesonbuild.com/)
- `pip install meson-python` - [Meson backend for building Python wheels.](https://mesonbuild.com/meson-python/)
- `pip install ninja` - [C++ backend used by Meson.](https://ninja-build.org/)

`mollybuild.py develop` and `mollybuild.py package` acquire all of these automatically.

Packaging the project requires:
- `pip install build` - [Top-level Python packaging tool.](https://pypi.org/project/build/)

`mollybuild.py package` acquires this automatically.

## 2. Getting C++ dependencies

The project's C++ module requires:
- Boost - [`atomic`](https://github.com/boostorg/atomic) and [`stacktrace`](https://github.com/boostorg/stacktrace) support libraries.
- `fmt` - [String formatting & printing.](https://github.com/fmtlib/fmt)
- GLM - [GL-style graphcis math.](https://github.com/g-truc/glm)
- Pybind11 - [C++ <-> Python Binding API.](https://pypi.org/project/pybind11/)
- SDL3 - [Low-level platform abstraction.](https://github.com/libsdl-org/SDL)
- SDL3_ttf - [Font rendering support for SDL3.](https://github.com/libsdl-org/SDL_ttf)
- Tracy (optional) - [Multi-platform profiler.](https://github.com/wolfpld/tracy)

Everything but Boost has an externally-maintained port available on Meson's
[WrapDB](https://mesonbuild.com/Wrapdb-projects.html), which the project will
automatically download and execute as a build step, if the dependency can't first
be fulfilled by your system packages.

SDL3 uses a local copy of the [Meson team's wrap overlay](https://github.com/mesonbuild/wrapdb),
extended to support Emscripten. It's on You, the Maintainer of the Build, to ensure that
any important updates they make to the overlay are propagated into the local copy. (It'd be
nicer if we could fork individual patch repos instead, but so far, so Meson.)

`fmt`'s externally-maintained port doesn't properly expose its header-only variant as a
Meson dependency name. A local `diff_files` patch applied to its `.wrap` addresses this.

SDL3_ttf's externally-maintained port is broken, and needs some patching.
- `freetype2` is required, but no external dependency acquisition method is provided.
  We thus specifice a `.wrap` for it ourselves, which the build can then discover.
- `-D DLLEXPORT` needs to be defined on Windows, or the build will incorrectly assume
  static linking, and fail. This is fixed by applying a local `diff_files` patch in the
  SDL3_ttf `.wrap`.
- The SDL3_ttf binaries need to be marked as installable, or they can't be propagated
  into a built package, breaking Windows. This is also fixed through the `diff_files` patch.

For Boost, the build leverages Meson's support for downloading Git repositories, and then
applies a minimal build patch to discover and propagate a provided set of header-only
libraries. Non-header-only libraries aren't yet supported, but also aren't yet needed.

## 3. Setting up a development environment

`meson-python` integrates with `pip` to provide support for dynamically recompiling
the project's C++ source files at module import-time, as needed, if the project is
installed in "editable" mode. This means that instead of running ordinary Meson
setup, you'll "just" run `pip install --editable .`.

However, a lot of configuration is required to get exactly-correct behavior. To keep
this concise, we provide ["Native environment" config files](https://mesonbuild.com/Native-environments.html),
that predefine known-good environments, in `build_native/`. You'll want to specify a
build mode (`mode-<mode>.ini`), and a toolchain (`toolchain-<os>-<tools>.ini`). E.g.:
```
pip install
    -Ceditable-verbose=true
    -Csetup-args=--native-file=<absolute>/<path>/<to>/build_native/toolchain-<toolchain>.ini
    -Csetup-args=--native-file=<absolute>/<path>/<to>/build_native/mode-<mode>.ini
    -Csetup-args=-Dbuildtype=<mode>
    -Ccompile-args=-mollytime<python-extension-suffix>
    --editable .
```
Note that you have to manually specify the `buildtype`. This is due to an oversight in
meson-python: its [built-in option overrides](https://mesonbuild.com/meson-python/explanations/default-options.html)
are hardcoded as CLI args, meaning they always take priority over our native files.
We have to claim even-higher priority by passing in our own CLI override.

Further note the need for an extension suffix on the the `compiler-args`. Without this,
meson-python will unnecessarily build the Pyinstaller target, wasting a minute or more
of time. Getting that suffix is up to you; or just omit the args, if you don't mind the
wait.

If you want to, you can omit or override the native config files, and provide your own
arguments. Look at `meson.options` to see available build options. You'll probably want
to select an `audio_backend`, at minimum. Be sure to reference Meson's
[built-in options](https://mesonbuild.com/Builtin-options.html) for anything not
covered here, like whether or not to emit optimized builds.

> IMPORTANT: On Linux, you'll need to disable Meson's `b_asneeded` and `b_lundef` options.

`mollybuild.py develop` handles all of this automatically.

## 4. Running the project

If all goes well, you will then be able to run Mollytime like so:
- `python -m mollytime`

That's all! Out-of-date C++ source files will be detected and recompiled when the
`mollytime` extension module is imported by the Python runtime.

## 5. Packaging a wheel

To package, run `build`:
```
python -m build
    -Csetup-args=--native-file=<absolute>/<path>/<to>/build_native/mode-release.ini
    -Csetup-args=--native-file=<absolute>/<path>/<to>/build_native/<toolchain>.ini
```

This will integrate with the `meson-python` backend, passing along your native toolchain
of choice to Meson -- or your own arguments, if you prefer. (This is the same as Step 3,
earlier.) Yes, you do need absolute paths if you're using the native config files.

`mollybuild.py package` handles this automatically.

## 6. Packaging an executable

Pyinstaller is designed to work on a complete and well-formatted Python package, with all
modules discoverable through natural `import`s, pointing to idiomatic filesystem locations.
Mollytime doesn't exist in such a form until it's formally packaged, so the best way to get
a clean Pyinstaller build is to execute it upon the extracted contents of a built wheel.

The wheel is just a glorified `.zip` archive, and can be extracted with any tool that knows
how to unzip, like `unzip`.

Then, you'll need to copy `pyinstaller_main.py` into the directory you extracted to, such that
it's a sibling of the `mollytime/` directory. This works around a known, perpetually unfixed
Pyinstaller issue, where package-relative imports can't be used in an entry-point module:
https://github.com/pyinstaller/pyinstaller/issues/2560

Now you can run Pyinstaller:
```
pyinstaller
    --onefile                           # Build a self-contained executable.
    --name mollytime                    # Name it "mollytime".
    --icon <project dir>/mollytime.ico  # Use this icon for the executable.
    --copy-metadata mollytime           # Copy package metadata.
    --collect-binaries mollytime        # Find and propagate all binary dependencies, e.g DLLs on Windows.
    --collect-data mollytime            # Find and propagate all non-Python files in the package.
    pyinstaller_main.py                 # Program entry point.
```

`mollybuild.py package` handles this automatically.

# Appendix A: Linux Dependencies
## Fedora 42

Fedora Linux players are recommended to use the [Fedora Cinnamon Spin](https://fedoraproject.org/spins/cinnamon),
or failing that, they're recommended to use the Cinnamon desktop environment w/ their choice of xserver.
Gnome and KDE both have hardcoded behavior that prevent multitouch programs from receiving more than 
three simultaneous touch points, which is problematic for using Mollytime as a touch screen instrument.

Regardless of their choice of window manager, Fedora Linux players will want to install the following
packages before building mollytime:
```
sudo dnf install \
  clang \
  libcxx-devel \
  pipewire-jack-audio-connection-kit-devel \
  alsa-lib-devel \
  python3-devel
```

After installing the required dependencies, Mollytime then can be setup with the following command,
as described in the sections above:
```
python mollybuild.py setup release linux-clang
```

## Ubuntu Studio 24.04 LTS

Ubuntu Studio 24.04 defaults to KDE, which may or may not prove to be problematic for touch
screens, as noted in the section about Fedora above.  This has yet to be tested.

Ubuntu Studio 24.04 does not provide a suitable version of Clang or SDL3 through apt, so these must be installed
manually.

Fortunately, the LLVM project provides compiled versions of Clang for apt based Linux distributions.
Follow these instructions to install Clang 20 on your system:
[Install Clang 20, 19, or old versions in Ubuntu 24.04 | 22.04](https://ubuntuhandbook.org/index.php/2023/09/how-to-install-clang-17-or-16-in-ubuntu-22-04-20-04/)

Next, install Mollytime's required dependencies via apt like so:

```
sudo apt-get install python3-dev python3-venv clang-20 lldb-20 lld-20 clangd-20 libc++-20-dev
```

Now we need to build SDL3 and SDL3_ttf from source.  For the sake of copy-and-paste without reading,
these instructions will have you make a folder called `science` in your home folder, which will
contain the SDL build source trees as well as Mollytime, but feel free to use your own filing
system if you're the sort that likes to read all this text.

```
cd ~
mkdir science
cd science
git clone https://github.com/libsdl-org/SDL
git clone https://github.com/libsdl-org/SDL_ttf
git clone https://github.com/Aeva/mollytime
```

Note that we're drinking from the root branch for all three.  These should generally be pretty stable,
but you may encounter bugs.  Assuming all went well, let's build these:

```
cd ~/science/SDL
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DSDL_X11_XSCRNSAVER=OFF ..
cmake --build . --config Release --parallel
sudo cmake --install . --config Release
```

If all goes well, SDL3 will build without issue.  The last command installs it somewhere our
build system can find it.  Assuming this was successful, now let's do the same for SDL3_ttf:

```
cd ~/science/SDL_ttf
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release --parallel
sudo cmake --install . --config Release
```

If that was successful, we should be able to build Mollytime now.

```
cd ~/science/mollytime
python -m venv venv
source venv/bin/activate
python mollybuild.py setup release linux-clang-20
```

From here, you can run Mollytime by running `python -m mollytime` while your venv is active.
