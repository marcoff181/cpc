SHELL := /bin/bash
.ONESHELL:
.SHELLFLAGS := -e -c


RUN_NAME := benchmark

MODULES := MPICH/3.4.2-GCC-10.3.0

###########################################################
# COMPILATION 
###########################################################
CC     := mpicc
# -lm ensures math.h is linked
CFLAGS := -O2 -Wall -Wextra -Wpedantic -Wconversion -lm
SRC    := main.c
# please keep the build directory hardcoded. I wouldn't
# want to set it to ./ (current project directory)
# by mistake and run `make clean` at the wrong time.
BIN    := executables/$(RUN_NAME)

# OTHERS
INPUT  ?= input/input.txt


###########################################################
# PBS info
###########################################################
JOB_NAME   := job_$(RUN_NAME)
JOB_SCRIPT := pbs_job.sh
WALLTIME   ?= 00:05:00
QUEUE      ?= shortHPC4DS
CHUNKS     ?= 2
NP         ?= 1
MEM        ?= 2
# pack, scatter, pack:excl, scatter:excl
PLACE      ?= pack

OUTPUT ?= results_$(PLACE)


.PHONY: all build submit status clean reset help

all: build
build: $(BIN)

$(BIN): $(SRC)
		module load $(MODULES)
		mkdir -p executables
		$(CC) $(CFLAGS) $(SRC) -o $(BIN)

submit: build
		mkdir -p "$(OUTPUT)"
		qsub \
			-N $(JOB_NAME) \
			-q $(QUEUE) \
			-l walltime=$(WALLTIME)
			-l select=$(CHUNKS):ncpus=$(NP):mem=$(MEM)gb \
			-l place=$(PLACE) \
			-v BIN=$(BIN),INPUT=$(INPUT) \
			-o "$(CURDIR)/$(OUTPUT)/" \
			-e "$(CURDIR)/$(OUTPUT)/" \
			$(JOB_SCRIPT)

status:
		qstat -u "$$USER"

clean:
		rm -rf executables

reset:
		rm -rf executables "$(OUTPUT)"

help:
		@echo "COMMAND         DESCRIPTION"
		@echo "make build      compile if needed"
		@echo "make submit     compile and submit job, move output to given dirs"
		@echo "make status     show your jobs"
		@echo "make clean      remove executables"
		@echo "make reset      remove executables and output"