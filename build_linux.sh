cd src
clang++ \
    -std=c++2c \
    -lm \
    -I "../third_party/glm-0.9.9.8" \
    $(python -m pybind11 --includes) \
    -fPIC -shared \
    colors.cpp errors.cpp py_api.cpp \
    -o ../mollytime/mollytime$(python3-config --extension-suffix)
