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
library_dirs = []
dependencies = []
source_files = []

# We'll need to link `dl` (dynamic linking) and `m` (math) on non-Windows.
if build_machine.system() != 'windows'
    dependencies += compiler.find_library('m')
    dependencies += compiler.find_library('dl')
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

python_install = py.find_installation()
python = python_install.full_path()

# On Windows, we need an explicit path to the Python static library directory, for pybind11.
if build_machine.system() == 'windows'
    base_prefix_command = run_command(python, '-c', 'import sys; print(sys.base_prefix)', check : true)
    base_prefix = base_prefix_command.stdout().strip()
    python_libs_path = base_prefix / 'libs'
    library_dirs += f'-L@python_libs_path@'
    message(f'Python `libs` directory: "@python_libs_path@"')
endif

# ---
# GLM

glm_include = 'third_party' / 'glm-0.9.9.8'
include_dirs += include_directories(glm_include)

# ---
# Pybind11

pybind_includes_command = run_command(python, '-m', 'pybind11', '--includes', check : true)
pybind_include_string = pybind_includes_command.stdout().strip()

# `pybind11 --includes` returns whitespace-separated, GCC-style `-I` syntax.
# We need to extract each path as a separate item, with no prefix.
foreach include : pybind_include_string.split(' ')
    include = include.strip()

    # Remove -I prefix.
    if include.startswith('-I')
        include = include.substring(2)
    endif

    # Meson doesn't accept absolute paths that point inside the project directory.
    # Make any such paths relative before including.
    if include.startswith(project_dir)
        include = fs.relative_to(include, project_dir)
    endif
    include_dirs += include_directories(include)
    message(f'Pybind11 include directory: "@include@"')
endforeach

# Windows doesn't (easily) provide `python3-config`, so pybind11 produces an equivalent for getting the `--extension-suffix`.
# ...On Windows only. It omits this elsewhere, so we've got to change which one we're invoking depending on platform.
if build_machine.system() == 'windows'
    python_suffix_command_args = [ python, '-m', 'pybind11', '--extension-suffix' ]
else
    python_suffix_command_args = [ 'python3-config', '--extension-suffix' ]
endif

pybind_suffix_command = run_command(python_suffix_command_args, check : true)
pybind_suffix = pybind_suffix_command.stdout().strip()

# `pybind11 --extension-suffix` includes the leading `.`, but Meson wants to add this itself.
if pybind_suffix.startswith('.')
    pybind_suffix = pybind_suffix.substring(1)
endif

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

# Ideally, we can grab Boost as a system-known dependency. (Meson has internal magic for this.)
# If not -- i.e, on Windows -- we'll look for a fallback in the `third_party` directory.

if get_option('boost_stacktrace').enabled()
    boost_stacktrace = dependency('boost', required : false)
    if boost_stacktrace.found()
        dependencies += boost_stacktrace
    elif get_option('boost_stacktrace').enabled()
        boost_stacktrace_dir = 'third_party' / 'boost-stacktrace-1.89.0'
        include_dirs += include_directories(boost_stacktrace_dir / 'include')
        message(f'boost_stacktrace: Fallback found: "@boost_stacktrace_dir@"')
    endif

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
    'src/py_api.cpp'
]

shared_library(
    'mollytime',
    source_files,
    name_prefix : '',
    name_suffix : pybind_suffix,
    include_directories : include_dirs,
    link_args : library_dirs,
    dependencies : dependencies,
    install : true,
    install_dir : project_dir / 'mollytime'
)