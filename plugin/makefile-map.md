# Makefile map

A visual companion to [makefile-commands.md](makefile-commands.md). From the
repository root, run `make -C plugin <target>`. Arrows show what each target
uses or launches; paths below are relative to `plugin/`.

## Build and launch

```mermaid
flowchart TD
    ALL["all (default target)"] --> NATIVE["native"] & NVIM["nvim"]
    NATIVE --> NFILES["Native DLLs + Scintilla files"]
    NVIM --> VFILES["Neovim host + DLLs + Scintilla files"]
    NVIM --> STAGE["stagenvim: copy Neovim distribution"]
    RSN["runscintillanative"] --> NATIVE & NHOST["Native host"]
    RSV["runscintillanvim"] --> NVIM
    IN["installnative"] --> NATIVE
    IV["installnvim"] --> NVIM
    RNN["runnotepadnative"] --> IN & NP["Notepad++"]
    RNV["runnotepadnvim"] --> IV & NP
```

The native build defaults to `build/windows-x64/both/`; the Neovim build uses
`build/windows-x64/neovim-build/`. `BACKENDS` changes the native directory
suffix, not which backend a target builds.

## Test, fuzz, and replay

```mermaid
flowchart LR
    TN["test / testnative"] --> RT["run-tests"]
    TV["testnvim"] --> RT
    RT --> FORMAT["Format all JSON files below tests/cases/"]
    RT --> CASES["TEST_PATH: file or directory"]
    RT --> REPORT["TEST_REPORT_DIR: text + JSON reports"]
    FN["fuzznative / fuzznvim"] --> FUZZ["Differential fuzz runner"]
    FUZZ --> REF["Local Neovim reference"] & TEMP["tests_fuzzing/tmp/"]
    RN["replaynative / replaynvim"] --> HOST["Selected host"]
    RN --> CASE["CASE: required case ID"]
    RN --> FILE["REPLAY_CASES: JSON file"]
```

`WAIT_MS` controls the fixed delay before each functional result is checked;
`VISIBLE=1` shows the test window. The test runner writes to
`tests/functional/tmp/reports/` by default. Fuzzing has its own temporary
directory and replaces the previous fuzz run. Focused checks under
`tests/neovim/` run through `ci-portable`, `ci-nvim-build`, and
`ci-nvim-integration`. See the
[test guide](tests/README.md#focused-neovim-checks) for their commands and coverage.

Replay requires `CASE=<id>`; `REPLAY_CASES=<json-file>` defaults to
`tests_fuzzing/tmp/cases/generated.json`. `replay` selects the native backend.
For the shared `run-replay` target, `REPLAY_APP` optionally selects an
already-built host. See the [replay examples](makefile-commands.md#replay-a-test-case).

Backend-specific test, fuzz, and replay targets prepare their runtime files
before invoking the shared recipes; Neovim targets also stage Neovim.
The shared `run-tests`, `run-fuzz`,
`run-fuzz-seed`, and `run-replay` targets do not independently prepare every
backend dependency.
