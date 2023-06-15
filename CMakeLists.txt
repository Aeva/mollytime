cmake_minimum_required(VERSION 3.24)
project(mollytime
	DESCRIPTION "MIDI tracker")

include(GNUInstallDirs)

#######################################################################
## Build options:

set(INSTALL_PKG_SUBPATH "mollytime" CACHE PATH
	"Subdirectory to form PKGDATADIR from DATADIR")
mark_as_advanced(INSTALL_PKG_SUBPATH)

#######################################################################
## Required dependencies:

find_package(ALSA REQUIRED)
find_package(OpenGL REQUIRED COMPONENTS OpenGL)
find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 17)

#######################################################################
##+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++##
#######################################################################
## Third-Party Libraries:

#######################################################################
## fmt:

add_library(fmt OBJECT "external/fmt/src/format.cc")
target_include_directories(fmt PUBLIC "external/fmt/include")

#######################################################################
## GLM:

add_subdirectory("external/glm")

#######################################################################
## ImGui:

file(GLOB MOLLYTIME_IMGUI_FILES CONFIGURE_DEPENDS
	"external/imgui/*.cpp"
	"external/imgui/backends/imgui_impl_opengl2.cpp"
	"external/imgui/backends/imgui_impl_sdl2.cpp")
add_library(imgui OBJECT "${MOLLYTIME_IMGUI_FILES}")
target_link_libraries(imgui PUBLIC SDL2::SDL2)
# some complexity to make both <imgui/imgui.h> and "imgui.h" work
target_include_directories(imgui PUBLIC
	$<BUILD_INTERFACE:$<PATH:APPEND,${CMAKE_BINARY_DIR},imgui-include-dir>>
	$<BUILD_INTERFACE:$<PATH:APPEND,${CMAKE_BINARY_DIR},imgui-include-dir/imgui>>
	$<BUILD_INTERFACE:$<PATH:APPEND,${CMAKE_BINARY_DIR},imgui-include-dir/imgui/backends>>
	$<INSTALL_INTERFACE:"${CMAKE_INSTALL_FULL_INCLUDEDIR}">)
file(GLOB MOLLYTIME_IMGUI_HEADERS CONFIGURE_DEPENDS "external/imgui/*.h")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/imgui-include-dir/imgui/backends")
foreach(X IN ITEMS "external/imgui/backends/imgui_impl_opengl2.h"
			"external/imgui/backends/imgui_impl_sdl2.h")
	configure_file("${X}" "imgui-include-dir/imgui/backends" COPYONLY)
endforeach()
foreach(X IN LISTS MOLLYTIME_IMGUI_HEADERS)
	configure_file("${X}" "imgui-include-dir/imgui")
endforeach()

#######################################################################
## ImFileDialog:

add_library(ImFileDialog OBJECT
	"external/ImFileDialog/ImFileDialog.cpp"
	"external/stb/stb.cpp")
target_link_libraries(ImFileDialog PUBLIC imgui)
target_include_directories(ImFileDialog
	PUBLIC "external/ImFileDialog" #/ImFileDialog.h"
	PRIVATE "external/stb") #/stb_image.h")

#######################################################################
##+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++##
#######################################################################
## The executable:

file(GLOB_RECURSE MOLLYTIME_FILES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(mollytime ${MOLLYTIME_FILES})
set_target_properties(mollytime
	PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY
	$<PATH:APPEND,${CMAKE_BINARY_DIR},${CMAKE_INSTALL_BINDIR}>)
target_compile_definitions(mollytime PRIVATE
	"MOLLYTIME_PKGDATADIR_FROM_BINDIR=${PKGDATADIR_FROM_BINDIR}")
target_link_libraries(mollytime PRIVATE
	ALSA::ALSA
	OpenGL::OpenGL
	fmt
	glm
	imgui
	ImFileDialog
	tinfo # see https://gitlab.kitware.com/cmake/cmake/-/issues/23236
	${CMAKE_THREAD_LIBS_INIT}
	${CMAKE_DL_LIBS})
install(TARGETS mollytime)

