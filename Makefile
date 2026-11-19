SHELL := /bin/bash
.ONESHELL:
.SHELLFLAGS := -e -c


MODULES := MPICH/3.4.2-GCC-10.3.0


RUN_NAME ?= runname


###########################################################
# COMPILATION
###########################################################

# Run `mpicc` when using MPI
CC ?= cc

# -lm ensures math.h is linked
# Add -fopenmp when using OpenMP
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -Wconversion -lm

# please keep the build directory hardcoded. I don't
# want to set it to ./ (current project directory)
# by mistake and run `make clean` at the wrong time.
BIN := executables/$(RUN_NAME)

LG_K ?= 4
INPUT_MODE ?= stream

OUTPUT ?= results/$(RUN_NAME)


###########################################################
# PBS info
###########################################################

JOB_NAME       := job_$(RUN_NAME)
WALLTIME       ?= 00:05:00
QUEUE          ?= shortHPC4DS
CHUNKS         ?= 1
NP             ?= 1
MEM            ?= 2
# pack, scatter, pack:excl, scatter:excl
PLACE          ?= pack


.PHONY: all build submit status clean reset help

all:
		@echo "Run 'make help'"

build: $(BIN)

$(BIN): $(SRC)
		module load $(MODULES)
		mkdir -p executables
		$(CC) $(CFLAGS) $(SRC) -o $(BIN)

submit: build
		if [ ! -f "$(INPUT_FILE)" ]; then
				echo "Something wrong with INPUT_FILE: '$(INPUT_FILE)'" >&2
				exit 1
		fi
		mkdir -p "$(OUTPUT)"
		qsub \
			-N $(JOB_NAME) \
			-q $(QUEUE) \
			-l walltime=$(WALLTIME) \
			-l select=$(CHUNKS):ncpus=$(NP):mem=$(MEM)gb \
			-l place=$(PLACE) \
			-v "BIN=$(BIN),INPUT_FILE=$(INPUT_FILE),LG_K=$(LG_K),INPUT_MODE=$(INPUT_MODE)" \
			-o "$(CURDIR)/$(OUTPUT)/" \
			-e "$(CURDIR)/$(OUTPUT)/" \
			$(JOB_SCRIPT)

status:
		qstat -u "$$USER"

clean:
		rm -rf executables

reset:
		rm -rf executables results

help:
		@echo "COMMAND         DESCRIPTION"
		@echo "make build      compile if needed"
		@echo "make submit     compile and submit job, move output to given dirs"
		@echo "                sequential input mode: INPUT_MODE=stream (default) or INPUT_MODE=preload"
		@echo "make status     show your jobs"
		@echo "make clean      remove executables"
		@echo "make reset      remove executables and output"
