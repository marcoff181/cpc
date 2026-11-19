#!/bin/bash

cd "$PBS_O_WORKDIR" || exit 1

exec "./$BIN" "$INPUT_FILE" "$LG_K" "--${INPUT_MODE-stream}"
