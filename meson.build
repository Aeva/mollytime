project('mollytime', 'cpp',
    meson_version : '>=1.3.0',
    default_options : [
        'cpp_std=c++17',
        'warning_level=3'   # GCC-style: `-Wall -Wextra -pedantic`; MSVC-style: `/W4`
    ]
)

cmake = import('cmake')
fs = import('fs')
py = import('python')

compiler = meson.get_compiler('cpp')
project_dir = meson.project_source_root()

python_install = py.find_installation(pure : false)
python = python_install.full_path()

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

# HACK: Apparently Meson doesn't actually offer a way for us to get a platform's
# executable suffix...? It absolutely knows about this, and has this internally. See:
# - https://github.com/mesonbuild/meson/blob/8bba5d0a80ece5c14f5e56468dcf50752fe5d46d/mesonbuild/build.py#L2152
# - https://github.com/mesonbuild/meson/blob/master/mesonbuild/envconfig.py#L382
if build_machine.system() == 'windows'
    executable_suffix = '.exe'
else
    executable_suffix = ''
endif

# Force libc++ when compiling on Linux
if compiler.get_id() == 'clang'
    # NOTE: according to https://mesonbuild.com/Reference-tables.html#compiler-ids,
    # compiler.get_id() probably returns "clang-cl" for Windows clang, so branching again
    # on the build machine target is probably not necessary, but this (hopefully) avoids
    # regressing the Windows build until it can be tested properly.
    if build_machine.system() != 'windows'
        add_project_arguments(['-stdlib=libc++'], language : 'cpp')
        add_project_link_arguments(['-stdlib=libc++'], language : 'cpp')
    endif
endif

# ---
# Source files

