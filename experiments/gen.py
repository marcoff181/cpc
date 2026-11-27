#!/usr/bin/env python3
"""
usage: gen.py scaling.toml job_template.sh results/scaling/<ts>

[pbs.product] -> cartesian product -> one rendered .sh (one PBS job) per row
[cases.product] -> cartesian product -> generates lines of mpi run
[pbs.derive] / [cases.derive]: {key: "expression"} evaluated per row, in the
    order the keys appear in the TOML

see job_template.sh to understand expected template structure
"""
import itertools, sys, tomllib
from pathlib import Path
import jinja2

def product(table):
    keys = list(table)
    return [dict(zip(keys, vals)) for vals in itertools.product(*table.values())]

def derive(rows, exprs):
    for row in rows:
        for key, expr in exprs.items():  # TOML order -> later exprs may use earlier keys
            row[key] = eval(expr, {"__builtins__": {}}, row)
    return rows

toml_path, template_path, out = sys.argv[1:4]
cfg = tomllib.load(open(toml_path, "rb"))
out = Path(out)
out.mkdir(parents=True, exist_ok=True)

bin_dir = cfg.get("bin_dir", "build/algorithms")
template = jinja2.Template(Path(template_path).read_text(), trim_blocks=True, lstrip_blocks=True)

pbs_rows = derive(product(cfg["pbs"]["product"]), cfg["pbs"].get("derive", {}))
case_rows = derive(product(cfg["cases"]["product"]), cfg["cases"].get("derive", {}))

header, manifest, qsub = None, [], []
for pbs in pbs_rows:
    name = f"n{pbs['select']}_{pbs['mpiprocs']}x{pbs['ompthreads']}_{pbs['place']}"
    script = out / f"{name}.sh"
    script.write_text(template.render(bin_dir=bin_dir, cases=case_rows, **pbs))
    script.chmod(0o755)
    qsub.append(f"qsub {script}")

    for i, case in enumerate(case_rows):
        row = {"job": name, **pbs, "case": i, **case}
        if header is None:
            header = list(row)
            manifest.append("\t".join(header))
        manifest.append("\t".join(str(row[k]) for k in header))

(out / "manifest.tsv").write_text("\n".join(manifest) + "\n")
(out / "qsub.sh").write_text("\n".join(qsub) + "\n")
print(out)
