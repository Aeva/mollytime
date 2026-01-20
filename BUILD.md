# Build system documentation

Mollytime primarily uses the [Meson](https://mesonbuild.com/) build system. Build
tools and Python dependencies are acquired via `pip`. Third-party C++ dependencies
are tracked using Git submodules, and built (if necessary) using some special-case
CMake shims. Mollytime itself can be packaged both as a Wheel (via [`build`](https://pypi.org/project/build/), using
the [meson-python](https://mesonbuild.com/meson-python/) backend), and as a
self-contained executable (via [Pyinstaller](https://pyinstaller.org/en/stable/)).

As much of this as possible is automated using `mollybuild.py`, a project-
specific build script. End-users Shouldn't™ have to care about anything more
than:
- `python mollybuild.py init` - One-time get-and-build of third-party dependencies.
- `python mollybuild.py build` - Build an iterative development environment.
- `python -m mollytime` - Run from the development environment, auto-recompiling if needed.
- `python mollybuild.py package` - Package the project into a wheel.
- `python mollybuild.py exe` - Package the project into a self-contained executable.

If you want to, or must, care about more than this, here's what's really going on.

## 1. Getting Python & build system dependencies

Building the project requires:
- `pip install meson` - [Primary build system for the project.](https://mesonbuild.com/)
- `pip install meson-python` - [Meson backend for building Python wheels.](https://mesonbuild.com/meson-python/)
- `pip install build` - [Top-level Python packaging tool.](https://pypi.org/project/build/)
- `pip install cmake` - [Build system for third-party dependencies.](https://cmake.org/)
- `pip install ninja` - [C++ backend used by both Meson and CMake.](https://ninja-build.org/)
- `pip install pyinstaller` - [Bundles Python programs into a single executable.](https://pyinstaller.org/en/stable/)

Running the project additionally requires:
- `pip install pybind11` - [C++ <-> Python Binding API.](https://pypi.org/project/pybind11/)

`mollybuild.py init` acquires all of these as its first step.

## 2. Getting C++ dependencies

The project's C++ module requires:
- `fmt` - [String formatting & printing.](https://github.com/fmtlib/fmt)
- GLM - [GL-style graphcis math.](https://github.com/g-truc/glm)
- Boost - [`atomic`](https://github.com/boostorg/atomic) and [`stacktrace`](https://github.com/boostorg/stacktrace) support libraries.
- SDL3 - [Low-level platform abstraction.](https://github.com/libsdl-org/SDL)
- SDL3_ttf - [Font rendering support for SDL3.](https://github.com/libsdl-org/SDL_ttf)
- Tracy (optional) - [Multi-platform profiler.](https://github.com/wolfpld/tracy)

All of these, happily, can be and are tracked as Git submodules, and can thus
be acquired idiomatically:
- `git submodule init`
- `git submodule update`

SDL3_ttf requires its own submodule dependencies. If not provided by your
system, you can acquire them using its helper script:
- `sh third_party/SDL3_tff-3.2.2/external/download.sh`
- Or, `powershell -Command & 'third_party/SDL3_tff-3.2.2/external/Get-GitModules.ps1'`

Lastly, Boost is shallow-cloned without any modules, to minimize disk
footprint. Per [its documentation](https://www.boost.org/doc/user-guide/getting-started.html#_individual_modules),
you can acquire only the modules we actually depend upon. Note that Boost's
build scripts require you set its project folder as the working directory:
- `cd third_party/boost_1_90_0`
- `git submodule update --depth 1 -q --init tools/boostdep`
- `git submodule update --depth 1 -q --init libs/atomic`
- `git submodule update --depth 1 -q --init libs/stacktrace`
- `python tools/boostdep/depinst/depinst.py -X test -g "--depth 1" atomic`
- `python tools/boostdep/depinst/depinst.py -X test -g "--depth 1" stacktrace`
- `cd ../..`

`mollybuild.py init` also performs all of this automatically.

## 3. Building C++ dependencies

Only Boost, SDL3, and SDL3_ttf require building from source to integrate.
Ideally, these dependencies could be fully managed by Mollytime's top-level
Meson build. We do not live in an ideal world.
- Not all of these are available through Meson's "WrapDB" pseudo package manager.
- Even so, SDL3 lags behind official releases.
- Even so, SDL3 fails to build on Windows using Clang, a first-class project requirement.
- Even so, SDL3_ttf fails to properly provide import libraries for linking.
- Even so, the required Boost dependences are not provided.

SDL3 and SDL3_ttf use CMake as their primary build system. Boost uses its own system
by default, but offers a CMake alternative. Meson does provide a CMake module for
handling this sort of case, but it's insufficient: SDL3_ttf requires that SDL3 is
discoverable as a CMake package dependency, but Meson provides no way to propagate
such interdependencies between CMake projects.

Meson's preference is that that we write wrapper shims for such non-"native" builds.
Given that its offical wrappers are lacking, we've chosen to simply handle this part
ourselves, avoiding the indirection.

So, as noted earlier, CMake is acquired through `pip` as a build system dependency.
From there, all three dependencies can be built without doing anything too weird.
Since Ninja is also acquired for the system, it's best used as the build backend.

Boost:
- `cmake -Wno-dev -G Ninja -DCMAKE_MAKE_PROGRAM=ninja -S third_party/boost_1_90_0 -B build/boost -DCMAKE_INSTALL_PREFIX=third_party/boost_1_90_0/dist -DBOOST_INSTALL_LAYOUT='versioned'`
    - `-Wno-dev` - Ignore warnings & misconfigurations in the CMake project. It's not ours.
    - `-G Ninja` - Use Ninja backend.
    - `-DCMAKE_MAKE_PROGRAM=ninja` - Here's the Ninja exectuable (i.e. on PATH).
    - `-S third_party/boost_1_90_0` - Here's the Boost source directory.
    - `-B build/boost` - Stash intermediary build artifacts here.
    - `-DCMAKE_INSTALL_PREFIX=third_party/boost_1_90_0/dist` - Install built output here. (Boost `.gitignore`s this directory.)
    - `-DBOOST_INSTALL_LAYOUT='versioned'` - Use the same install layout on all platforms.

SDL3:
- `cmake -Wno-dev -G Ninja -DCMAKE_MAKE_PROGRAM=ninja -S third_party/SDL3-3.4.0 -B build/boost -DCMAKE_INSTALL_PREFIX=third_party/SDL3-3.4.0/dist`
    - `-Wno-dev` - Ignore warnings & misconfigurations in the CMake project. It's not ours.
    - `-G Ninja` - Use Ninja backend.
    - `-DCMAKE_MAKE_PROGRAM=ninja` - Here's the Ninja exectuable (i.e. on PATH).
    - `-S third_party/SDL3-3.4.0` - Here's the SDL3 source directory.
    - `-B build/sdl3` - Stash intermediary build artifacts here.
    - `-DCMAKE_INSTALL_PREFIX=third_party/SDL3-3.4.0/build/install` - Install built output here. (SDL3 `.gitignore`s this directory.)

SDL3_ttf:
- `cmake -Wno-dev -G Ninja -DCMAKE_MAKE_PROGRAM=ninja -S third_party/SDL3_ttf-3.2.2 -B build/boost -DCMAKE_INSTALL_PREFIX=third_party/SDL3_ttf-3.2.2/dist`
    - `-Wno-dev` - Ignore warnings & misconfigurations in the CMake project. It's not ours.
    - `-G Ninja` - Use Ninja backend.
    - `-DCMAKE_MAKE_PROGRAM=ninja` - Here's the Ninja exectuable (i.e. on PATH).
    - `-S third_party/SDL3_ttf-3.2.2` - Here's the SDL3_ttf source directory.
    - `-B build/sdl3` - Stash intermediary build artifacts here.
    - `-DCMAKE_INSTALL_PREFIX=third_party/SDL3_ttf-3.2.2/build/install` - Install built output here. (SDL3_ttf `.gitignore`s this directory.)

It's important that, if not using the helper scripts, you specify the *exact* build
and install directories shown here. The Meson build hardcodes them to look up installed
artifacts.

This is automated by some helper scripts:
- `python build_tools/boost_build.py`
- `python build_tools/sdl3_build.py`
- `python build_tools/sdl3_ttf_build.py`

`mollybuild.py init` invokes these automatically, as its last step.

## 4. Setting up a development environment

Now that the hard stuff's out of the way, we can actually enjoy what Meson's good at,
instead of suffering what it's bad at.

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

`mollybuild.py build` handles all of this automatically.

## 5. Running the project

If all goes well, you will then be able to run Mollytime like so:
- `python -m mollytime`

That's all! Out-of-date C++ source files will be detected and recompiled when the
`mollytime` extension module is imported by the Python runtime.

## 6. Building an executable

The Meson build specifies a Pyinstaller target, not built by default. Build it,
then install it using the `exe` tag:
- `meson compile -C build mollytime-exe`
- `meson install -C build --tags=exe`

`mollybuild.py exe` handles this automatically.

## 7. Packaging a wheel

To package, run `build`. This will integrate with the `meson-python` backend,
passing along your native toolchain of choice to Meson -- or your own arguments,
if you prefer. (This is the same as Step 4, earlier.)

```
python -m build
    -Csetup-args=--native-file=<absolute>/<path>/<to>/build_native/mode-release.ini
    -Csetup-args=--native-file=<absolute>/<path>/<to>/build_native/<toolchain>.ini`
```

Yes, you do need absolute paths if you're using the native config files.

`mollybuild.py package` handle this automatically.

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