backend_source_files = files(
    'mollytime' / 'backend' / 'alsa_midi.cpp',
    'mollytime' / 'backend' / 'audio_backend.cpp',
    'mollytime' / 'backend' / 'colors.cpp',
    'mollytime' / 'backend' / 'errors.cpp',
    'mollytime' / 'backend' / 'jack_stream.cpp',
    'mollytime' / 'backend' / 'midi.cpp',
    'mollytime' / 'backend' / 'mmeapi_midi.cpp',
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
    'mollytime' / 'backend' / 'sdl_stream.cpp',
    'mollytime' / 'backend' / 'sdl_time.cpp',
    'mollytime' / 'backend' / 'thunks.cpp',
    'mollytime' / 'backend' / 'wasapi_stream.cpp'
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
dependencies += compiler.find_library('m', required : false)
dependencies += compiler.find_library('dl', required : false)

# Pybind11
dependencies += dependency('pybind11', required : true)

# Pyinstaller
pyi_makespec = find_program('pyi-makespec', required : true)
pyinstaller = find_program('pyinstaller', required : true)

# fmt
fmt_dir = 'third_party' / 'fmt-12.1.0'
fmt_include = fmt_dir / 'include'
fmt_include_dirs = include_directories(fmt_include)
fmt_source_files = files(fmt_dir / 'src' / 'format.cc')

fmt_dependency = declare_dependency(
    include_directories : fmt_include_dirs,
    link_with : static_library(
        'fmt',
        fmt_source_files,
        include_directories : fmt_include_dirs,
        override_options : [ 'warning_level=0' ]
    )
) 
dependencies += fmt_dependency

# GLM
glm_dir = 'third_party' / 'glm-1.0.3'

glm_dependency = declare_dependency(
    include_directories : include_directories(glm_dir)
) 
dependencies += glm_dependency

# Tracy
if get_option('tracy').enabled()
    tracy_dir = 'third_party' / 'tracy-0.13.1' / 'public'
    tracy_include_dirs = include_directories(tracy_dir)
    tracy_source_files = files(tracy_dir / 'TracyClient.cpp')
    
    tracy_dependency = declare_dependency(
        compile_args : '-DTRACY_ENABLE',
        dependencies : dependency('threads', required : false),
        include_directories : tracy_include_dirs,
        link_with : static_library(
            'tracy',
            tracy_source_files,
            include_directories : tracy_include_dirs,
            override_options : 'warning_level=0'
        )
    )
    dependencies += tracy_dependency
endif

# Boost
boost_install_info_command = run_command(
    python, 'build_tools' / 'boost_get_install_info.py', 'build' / 'boost',
    check : true
)
boost_install_info = boost_install_info_command.stdout().strip().splitlines()

boost_compile_args = []
boost_include_dir = ''

if get_option('boost_stacktrace').enabled()
    boost_compile_args += '-DENABLE_STACK_TRACES=1'
    if build_machine.system() == 'windows'
        boost_compile_args += '-DBOOST_STACKTRACE_USE_WINDBG=1'
    endif
endif

foreach line : boost_install_info
    if line.startswith('include_dir:')
        boost_include_dir = line.substring(12)
    endif
endforeach

boost_dependency = declare_dependency(
    compile_args : boost_compile_args,
    include_directories : include_directories(boost_include_dir),
)
dependencies += boost_dependency

# SDL3
sdl3_install_info_command = run_command(
    python, 'build_tools' / 'sdl3_get_install_info.py', 'build' / 'sdl3',
    check : true
)
sdl3_install_info = sdl3_install_info_command.stdout().strip().splitlines()

sdl3_include_dir = ''
sdl3_library_dir = ''
sdl3_binary_dir = ''

foreach line : sdl3_install_info
    if line.startswith('include_dir:')
        sdl3_include_dir = line.substring(12)
    elif line.startswith('library_dir:')
        sdl3_library_dir = line.substring(12)
    elif line.startswith('binary_dir:')
        sdl3_binary_dir = line.substring(11)
    endif
endforeach

sdl_dependency = declare_dependency(
    include_directories : include_directories(sdl3_include_dir),
    dependencies : compiler.find_library('SDL3', dirs : project_dir / sdl3_library_dir)
)
dependencies += sdl_dependency

if build_machine.system() == 'windows'
    fs.copyfile(sdl3_binary_dir / 'SDL3.dll')
endif

# SDL3_ttf
sdl3_ttf_install_info_command = run_command(
    python, 'build_tools' / 'sdl3_ttf_get_install_info.py', 'build' / 'sdl3_ttf',
    check : true
)
sdl3_ttf_install_info = sdl3_ttf_install_info_command.stdout().strip().splitlines()

sdl3_ttf_include_dir = ''
sdl3_ttf_library_dir = ''
sdl3_ttf_binary_dir = ''

foreach line : sdl3_ttf_install_info
    if line.startswith('include_dir:')
        sdl3_ttf_include_dir = line.substring(12)
    elif line.startswith('library_dir:')
        sdl3_ttf_library_dir = line.substring(12)
    elif line.startswith('binary_dir:')
        sdl3_ttf_binary_dir = line.substring(11)
    endif
endforeach

sdl_ttf_dependency = declare_dependency(
    include_directories : include_directories(sdl3_ttf_include_dir),
    dependencies : compiler.find_library('SDL3_ttf', dirs : project_dir / sdl3_ttf_library_dir)
)
dependencies += sdl_ttf_dependency

if build_machine.system() == 'windows'
    fs.copyfile(sdl3_ttf_binary_dir / 'SDL3_ttf.dll')
endif

# Audio backend
if get_option('audio_backend') == 'sdl'
    dependencies += declare_dependency(
        compile_args : '-DAUDIO_SDL=1',
        dependencies : sdl_dependency
    )
elif get_option('audio_backend') == 'jack'
    dependencies += declare_dependency(
        compile_args : '-DENABLE_JACK=1',
        dependencies : dependency('jack', required : true)
    )
elif get_option('audio_backend') == 'wasapi'
    dependencies += declare_dependency(
        compile_args : '-DAUDIO_WASAPI=1',
        dependencies : compiler.find_library('RuntimeObject', required : true)
    )
endif

# MIDI backend
if get_option('midi_backend') == 'alsa'
    dependencies += declare_dependency(
        compile_args : '-DMIDI_ALSA=1',
        dependencies : compiler.find_library('asound', required : true)
    )
elif get_option('midi_backend') == 'mmeapi'
    dependencies += declare_dependency(
        compile_args : '-DMIDI_MMEAPI=1',
        dependencies : compiler.find_library('Winmm', required : true)
    )   
endif


# ---
# Targets

# Compile and install the extension module in `<install root>/mollytime/`.
cpp_module = python_install.extension_module(
    'backend',
    backend_source_files,
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
