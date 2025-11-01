
# What is Mollytime?

Mollytime is a virtual synthesizer, with a visual patch editor designed for
touch screen devices.  Mollytime is intended to be useful for live
performances, and dynamic sound tracks for interactive media such as video-games
and installation art.

![A screeenshot of a virtual synthesizer visual programming language.  Patches are described as a set of tiles linked by arrows which describe the flow of samples through the system and how they are mutated.](screenshot.png)

# This Synthesizer Is Under Construction

While Mollytime is already quite usable, it is still missing quite a bit of
polish and several major features, and is not quite ready for prime time.

# I want to use it anyway!

Mollytime is a Python-based project, so you'll naturally need Python 3. Other Python
package dependencies will be automatically acquired by the build system.
These will go in your current environment, so setting up a `venv` is supported and
recommended.

You'll also need a C++ toolchain to build the core extension module. Supported
platforms, as in "we've tried these and it seems to work fine":
- Linux, via Clang or GCC, using glibc
- Windows, via Clang or MSVC, using MSVC 2022

Whatever you're using needs its binaries available on `PATH`.

Other dependencies you'll currently need to acquire yourself:
- JACK, if building with the JACK audio backend.
- Windows 11 SDK (10.0.22621.5040) or newer, if building with the WASAPI backend.
  (Don't worry, it also works with Windows 10.)
- `boost_stacktrace`, if building with stacktrace support.

Once you have determined the correct versions of each of these dependencies,
please let me know and I'll write them down here.

## "I want to work on / quickly try out the project!"

Run this:
- `python mollybuild.py setup <mode> <toolchain>`

That's it! Now just run `python -m mollytime` to launch the project. When you do,
the C++ extension module will be automatically recompiled if you've changed any
source files since the last run..

You can see supported `<mode>`s and `<toolchain>`s by checking command help:
- `python mollybuild.py setup -h`

If you omit the toolchain, it'll detect your system "default." This might work
even if your system default isn't on the supported list, but it might not.

## "I want to build a 'exe'!"

After `setup`, run this:
- `python mollybuild.py exe`

This uses [Pyinstaller](https://pyinstaller.org/) to bundle the project & its
dependencies into a single-file executable, output to a `dist` subfolder.

## "I want to package the project for distribution!"

Run this:
- `python mollybuild.py package <toolchain>`

This executes [`build`](https://build.pypa.io/en/latest/) on the project, with all
required settings pre-configured. An sdist and wheel will be output to a `dist`
subfolder.

You can see supported `<toolchain>`s by checking command help:
- `python mollybuild.py package -h`

If you omit the toolchain, it'll detect your system "default." This might work
even if your system default isn't on the supported list, but it might not.

# No, I want to build it all myself!

Mollytime uses the [Meson](https://mesonbuild.com/) build system, and leverages
[meson-python](https://mesonbuild.com/meson-python/) for packaging.

If you'd prefer to build "manually," you'll need to install these dependencies:
- `pip install meson`
- `pip install meson-python`
- `pip install ninja`
- `pip install pybind11`
- `pip install pyinstaller`
- `pip install build`

Native environment config files are provided for the Meson build, in `build_native/`.
You'll want to specify a build mode (`mode-<mode>.ini`), and a toolchain
(`toolchain-<os>-<tools>.ini`).

For an iterative development workflow, run `pip install` for an editable wheel:
```
pip install
    -Ceditable-verbose=true
    -Csetup-args=--native-file=<absolute>/<path>/<to>/build_native/toolchain-<toolchain>.ini
    -Csetup-args=--native-file=<absolute>/<path>/<to>/build_native/mode-<mode>.ini
    -Csetup-args=-Dbuildtype=<mode>
    -Ccompile-args=-mollytime<python-extension-suffix>
    --editable .
```

> Note that you have to manually specify the `buildtype`. This is due to an oversight in
> meson-python: its [built-in option overrides](https://mesonbuild.com/meson-python/explanations/default-options.html)
> are hardcoded as CLI args, meaning they always take priority over our native files.
> We have to claim even-higher priority by passing in our own CLI override.
>
> Further note the need for an extension suffix on the the `compiler-args`. Without this,
> meson-python will unnecessarily build the Pyinstaller target, wasting a minute or more of
> time. Getting that suffix is up to you; or just omit the args, if you don't mind the wait.

If all goes well, you will then be able to run Mollytime like so:
 - `python -m mollytime`

To build an executable, compile & install the target through the Meson project:
- `meson compile -C <the build dir> mollytime-exe`
- `meson install -C <the build dir> --tags=exe`

To package, run this lovely incantation, with your toolchain of choice:
```
python -m build
    -Csetup-args=--native-file=<absolute>/<path>/<to>/build_native/mode-release.ini
    -Csetup-args=--native-file=<absolute>/<path>/<to>/build_native/<toolchain>.ini`
```

Yes, you do need absolute paths for both.

## "I don't even want to use the native .ini files! I'll configure it all myself!"

Look at `meson.options` to see available build options. You'll probably want to
select an `audio_backend`, at minimum. Be sure to reference Meson's
[built-in options](https://mesonbuild.com/Builtin-options.html) for anything not
covered here, like whether or not to emit optimized builds.

> IMPORTANT: On Linux, you'll need to disable Meson's `b_asneeded` and `b_lundef` options.

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
  pipewire-jack-audio-connection-kit-devel \
  alsa-lib-devel \
  python3-devel \
  python3-tkinter
```

After installing the required dependencies, Mollytime then can be setup with the following command,
as described in the sections above:
```
python mollybuild.py setup release linux-clang
```
