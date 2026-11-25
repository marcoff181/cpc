init:
    cmake -B build

build:
    cmake --build build --parallel

run:
    # ./build/spmv

clean:
    rm -rf build
