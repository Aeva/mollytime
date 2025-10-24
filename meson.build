project('mollytime', 'cpp',
    meson_version : '>=1.3.0',
    default_options : [
        'cpp_std=c++23,c++latest',
        'warning_level=3'   # GCC-style: `-Wall -Wextra -pedantic`; MSVC-style: `/W4`
    ]
)

fs = import('fs')
py = import('python')

compiler = meson.get_compiler('cpp')
project_dir = meson.project_source_root()

include_dirs = []
dependencies = []
source_files = []

# We'll need to link `m` (math) `dl` (dynamic linking) on non-Windows.
dependencies += compiler.find_library('m', required : false)
dependencies += compiler.find_library('dl', required : false)

# HACK: Apparently Meson doesn't actually offer a way for us to get a platform's
# executable suffix...? It absolutely knows about this, and has this internally. See:
# - https://github.com/mesonbuild/meson/blob/8bba5d0a80ece5c14f5e56468dcf50752fe5d46d/mesonbuild/build.py#L2152
# - https://github.com/mesonbuild/meson/blob/master/mesonbuild/envconfig.py#L382
if build_machine.system() == 'windows'
    executable_suffix = '.exe'
else
    executable_suffix = ''
endif

# ---
# Warning wrangling 🤠

if compiler.get_argument_syntax() == 'gcc'
    add_project_arguments(
        '-Wshadow',
        '-Wno-unused-parameter',
        '-Wno-unknown-pragmas',
        language: 'cpp'
    )
elif compiler.get_argument_syntax() == 'msvc'
    add_project_arguments(
        '/wd4068',  # Disable 'Unused parameter'
        '/wd4100',  # Disable 'Unknown pragma'
        language: 'cpp'
    )
endif

# ---
# Python

python_install = py.find_installation(pure : false)
python = python_install.full_path()

# ---
# GLM

glm_include = 'third_party' / 'glm-0.9.9.8'
include_dirs += include_directories(glm_include)

# ---
# Pybind11

dependencies += dependency('pybind11')

# ---
# Pyinstaller

pyi_makespec = find_program('pyi-makespec', required : true)
pyinstaller = find_program('pyinstaller', required : true)

# ---
# Audio backend

if get_option('audio_backend') == 'jack'
    dependencies += dependency('jack', required : true)
    add_project_arguments('-DENABLE_JACK=1', language : 'cpp')
elif get_option('audio_backend') == 'wasapi'
    dependencies += compiler.find_library('RuntimeObject', required : true)
    add_project_arguments('-DAUDIO_WASAPI=1', language : 'cpp')
endif

# ---
# MIDI backend

if get_option('midi_backend') == 'alsa'
    dependencies += compiler.find_library('asound', required : true)
    add_project_arguments('-DMIDI_ALSA=1', language: 'cpp')
elif get_option('midi_backend') == 'mmeapi'
    dependencies += compiler.find_library('Winmm', required : true)
    add_project_arguments('-DMIDI_MMEAPI=1', language : 'cpp')
endif

# ---
# Stack trace

use_boost_stacktraces = get_option('boost_stacktrace').enabled()
if use_boost_stacktraces
    # Ideally, we can grab Boost as a system-known dependency. (Meson has internal magic for this.)
    boost_stacktrace = dependency('boost', required : false)
    if not boost_stacktrace.found()
        # If not -- i.e, on Windows -- we'll look for a fallback in the `third_party` directory.
        boost_stacktrace_dir = 'third_party' / 'boost-stacktrace-1.89.0'
        if not fs.exists(boost_stacktrace_dir)
            # If that's not there either, go ahead and let the build through; this isn't key functionality.
            use_boost_stacktraces = false
        else
            include_dirs += include_directories(boost_stacktrace_dir / 'include')
            message(f'boost_stacktrace: Fallback found: "@boost_stacktrace_dir@"')
        endif
    endif
endif

if use_boost_stacktraces
    add_project_arguments('-DENABLE_STACK_TRACES=1', language : 'cpp')
    if build_machine.system() == 'windows'
        add_project_arguments('-DBOOST_STACKTRACE_USE_WINDBG=1', language: 'cpp')
    endif
endif

# ---
# Tracy

