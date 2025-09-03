
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

Mollytime uses the [Meson](https://mesonbuild.com/) build system.

Supported platforms, as in "we've tried these and it seems to work fine":
- Linux, via Clang or GCC, using glibc
- Windows, via Clang or MSVC, using MSVC 2022

Mollytime is a Python-based project, so you'll naturally need Python 3. Otherwise,
most dependencies can be acquired via `pip`. (Setting up a virtual environment
first is supported and recommended.)
- `pip install meson`
- `pip install ninja` (if using Meson's default Ninja backend)
- `pip install pygame`
- `pip install pybind11`

Other dependencies you'll currently need to acquire yourself:
- JACK, if building with the JACK audio backend.
- Windows 11 SDK (10.0.22621.5040) or newer, if building with the WASAPI backend. (Don't worry, it also works with Windows 10.)
- `boost_stacktrace`, if building with stacktrace support.

Once you have determined the correct versions of each of these dependencies,
please let me know and I'll write them down here.

Some native environment config files are provided for the Meson build. You'll
want to specify a build mode (`debug.ini`, `profiling.ini`, or `release.ini`,) 
and a toolchain (`<os>-<tools>.ini`). Then, you can just `build` and `install`.
Example:
```
meson setup --native-file build_native/win32-clang.ini --native-file build_native/debug.ini build
meson compile -C build
meson install -C build
```

If all goes well, you will then be able to run Mollytime like so:

 - `python mollytime`

Note that any toolchain you request needs its binaries available on PATH,
for both `setup` and `compile`.

---

If you want to configure manually instead, look at `meson.options` to see available
build options. You'll probably want to select an `audio_backend`, at minimum.
Be sure to reference Meson's [built-in options](https://mesonbuild.com/Builtin-options.html)
for anything not covered here, like whether or not to emit optimized builds.

> IMPORTANT: On Linux, you'll need to disable Meson's `b_asneeded` and `b_lundef` options.