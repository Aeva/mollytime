import re
import subprocess

from argparse import ArgumentParser
from pathlib import Path

parser = ArgumentParser(
    prog = "boost_get_install_info",
    description = "Build helper for Boost."
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
boost_dir = project_dir / "third_party" / "boost_1_90_0"

args = vars(parser.parse_args())
cmake = Path(args["cmake_path"])    # pyright: ignore[reportAny]
build_dir = Path(args["build_dir"]) # pyright: ignore[reportAny]

cmake_query_process = subprocess.run(
    [
        cmake,
        "-S", boost_dir,
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

install_dir = variables["CMAKE_INSTALL_PREFIX"]
include_dir = variables["CMAKE_INSTALL_INCLUDEDIR"]
include_dir_boost_thing = variables["BOOST_INSTALL_INCLUDE_SUBDIR"].strip("/\\")

install_dir = Path(install_dir).relative_to(project_dir)
include_dir = install_dir / include_dir / include_dir_boost_thing

print(f"include_dir:{include_dir}")