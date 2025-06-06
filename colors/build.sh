clang++ \
    -std=c++2c \
    -lm \
    -I "glm-0.9.9.8" \
    -fPIC -shared -o colors.so \
    colors.cpp errors.cpp c_api.cpp
