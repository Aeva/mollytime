# Building for Webassembly

It's not pretty at the moment.

Mollytime can target WASM via Emscripten, via [Pyodide](https://pyodide.org/en/stable/). This isn't currently real support; all it does is build a Pyodide-compatible wheel, which could then theoretically be used by Pyodide in an HTML/JS frontend.

Prerequisites:
- Pyodide only officially supports Linux environments. You'll need everything you'd normally need to build the project in Linux, as documented in [BUILD-linux.md](BUILD-linux.md).
- Make sure you have a `venv` set up and activated.
- On some Debian-based distros, such as Debian, you'll need to edit your Python installation's `pyconfig.h` to accept WASM as a buildable platform.
  1. Copy `build_cross/pyconfig-32.h` into `/usr/include/<your python version>/`.
  2. Open `/usr/include/<your python version>/pyconfig.h`.
  3. Edit the top of the file as such:
    ```
    #if defined(__wasm__)
    # include "pyconfig-32.h"
    #elif defined(__linux__)
    ```

Then, build the project as a Pyodide wheel using `pyodide-build`.
1. `pip install pyodide-build`
2. `pyodide xbuildenv search -a` - This lists compatible Pyodide versions. Pick the latest **Version** that is **Compatible: Yes**.
3. `pyodide xbuildenv install <the version you picked>`
4. `git clone https://github.com/emscripten-core/emsdk third_party/emsdk`
5. `cd third_party/emsdk`
6. `pyodide config get emscripten_version` - This prints the latest Emscripten SDK supported by your selected Pyodide version.
7. `./emsdk install <the emscripten sdk version>`
8. `./emsdk activate <the emscripten sdk version>`
9. `source emsdk_env.sh`
10. `cd ../..`
11. `pyodide build --exports whole_archive -Csetup-args=--native-file=<absolute-path-to>/build_native/toolchain-linux-clang.ini -Csetup-args=--cross-file=<absolute-path-to>/build_cross/wasm32-emscripten.ini`

If nothing goes horribly wrong, this will output a wheel in `dist/`.