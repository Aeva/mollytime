
# What is Mollytime?

Mollytime is a virtual synthesizer, with a visual patch editor designed for
touch screen devices.  Mollytime is intended to be useful for live
performances, and dynamic sound tracks for interactive media such as video-games
and installation art.

![A screeenshot of a virtual synthesizer visual programming language.  Patches are described as a set of tiles linked by arrows which describe the flow of samples through the system and how they are mutated.](screenshot.png)

# This Synthesizer Is Under Construction

While Mollytime is already quite usable, it is still missing quite a bit of
polish and several major features, and is not quite ready for prime time.

In particular, even though a Windows build is currently provided, it doesn't
yet have any audio backends implemented, so you won't be able to hear anything.

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
- `boost_stacktrace`, if building with stacktrace support.

Once you have determined the correct versions of each of these dependencies,
please let me know and I'll write them down here.

Then, look at `meson.options` to see available build options. You'll probably
want to select an `audio_backend`, at minimum. Be sure to reference Meson's
[built-in options](https://mesonbuild.com/Builtin-options.html) for anything
not covered here, like whether or not to emit optimized builds.

> IMPORTANT: On Linux, you'll currently need to disable Meson's `b_asneeded` and
`b_lundef` options, until we add a way to handle this kind of configuration for you.

An example command sequence for building an optimized "release" build for Linux,
using Clang:
```
CXX=clang++ meson setup -Dbuildtype=release -Db_asneeded=false -Db_lundef=false -Daudio_backend=jack -Dmidi_backend=alsa build
meson compile -C build
meson install -C build
```

An example command sequence for building a "debug" build on Windows, using MSVC:
```
meson setup -Dbuildtype=debug -Daudio_backend=wasapi build
meson compile -C build
meson install -C build
```

If all goes well, you will then be able to run Mollytime like so:

 - `python mollytime`
