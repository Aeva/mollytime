project(
    'boost',
    'cpp',
    license : 'BSL-1.0',
    license_files: [ 'LICENSE_1_0.txt' ],
    version : '1.90.0',
    meson_version : '>=1.3.0'
)

py = import('python')
python = py.find_installation()

# Ensure all desired modules & their dependencies are available.
desired_modules = get_option('modules')
run_command(python, 'get_boost.py', desired_modules, check : true)

# Get their names as a cleanly-parseable list.
required_modules = run_command(
    python, 'find_dependencies.py', desired_modules,
    capture : true,
    check : true
).stdout()
required_modules = required_modules.strip().splitlines()

# Boost, mercifully, standardizes library layout.
# We can be sure that any libraries we want have their headers at `libs/the_module/include`.
boost_headers = []
foreach module : required_modules
    boost_headers += include_directories('libs' / module / 'include')
endforeach

# Currently, we only provide headers.
# I have an idea for generically building libraries, but I'll save it for if/when we need it.
boost_dep = declare_dependency(
    include_directories : boost_headers
)