#!/usr/bin/env python3
"""Run reproducible Vim word-motion differential fuzz scenarios."""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))

from neovim_reference import run_reference
from scenarios import generate_scenarios, write_json


PATH_TEMP = Path(__file__).resolve().parents[1] / "tmp"
PATH_NVIM_LOG = Path(__file__).resolve().parents[2] / ".nvimlog"


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--texts", type=int, default=20, help="random texts to generate")
    parser.add_argument("--nvim", default=shutil.which("nvim"))
    parser.add_argument("--wine", default=shutil.which("wine"))
    parser.add_argument("--test-exe", required=True, type=Path)
    return parser.parse_args()


def clear_previous_run() -> None:
    if PATH_TEMP.exists():
        shutil.rmtree(PATH_TEMP)
    PATH_TEMP.mkdir(parents=True)
    PATH_NVIM_LOG.unlink(missing_ok=True)


def main() -> int:
    args = parse_args()
    clear_previous_run()
    if not args.nvim:
        print("Neovim was not found; pass --nvim with its executable path.", file=sys.stderr)
        return 2
    if not args.wine:
        print("Wine was not found; pass --wine with its executable path.", file=sys.stderr)
        return 2
    if args.texts < 1:
        print("--texts must be at least 1.", file=sys.stderr)
        return 2

    scenarios = generate_scenarios(args.seed, args.texts)
    try:
        expected_carets = run_reference(args.nvim, scenarios, PATH_TEMP / "neovim")
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2
    if len(expected_carets) != len(scenarios):
        print("Neovim returned a different number of results than scenarios.", file=sys.stderr)
        return 2
    for scenario, caret in zip(scenarios, expected_carets):
        scenario["expected_caret"] = caret

    path_test_exe = args.test_exe.resolve()
    path_cases = PATH_TEMP / "cases"
    path_reports = PATH_TEMP / "reports"
    path_cases.mkdir()
    write_json(path_cases / "generated.json", scenarios)
    completed = subprocess.run(
        [str(args.wine), str(path_test_exe), str(path_cases), str(path_reports)],
        cwd=path_test_exe.parent,
        capture_output=True,
        text=True,
        check=False,
    )
    path_report = path_reports / "functional-report.json"
    if not path_report.exists():
        sOutput = (completed.stderr or completed.stdout).strip()
        print(
            f"Fuzz runner failed with exit code {completed.returncode}: "
            f"{sOutput[-1000:]}",
            file=sys.stderr,
        )
        return completed.returncode or 2
    report = json.loads(path_report.read_text(encoding="utf-8"))
    scenario_by_id = {scenario["id"]: scenario for scenario in scenarios}
    failures = [result for result in report["results"] if result["status"] != "passed"]
    print(
        f"Fuzz seed {args.seed}: {report['summary']['passed']}/"
        f"{report['summary']['total']} motions passed across {args.texts} texts."
    )
    print(f"Inspectable run files: {PATH_TEMP}")
    for failure in failures:
        scenario = scenario_by_id[failure["id"]]
        print(
            f"FAIL {failure['id']}\n"
            f"  text: {scenario['initial_text']!r}\n"
            f"  starting caret: {scenario['initial_caret']}\n"
            f"  motion: {scenario['keys'][0]}\n"
            f"  Neovim result: text={failure['expected']['text']!r}, "
            f"caret={failure['expected']['caret']}\n"
            f"  Scintilla result: text={failure['actual']['text']!r}, "
            f"caret={failure['actual']['caret']}"
        )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
