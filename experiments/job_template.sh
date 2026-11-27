#!/bin/bash
#PBS -N hpc_exp_n{{select}}_{{mpiprocs}}x{{ompthreads}}
#PBS -j oe    # merges error into output
#PBS -q shortHPC4DS
#PBS -l walltime=10:00 # TODO: could be that we need to set this dynamically based on # of executions per qsub
#PBS -l mem=32gb
#PBS -l select={{select}}:ncpus={{ncpus}}:mpiprocs={{mpiprocs}}:ompthreads={{ompthreads}}
#PBS -l place={{place}}

cd $PBS_O_WORKDIR
module load MPICH/3.4.2-GCC-10.3.0 #TODO: add other modules + maybe move to justfile on login node (prof said something about not running module load on jobs, need to check)

{% for c in cases -%}
mpirun -np {{np}} --map-by ppr:{{mpiprocs}}:node:PE={{ompthreads}} --bind-to core {{bin_dir}}/{{c.algo}} {{c.n_items}} {{c.unique_items}} {{c.lg_k}} {{c.warmup_reps}} {{c.reps}}
{% endfor %}
