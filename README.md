
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
- Linux, via Clang or GCC, using LLVM's libc++
- Windows, via Clang or MSVC, using MSVC 2022

Whatever you're using needs its binaries available on `PATH`.

Other dependencies you'll currently need to acquire yourself:
- JACK, if building with the JACK audio backend.
- Windows 11 SDK (10.0.22621.5040) or newer, if building with the WASAPI backend.
  (Don't worry, the SDK also works with Windows 10.)

Once you have determined the correct versions of each of these dependencies,
please let me know and I'll write them down here.

## "I want to work on / quickly try out the project!"

Run this:
- `python mollybuild.py develop -m <mode> -t <toolchain>`

You can see supported `<mode>`s and `<toolchain>`s by checking command help:
- `python mollybuild.py develop -h`

That's it! Now just run `python -m mollytime` to launch the project. When you do,
the C++ extension module will be automatically recompiled if you've changed any
source files since the last run.

## "I want to package the project!"

Run this:
- `python mollybuild.py package -t <toolchain>`

You can see supported `<toolchain>`s by checking command help:
- `python mollybuild.py package -h`

This executes [`build`](https://build.pypa.io/en/latest/) on the project, with all
required settings pre-configured. A source distribution archive and wheel will be output
to a `dist` subfolder. Then, it'll also emit a standalone executable based on the wheel,
using [Pyinstaller](https://pyinstaller.org/).

## "I need to do things to the build system..."

If you just need to configure Meson options -- whether built-in options like the `cpp`
compiler, or Mollytime-specific options like which audio drivers to include -- you can
pass these to `mollybuild.py` as extra positional arguments, in the form `option=value`.

E.g.:
- `python mollybuild.py develop -m debug cpp=g++ audio_driver_jack=disabled`

For anything further, consult `BUILD.md` for more details, with sincere condolences.