# Differential fuzz tests

The fuzz runner generates editing sequences and compares the standalone Vim
integration with headless Neovim. It uses the same Scintilla harness as the
[functional tests](../tests/README.md). It does not load the Notepad++ plugin DLL.
The comparison has limits described below, so failures need manual review.

## Running

From the repository root:

```sh
make -C plugin fuzz
make -C plugin fuzz-seed SEED=42
make -C plugin fuzznvim
```

`fuzz` and `fuzz-seed` select the native backend; `fuzznvim` selects Neovim.
These targets build the Windows test executable and runtime DLLs, then run the
Python driver. They require Python 3, local Neovim, Wine, and the MinGW x86-64
C++ compiler. The Makefile expects a compatible Windows x64 DLL at
`plugin/dependencies/scintilla/Scintilla.dll`. Neovim-backend runs also need a
Windows Neovim distribution. See the
[Makefile commands](../makefile-commands.md#fuzzing) for all backend-specific targets.

The default seed is `1` and the default run generates 20 texts. To choose the
number of texts or executable paths, run the driver directly after building:

```sh
python3 plugin/tests_fuzzing/differential/run_fuzz.py \
  --seed 42 --texts 50 --test-exe plugin/build/windows-x64/both/VimMotionsTests.exe
```

Use `--nvim` and `--wine` to specify executable paths when they are not on `PATH`.

## Current coverage and limitations

The generator produces ASCII text with CRLF line endings and a starting caret.
For each text it combines `d`, `c`, and `y` with each of `h`, `j`, `k`, `l`, `w`,
`W`, `b`, `B`, `e`, `E`, `0`, `$`, `G`, and `gg`, followed by `p` or `P`. Some
sequences have a leading count from 2 through 6. The seed makes inputs repeatable.

Neovim runs each sequence with `normal!` and returns a final caret offset.
The harness compares that offset and the actual text against a generated case.
**The expected text is the original input, not Neovim's resulting text.**
Text-changing sequences therefore do not provide a complete comparison. The
reference also calculates offsets using the original lines, so edits that
change line structure can make its caret offsets unreliable. A failure alone
does not establish a compatibility bug. Review the generated cases and reports;
the console's “Neovim result” text label still refers to the original input.

The driver does not pass a wait option to the Windows harness. The harness
therefore checks results immediately, which can produce timing-related failures
with the asynchronous Neovim backend. The Makefile's `WAIT_MS` option applies to
functional tests and is not forwarded to fuzzing.

The reference converts CRLF to LF for Neovim's buffer and adjusts byte offsets
for CRLF in the Scintilla fixture. Generated input is ASCII, so this does not
test Unicode positions.

## Generated files

Each run replaces `plugin/tests_fuzzing/tmp/` and removes `plugin/.nvimlog` if
present. Cleanup happens before dependency and argument validation, so even a
setup failure can remove the previous run. The temporary directory contains:

- `neovim/scenarios.json`, `reference.lua`, and `results.json`: reference inputs, script, and caret offsets.
- `cases/generated.json`: cases passed to the Windows runner.
- `reports/functional-report.json` and `functional-report.txt`: results and diagnostics.

The driver returns a failure status for mismatches and reports setup errors
separately. Preserve any files you need before starting another run.
