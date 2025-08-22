
BUILD_DIR := build
SRC_DIR := src

OBJECT_FILES = $(BUILD_DIR)/%.o
SOURCE_FILES = $(SRC_DIR)/%.cpp

OBJECT_TARGETS = $(shell find $(SRC_DIR) -name '*.cpp' | sed -e 's/$(SRC_DIR)/$(BUILD_DIR)/g' -e 's/.cpp/.o/g')

TARGET_LIB := mollytime/mollytime$(shell python3-config --extension-suffix)

PERF_MODE_ARGS := -g -O2 -fno-omit-frame-pointer
DEBUG_MODE_ARGS := -g -O0 -fno-omit-frame-pointer #-fsanitize=address -fno-optimize-sibling-calls
RELEASE_MODE_ARGS := -O2
ENABLE_DEBUG := #uncomment me to enable debugging
ENABLE_STACK_TRACES := uncomment me to enable stacktraces.  requires boost-stacktrace
ENABLE_PERF := #uncomment me to enable perf instrumentation (tracy)

ENABLE_JACK := uncomment to enable jack

ENABLE_PSMOVE := uncomment to enable psmove, requires `psmoveapi-devel` to be installed

TRACY_DIR := third_party/tracy-0.12.2/public
INCLUDE_TRACY := -I "$(TRACY_DIR)"
TRACY_SOURCE := $(TRACY_DIR)/TracyClient.cpp
TRACY_OBJECT := $(BUILD_DIR)/TracyClient.o
TRACY_TARGET := $(if $(ENABLE_PERF),$(TRACY_OBJECT),)

INSTRUMENTATION := \
	$(if $(ENABLE_DEBUG),$(DEBUG_MODE_ARGS),$(if $(ENABLE_PERF),$(PERF_MODE_ARGS),$(RELEASE_MODE_ARGS))) \
	$(if $(ENABLE_STACK_TRACES),-DENABLE_STACK_TRACES,) \
	$(if $(ENABLE_PERF),$(INCLUDE_TRACY) -DTRACY_ENABLE,)

COMMON_ARGS := -std=c++2c -fPIC $(INSTRUMENTATION) $(if $(ENABLE_JACK),-DENABLE_JACK,) -DMIDI_ALSA \
	-Wall -Wextra -Wshadow -pedantic -Wno-unused-parameter -Wno-unknown-pragmas

INCLUDE_GLM := -I "third_party/glm-0.9.9.8"
INCLUDE_PYTHON := $(shell python -m pybind11 --includes)
INCLUDE_JACK := $(shell pkg-config --cflags jack)
INCLUDE_AUDIO := $(if $(ENABLE_JACK), $(INCLUDE_JACK),)
INCLUDE_PSMOVE := $(shell pkg-config --cflags psmoveapi)
INCLUDE_OPTIONAL := $(if $(ENABLE_PSMOVE), $(INCLUDE_PSMOVE),)
LIB_JACK := $(shell pkg-config --libs jack)
LIB_AUDIO := $(if $(ENABLE_JACK), $(LIB_JACK),)
LIB_PSMOVE := $(shell pkg-config --libs psmoveapi)
LIBRARIES := -lm -lasound $(LIB_AUDIO) $(if $(ENABLE_PERF),-lpthread -ldl,) $(if $(ENABLE_PSMOVE), $(LIB_PSMOVE),)

all: $(OBJECT_TARGETS) $(TRACY_TARGET) $(TARGET_LIB)

clean:
	rm -f $(BUILD_DIR)/*.o
	rm -f $(TARGET_LIB)

$(OBJECT_FILES): $(SOURCE_FILES)
	mkdir -p build
	clang++ $(COMMON_ARGS) $(INCLUDE_GLM) $(INCLUDE_PYTHON) $(INCLUDE_AUDIO) $(INCLUDE_OPTIONAL) -c $< -o $@

$(TRACY_OBJECT): $(TRACY_SOURCE)
	mkdir -p build
	clang++ $(COMMON_ARGS) $(INCLUDE_TRACY) -c $(TRACY_SOURCE) -o $(TRACY_OBJECT)

$(TARGET_LIB): $(OBJECT_TARGETS)
	clang++ $(COMMON_ARGS) $(LIBRARIES) -shared $(BUILD_DIR)/*.o -o $(TARGET_LIB)
