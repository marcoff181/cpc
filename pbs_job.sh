#!/bin/bash

cd "$PBS_O_WORKDIR"
module load MPICH/3.4.2-GCC-10.3.0
mpiexec -n "$PBS_NP" "$HOME"/mpi_wrapper.sh "./$BIN" "$INPUT"
