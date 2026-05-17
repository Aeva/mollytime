project('mollytime', 'cpp',
    meson_version : '>=1.3.0',
    license : 'Apache-2.0',
    license_files : 'LICENSE.txt',
    default_options : [
        'cpp_std=c++17',
        'warning_level=3'   # GCC-style: `-Wall -Wextra -pedantic`; MSVC-style: `/W4`
    ]
)

py = import('python')

compiler = meson.get_compiler('cpp')
project_dir = meson.project_source_root()

python_install = py.find_installation(pure : false)
python = python_install.full_path()

# Warning wrangling 🤠
extra_warnings = []
if compiler.get_argument_syntax() == 'gcc'
    extra_warnings += [
        '-Wshadow',
        '-Wno-unused-parameter',
        '-Wno-unknown-pragmas',
    ]
elif compiler.get_argument_syntax() == 'msvc'
    extra_warnings += [
        '/wd4068',  # Disable 'Unused parameter'
        '/wd4100',  # Disable 'Unknown pragma'
    ]
endif
if compiler.get_id() == 'clang'
    extra_warnings += [
        '-Wnewline-eof',
        '-Wshadow-field-in-constructor'
    ]
endif

# HACK: I want to disable *all* warnings for *external dependencies only*, but Meson doesn't permit this.
# - `warning_level` provides no `none` option: https://github.com/mesonbuild/meson/issues/8636
# - We can't directly pass warning flags to `dependency()`: https://github.com/mesonbuild/meson/issues/9486
#
# As a workaround, we'll use warning level 0 (default) for the problem dependencies -- currently SDL3 -- and
# globally disable *only* the additional warnings that get through that. Luckily, SDL3 is C-only, and we're
# C++-only, so the language specifier will ensure we still get these warnings in our code.
if compiler.get_argument_syntax() == 'gcc'
    add_global_arguments(
        '-Wno-cast-function-type-mismatch',
        '-Wno-deprecated-declarations',
        language : 'c'
    )
elif compiler.get_argument_syntax() == 'msvc'
    add_global_arguments(
        '/wd4127', # Constant conditonal expression
        '/wd4244', # Implicit signed <-> unsigned conversion
        '/wd4267', # Implicit narrowing conversion
        '/wd4996', # "Insecure" C stdlib function
        language : 'c'
    )
endif

# ---
# Source files

backend_source_files = files(
    'mollytime' / 'backend' / 'audio_driver.cpp',
    'mollytime' / 'backend' / 'colors.cpp',
    'mollytime' / 'backend' / 'config.cpp',
    'mollytime' / 'backend' / 'errors.cpp',
    'mollytime' / 'backend' / 'midi.cpp',
    'mollytime' / 'backend' / 'moon.cpp',
    'mollytime' / 'backend' / 'patch.cpp',
    'mollytime' / 'backend' / 'perf.cpp',
    'mollytime' / 'backend' / 'py_api.cpp',
    'mollytime' / 'backend' / 'sdl_display.cpp',
    'mollytime' / 'backend' / 'sdl_draw.cpp',
    'mollytime' / 'backend' / 'sdl_events.cpp',
    'mollytime' / 'backend' / 'sdl_font.cpp',
    'mollytime' / 'backend' / 'sdl_mouse.cpp',
    'mollytime' / 'backend' / 'sdl_rect.cpp',
    'mollytime' / 'backend' / 'sdl_time.cpp',
    'mollytime' / 'backend' / 'thunks.cpp',
)

python_files = files(
    'mollytime' / '__init__.py',
    'mollytime' / '__main__.py',
    'mollytime' / 'colors.py',
    'mollytime' / 'dpi.py',
    'mollytime' / 'fonts.py',
    'mollytime' / 'more_math.py',
    'mollytime' / 'patterns.py',
    'mollytime' / 'perf.py',
    'mollytime' / 'power.py',
    'mollytime' / 'media' / '__init__.py',
    'mollytime' / 'screens' / '__init__.py',
    'mollytime' / 'screens' / 'calc.py',
    'mollytime' / 'screens' / 'common.py',
    'mollytime' / 'screens' / 'inspect.py',
    'mollytime' / 'screens' / 'midi_settings.py',
    'mollytime' / 'screens' / 'pick_and_place.py',
    'mollytime' / 'screens' / 'select.py'
)

data_files = files(
    'mollytime' / 'media' / 'afacad' / 'Afacad-Italic-VariableFont_wght.ttf',
    'mollytime' / 'media' / 'afacad' / 'Afacad-VariableFont_wght.ttf',
    'mollytime' / 'media' / 'afacad' / 'static' / 'Afacad-Bold.ttf',
    'mollytime' / 'media' / 'afacad' / 'static' / 'Afacad-BoldItalic.ttf',
    'mollytime' / 'media' / 'afacad' / 'static' / 'Afacad-Italic.ttf',
    'mollytime' / 'media' / 'afacad' / 'static' / 'Afacad-Medium.ttf',
    'mollytime' / 'media' / 'afacad' / 'static' / 'Afacad-MediumItalic.ttf',
    'mollytime' / 'media' / 'afacad' / 'static' / 'Afacad-Regular.ttf',
    'mollytime' / 'media' / 'afacad' / 'static' / 'Afacad-SemiBold.ttf',
    'mollytime' / 'media' / 'afacad' / 'static' / 'Afacad-SemiBoldItalic.ttf',
    'mollytime' / 'media' / 'national_park' / 'NationalPark-Bold.ttf',
    'mollytime' / 'media' / 'national_park' / 'NationalPark-ExtraBold.ttf',
    'mollytime' / 'media' / 'national_park' / 'NationalPark-ExtraLight.ttf',
    'mollytime' / 'media' / 'national_park' / 'NationalPark-Light.ttf',
    'mollytime' / 'media' / 'national_park' / 'NationalPark-Medium.ttf',
    'mollytime' / 'media' / 'national_park' / 'NationalPark-Regular.ttf',
    'mollytime' / 'media' / 'national_park' / 'NationalPark-SemiBold.ttf',
)

