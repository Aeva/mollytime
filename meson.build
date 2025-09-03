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

python_install = py.find_installation()
python = python_install.full_path()

# ---
# GLM

glm_include = 'third_party' / 'glm-0.9.9.8'
include_dirs += include_directories(glm_include)

# ---
# Pybind11

dependencies += dependency('pybind11')

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
    source_files += tracy_dir / 'TracyClient.cpp'

    if build_machine.system() != 'windows'
        dependencies += dependency('threads')
    endif
endif

# ---
# mollytime library

source_files += [
    'src/alsa_midi.cpp',
    'src/audio_backend.cpp',
    'src/colors.cpp',
    'src/errors.cpp',
    'src/jack_stream.cpp',
    'src/midi.cpp',
    'src/patch.cpp',
    'src/perf.cpp',
    'src/py_api.cpp',
    'src/wasapi_stream.cpp'
]

python_install.extension_module(
    'mollytime',
    source_files,
    include_directories : include_dirs,
    dependencies : dependencies,
    install : true,
    subdir : project_dir / 'mollytime'
)