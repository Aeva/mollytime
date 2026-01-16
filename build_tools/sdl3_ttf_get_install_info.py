import re
import subprocess

from argparse import ArgumentParser
from pathlib import Path

parser = ArgumentParser(
    prog = "sdl3_ttf_get_install_info",
    description = "Build helper for SDL3_ttf."
)
_ = parser.add_argument(
    "--cmake-path",
    help = "Path to a CMake executable. If unspecified, it has to be on PATH.",
    default = "cmake"
)
_ = parser.add_argument(
    "build_dir",
    help = "Where intermediate files for the build are stashed."
)

this_dir = Path(__file__).parent
project_dir = this_dir.parent
sdl3_ttf_dir = project_dir / "third_party" / "SDL3_ttf-3.2.2"

args = vars(parser.parse_args())
cmake = Path(args["cmake_path"])    # pyright: ignore[reportAny]
build_dir = Path(args["build_dir"]) # pyright: ignore[reportAny]

cmake_query_process = subprocess.run(
    [
        cmake,
        "-S", sdl3_ttf_dir,
        "-B", build_dir,
        "-N",
        "-LA"
    ],
    capture_output = True
)
cmake_query_process.check_returncode()

variable_pattern = re.compile(r"(?P<name>.*?)\:\w+\=(?P<value>.*)")
variable_strings = cmake_query_process.stdout.decode().splitlines()
variable_matches = ( variable_pattern.match(line) for line in variable_strings )
variables = {
    match.group("name") : match.group("value")
    for match in variable_matches
    if match != None
}

# print(variables["CMAKE_INSTALL_PREFIX"])
install_dir = variables["CMAKE_INSTALL_PREFIX"]
include_dir = variables["CMAKE_INSTALL_INCLUDEDIR"]
library_dir = variables["CMAKE_INSTALL_LIBDIR"]
binary_dir = variables["CMAKE_INSTALL_BINDIR"]

install_dir = Path(install_dir).relative_to(project_dir)
include_dir = install_dir / include_dir
library_dir = install_dir / library_dir
binary_dir = install_dir / binary_dir

print(f"include_dir:{include_dir}")
print(f"library_dir:{library_dir}")
print(f"binary_dir:{binary_dir}")