# ---
# Dependencies

dependencies = []

# *nixisms
libm = compiler.find_library('m', required : false)
libdl = compiler.find_library('dl', required : false)

# From wrap
boost = dependency('boost', required : true, version : '>=1.90.0', default_options : 'modules=atomic,stacktrace')
fmt = dependency('fmt', required : true, version : '>=12.0.0', default_options : 'header-only=true')
glm = dependency('glm', required : true, version : '>=1.0.1')
sdl3 = dependency('sdl3', required : true, version : '>=3.4.0', default_options : { 'warning_level' : '0' })
sdl3_ttf = dependency('sdl3_ttf', required : true, version : '>=3.2.0', default_options : { 'warning_level' : '0' })
tracy = dependency('tracy', version : '>=0.13.0', required : get_option('tracy'))

# HACK: Nanobind is intended to integrate directly into the source tree. Compile it as a separate
# library anyway, so its warnings can be separately managed.
# We wouldn't have to do this if Meson's handling of subproject warnings wasn't *utterly* deranged.
nanobind_disable_warnings = []
if compiler.get_id() == 'gcc'
    nanobind_disable_warnings += '-Wno-shadow'
endif

nanobind_source = dependency('nanobind', required : true, version : '>=2.11.0')
nanobind_lib = static_library(
    'nanobind_lib',
    dependencies : nanobind_source,
    cpp_args : nanobind_disable_warnings,
    override_options : { 'warning_level' : '0' }
)
nanobind = declare_dependency(
    dependencies : nanobind_source.partial_dependency(includes : true),
    link_with : nanobind_lib
)

# ---
# Targets

driver_dependencies = []

if get_option('audio_driver_sdl').allowed()
    driver_dependencies += declare_dependency(
        dependencies : [ boost, fmt, sdl3 ],
        compile_args : [ '-DAUDIO_SDL=1' ] + extra_warnings,
        sources : files('mollytime' / 'backend' / 'sdl_stream.cpp')
    )
endif

jack = dependency('jack', required : false)
use_jack = get_option('audio_driver_jack').enable_auto_if(jack.found())
if use_jack.enabled()
    driver_dependencies += declare_dependency(
        dependencies : [ boost, fmt, jack ],
        compile_args : [ '-DENABLE_JACK=1' ] + extra_warnings,
        sources : files('mollytime' / 'backend' / 'jack_stream.cpp')
    )
endif

use_wasapi = get_option('audio_driver_wasapi').enable_auto_if(host_machine.system() == 'windows')
if use_wasapi.enabled()
    windows_runtime_object = compiler.find_library('RuntimeObject', required : true)
    driver_dependencies += declare_dependency(
        dependencies : [ boost, fmt, windows_runtime_object ],
        compile_args : [ '-DAUDIO_WASAPI=1' ] + extra_warnings,
        sources : files('mollytime' / 'backend' / 'wasapi_stream.cpp'),
    )
endif

alsa = compiler.find_library('asound', required : false)
use_alsa = get_option('midi_driver_alsa').enable_auto_if(alsa.found())
if use_alsa.enabled()
    driver_dependencies += declare_dependency(
        dependencies : [ boost, fmt, alsa ],
        compile_args : [ '-DMIDI_ALSA=1' ] + extra_warnings,
        sources : files('mollytime' / 'backend' / 'alsa_midi.cpp')
    )
endif

winmm = compiler.find_library('Winmm', required : false)
use_winmm = get_option('midi_driver_mmeapi').enable_auto_if(winmm.found())
if use_winmm.enabled()
    driver_dependencies += declare_dependency(
        dependencies : [ boost, fmt, winmm ],
        compile_args : [ '-DMIDI_MMEAPI=1' ] + extra_warnings,
        sources : files('mollytime' / 'backend' / 'mmeapi_midi.cpp')
    )
endif

# Compile and install the extension module in `<install root>/mollytime/`.
cpp_module = python_install.extension_module(
    'backend',
    backend_source_files,
    dependencies : [ libm, libdl, boost, fmt, glm, nanobind, sdl3, sdl3_ttf, tracy ] + driver_dependencies,
    cpp_args : extra_warnings,
    install : true,
    subdir : 'mollytime'
)

# Install the Python source files in `<install root>/<source file path>`, preserving
# relative directories. This means that e.g. `mollytime/screens/calc.py` will map to
# `<install root>/mollytime/screens/calc.py`.
python_install.install_sources(
    python_files,
    preserve_path : true
)

# Install the data files in `<install root>/<source file path>`, preserving relative
# directories. This means that e.g. `mollytime/media/national_park/NationalPark-Bold.ttf`
# will map to `<install root>/mollytime/media/national_park/NationalPark-Bold.ttf`.
install_data(
    data_files,
    install_dir : python_install.get_install_dir(),
    install_tag : 'python-runtime',
    preserve_path : true
)

# Install remaining informational files directly to the install root.
install_data(
    meson.project_license_files(),
    'CONTRIBUTORS.txt',
    install_dir : python_install.get_install_dir(),
    install_tag : 'python-runtime'
)