if get_option('tracy').enabled()
    tracy_dir = 'third_party' / 'tracy-0.12.2' / 'public'
    add_project_arguments('-DTRACY_ENABLE', language: 'cpp')
    include_dirs += include_directories(tracy_dir)
    source_files += files(tracy_dir / 'TracyClient.cpp')

    if build_machine.system() != 'windows'
        dependencies += dependency('threads')
    endif
endif

# ---
# File lists

source_files += files(
    'src' / 'alsa_midi.cpp',
    'src' / 'audio_backend.cpp',
    'src' / 'colors.cpp',
    'src' / 'errors.cpp',
    'src' / 'jack_stream.cpp',
    'src' / 'midi.cpp',
    'src' / 'mmeapi_midi.cpp',
    'src' / 'moon.cpp',
    'src' / 'patch.cpp',
    'src' / 'perf.cpp',
    'src' / 'py_api.cpp',
    'src' / 'sdl_display.cpp',
    'src' / 'sdl_draw.cpp',
    'src' / 'sdl_events.cpp',
    'src' / 'sdl_font.cpp',
    'src' / 'sdl_mouse.cpp',
    'src' / 'sdl_rect.cpp',
    'src' / 'sdl_time.cpp',
    'src' / 'wasapi_stream.cpp'
)

python_files = files(
    'mollytime' / '__init__.py',
    'mollytime' / '__main__.py',
    'mollytime' / 'colors.py',
    'mollytime' / 'fonts.py',
    'mollytime' / 'more_math.py',
    'mollytime' / 'patterns.py',
    'mollytime' / 'perf.py',
    'mollytime' / 'power.py',
    'mollytime' / 'pygame_setup.py',
    'mollytime' / 'media' / '__init__.py',
    'mollytime' / 'screens' / '__init__.py',
    'mollytime' / 'screens' / 'calc.py',
    'mollytime' / 'screens' / 'common.py',
    'mollytime' / 'screens' / 'connect.py',
    'mollytime' / 'screens' / 'inspect.py',
    'mollytime' / 'screens' / 'pick_and_place.py',
    'mollytime' / 'screens' / 'scope.py',
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
# Targets

# Compile and install the extension module in `<install root>/mollytime/`.
cpp_module = python_install.extension_module(
    'mollytime',
    source_files,
    include_directories : include_dirs,
    dependencies : dependencies,
    install : true,
    install_tag : 'package',
    subdir : 'mollytime'
)

# Install the Python source files in `<install root>/<source file path>`, preserving
# relative directories. This means that e.g. `mollytime/screens/calc.py` will map to
# `<install root>/mollytime/screens/calc.py`.
python_install.install_sources(
    python_files,
    install_tag : 'package',
    preserve_path : true
)

# Install the data files in `<install root>/<source file path>`, preserving relative
# directories. This means that e.g. `mollytime/media/national_park/NationalPark-Bold.ttf`
# will map to `<install root>/mollytime/media/national_park/NationalPark-Bold.ttf`.
install_data(
    data_files,
    install_dir : python_install.get_install_dir(),
    install_tag : 'package',
    preserve_path : true
)

# Generate a pyinstaller spec file on `setup`.
cpp_module_source = cpp_module.full_path()
cpp_module_target_dir = 'mollytime'
media_dir_source = project_dir / 'mollytime' / 'media'
media_dir_target = 'mollytime' / 'media'

run_command(
    pyi_makespec,
    '--onefile',
    '--specpath', meson.project_build_root(),
    '--name', 'mollytime',
    '--icon', project_dir / 'mollytime.ico',
    '--add-binary', cpp_module_source + ':' + cpp_module_target_dir,
    '--add-data', media_dir_source + ':' + media_dir_target,
    project_dir / 'pyinstaller_main.py',
    check : true
)

# Create a build target for pyinstaller, installed separately from our `package` targets.
spec_file = files(meson.project_build_root() / 'mollytime.spec')

custom_target(
    'mollytime-exe',
    command : [
        pyinstaller,
        '--workpath', '@PRIVATE_DIR@',
        '--distpath', '@OUTDIR@',
        '@INPUT@'
    ],
    build_by_default : false,
    input : [ spec_file ],
    output : 'mollytime' + executable_suffix,
    depend_files : python_files + data_files,
    depends : [ cpp_module ],
    install : true,
    install_dir: project_dir / 'dist',
    install_tag : 'exe'
)
