#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile
import os

ROOT=pathlib.Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as folder:
    binary=pathlib.Path(folder)/"sim_identity_scenarios"
    compile_result=subprocess.run([
        "g++","-std=c++17","-Wall","-Wextra","-Werror",
        "-I",str(ROOT/"host"),
        str(ROOT/"host"/"sim_identity_scenarios.cpp"),
        "-o",str(binary),
    ],capture_output=True,text=True)
    if compile_result.returncode:
        raise SystemExit(compile_result.stdout+compile_result.stderr)
    run_result=subprocess.run([str(binary)],capture_output=True,text=True)
    if run_result.returncode:
        raise SystemExit(run_result.stdout+run_result.stderr)
results=pathlib.Path(os.environ.get('SJKS_TEST_OUTPUT',ROOT.parent/'results/sim_identity'))
results.mkdir(parents=True,exist_ok=True)
(results/"sim-identity-results.txt").write_text(run_result.stdout)
print(run_result.stdout,end="")
