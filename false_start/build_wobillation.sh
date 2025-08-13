clang++ \
    -std=c++2c \
    -lm \
    $(pkg-config --cflags --libs libpipewire-0.3) \
    -fPIC -shared -o wobillation.so \
    wobillation.cpp
