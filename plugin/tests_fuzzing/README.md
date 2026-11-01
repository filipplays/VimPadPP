# Differential fuzz tests

The fuzz runner generates editor scenarios and compares the standalone Vim integration's results with a headless Neovim reference. It uses the same Scintilla test harness as the [functional tests](../tests/README.md); it does not load the Notepad++ plugin DLL.

## Running

From the `plugin/` directory:

```sh
make fuzz
make fuzz-seed SEED=42
```

These targets build the Windows test executable and runtime DLLs, then run the Python driver. They require Python 3, Neovim, Wine, and the MinGW x86-64 C++ compiler. The Makefile expects a compatible Windows x64 DLL at `dependencies/scintilla/Scintilla.dll`. See the [Makefile commands](../makefile-commands.md#fuzzing-and-replay) for backend-specific targets.

The default seed is `1` and the default run generates 20 texts. To choose the number of texts or executable paths, run the driver directly after building:

```sh
python3 tests_fuzzing/differential/run_fuzz.py \
  --seed 42 --texts 50 --test-exe build/windows-x64/both/VimMotionsTests.exe
```

Use `--nvim` and `--wine` to specify executable paths when they are not on `PATH`.

## Current coverage and limitations

The generator produces ASCII text with CRLF line endings and a starting caret. For each text it combines `d`, `c`, and `y` with each of `h`, `j`, `k`, `l`, `w`, `W`, `b`, `B`, `e`, `E`, `0`, `$`, `G`, and `gg`, followed by `p` or `P`. Some sequences have a leading count from 2 through 6. The seed makes generated inputs repeatable.

Neovim runs each sequence with `normal!` and returns a final caret offset. The harness compares that offset and the actual text against a generated case. **The expected text is currently the original input, not Neovim's resulting text.** Consequently, text-changing sequences cannot be treated as a complete differential comparison, and a failure does not by itself establish a Neovim compatibility bug. The reference also converts offsets using the original lines, so edits that change line structure can make its caret offsets unreliable. Review the generated cases and reports when interpreting results.

The reference converts CRLF to LF for Neovim's in-memory buffer and adjusts byte offsets for CRLF in the Scintilla fixture. The generated input is ASCII; this does not test Unicode position handling.

## Generated files

Each run replaces `tests_fuzzing/tmp/` and removes the project-root `.nvimlog` if present. The temporary directory contains:

- `neovim/scenarios.json`, `reference.lua`, and `results.json`: reference inputs, script, and caret offsets.
- `cases/generated.json`: cases passed to the Windows runner.
- `reports/functional-report.json` and `functional-report.txt`: results and diagnostics.

The driver returns a failure status for mismatches and reports setup errors separately. Preserve any files you need before starting another run.
