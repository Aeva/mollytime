
BUILD_DIR := build
SRC_DIR := src

OBJECT_FILES = $(BUILD_DIR)/%.o
SOURCE_FILES = $(SRC_DIR)/%.cpp

TARGET_LIB := mollytime/mollytime$(shell python3-config --extension-suffix)

COMMON_ARGS := -std=c++2c -fPIC
INCLUDE_GLM := -I "third_party/glm-0.9.9.8"
INCLUDE_PYTHON := $(shell python -m pybind11 --includes)

all: $(BUILD_DIR)/errors.o $(BUILD_DIR)/colors.o $(BUILD_DIR)/py_api.o $(TARGET_LIB)

clean:
	rm -f $(BUILD_DIR)/*.o
	rm -f $(TARGET_LIB)

$(OBJECT_FILES): $(SOURCE_FILES)
	mkdir -p build
	clang++ $(COMMON_ARGS) $(INCLUDE_GLM) $(INCLUDE_PYTHON) -c $< -o $@

$(TARGET_LIB):
	clang++ $(COMMON_ARGS) -lm -shared $(BUILD_DIR)/*.o -o $(TARGET_LIB)
