init:
    cmake -B build

build:
    cmake --build build --parallel

run:
    # ./build/spmv

clean:
    rm -rf build

ts := `date +%Y-%m-%d_%H-%M-%S`

exp-gen exp:
    python3 experiments/gen.py experiments/{{exp}}.toml results/{{exp}}_{{ts}}

exp-run exp: (exp-gen exp)
    cd results/{{exp}}_{{ts}} && for f in *.sh; do qsub "$f"; done

