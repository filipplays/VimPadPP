"""Evaluate word motions in headless Neovim and return caret byte offsets."""

import json
import subprocess
from pathlib import Path


def run_reference(
    nvim: str, scenarios: list[dict[str, object]], path_dir: Path
) -> list[int]:
    path_dir.mkdir(parents=True)
    path_input = path_dir / "scenarios.json"
    path_output = path_dir / "results.json"
    path_lua = path_dir / "reference.lua"
    aNvimScenarios = []
    for sScenario in scenarios:
        sText = str(sScenario["initial_text"])
        nCaret = int(sScenario["initial_caret"])
        aNvimScenarios.append(
            {
                "initial_text": sText.replace("\r\n", "\n"),
                "initial_caret": nCaret - sText[:nCaret].count("\r"),
                "keys": sScenario["keys"],
            }
        )
    path_input.write_text(json.dumps(aNvimScenarios), encoding="utf-8")
    path_lua.write_text(
        "\n".join(
            [
                f"local f = assert(io.open({json.dumps(str(path_input))}, 'r'))",
                "local scenarios = vim.json.decode(f:read('*a'))",
                "f:close()",
                "local results = {}",
                "for i, scenario in ipairs(scenarios) do",
                "  local lines = vim.split(scenario.initial_text, '\\n', { plain = true })",
                "  vim.api.nvim_buf_set_lines(0, 0, -1, true, lines)",
                "  local remaining = scenario.initial_caret",
                "  local row = 1",
                "  while row <= #lines and remaining > #lines[row] do",
                "    remaining = remaining - #lines[row] - 1",
                "    row = row + 1",
                "  end",
                "  local col = remaining",
                "  vim.api.nvim_win_set_cursor(0, { row, col })",
                "  vim.cmd('normal! ' .. table.concat(scenario.keys))",
                "  local cursor = vim.api.nvim_win_get_cursor(0)",
                "  local offset = 0",
                "  for line = 1, cursor[1] - 1 do offset = offset + #lines[line] + 2 end",
                "  results[i] = offset + cursor[2]",
                "end",
                f"local out = assert(io.open({json.dumps(str(path_output))}, 'w'))",
                "out:write(vim.json.encode(results))",
                "out:close()",
                "vim.cmd('qa!')",
            ]
        )
        + "\n",
        encoding="utf-8",
    )
    completed = subprocess.run(
        [
            nvim,
            "--headless",
            "-u",
            "NONE",
            "-i",
            "NONE",
            "-n",
            "-c",
            f"lua dofile({json.dumps(str(path_lua))})",
        ],
        capture_output=True,
        text=True,
        check=False,
        cwd=path_dir,
    )
    if completed.returncode != 0 or not path_output.exists():
        raise RuntimeError(
            "Neovim reference run failed: "
            + (completed.stderr or completed.stdout).strip()
        )
    return json.loads(path_output.read_text(encoding="utf-8"))
