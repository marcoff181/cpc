#!/usr/bin/env python3
"""
usage: gen.py scaling.toml job_template.sh results/scaling/<ts>

[pbs.product] -> cartesian product -> one rendered .sh (one PBS job) per row
[mpirun.product] -> cartesian product -> generates lines of mpi run
[pbs.derive] / [cases.derive]: {key: "expression"} evaluated per row, in the
    order the keys appear in the TOML

see job_template.sh to understand expected template structure
"""
import itertools, sys, tomllib
from pathlib import Path

def product(table):
    keys = list(table)
    return [dict(zip(keys, vals)) for vals in itertools.product(*table.values())]

def derive(rows, exprs):
    for row in rows:
        for key, expr in exprs.items():  # TOML order -> later exprs may use earlier keys
            row[key] = eval(expr, {"__builtins__": {}}, row)
    return rows

toml_path, out = sys.argv[1:3]
cfg = tomllib.load(open(toml_path, "rb"))
out = Path(out)
out.mkdir(parents=True, exist_ok=True)

bin_dir = cfg["bin_dir"]
pbs_rows = derive(product(cfg["pbs"]["product"]), cfg["pbs"].get("derive", {}))
mpirun_rows = derive(product(cfg["mpirun"]["product"]), cfg["mpirun"].get("derive", {}))

manifest = []
header = None
for pbs in pbs_rows:
    job_name = cfg["pbs"]["directives"]["N"].format(**pbs)

    lines = ["#!/bin/bash"]
    lines += [f"#PBS -{k} {v.format(**pbs)}" for k, v in cfg["pbs"]["directives"].items()]
    lines.append(f"cd {out.resolve()}") # cd into the results/experimentname directory so we keep the outputs of the pbs scripts there
    lines += cfg["init"]["commands"]
    lines.append("")

    for case in mpirun_rows:
        row = {**pbs, "bin_dir": bin_dir, **case}
        lines.append(cfg["mpirun"]["command"].format(**row))

        if header is None:
            header = ["job", *row]
            manifest.append("\t".join(header))
        manifest.append("\t".join([job_name] + [str(row[k]) for k in header[1:]]))

    script = out / f"{job_name}.sh"
    script.write_text("\n".join(lines) + "\n")
    script.chmod(0o755)

(out / "manifest.tsv").write_text("\n".join(manifest) + "\n")
print(out)
