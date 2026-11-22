# Makefile commands

Run these commands from the repository root:

```sh
make -C plugin native
```

Use `make -C plugin -n <target>` to preview commands without running them.
Relative paths passed to Make are resolved from `plugin/`.

## Prerequisites

- The `x86_64-w64-mingw32-g++` cross-compiler builds Windows x64 binaries.
- Wine runs the Windows executables; Python 3 formats fixtures and runs fuzzing.
- `dependencies/scintilla/` contains `Scintilla.dll` and `License.txt`.
- Neovim targets also need a Windows Neovim distribution at
  `dependencies/neovim/`, including `bin/nvim.exe`.
- Fuzzing additionally needs a local `nvim` executable as the reference.

Override the dependency paths with `SCINTILLA_DIR` and `NEOVIM_DIR` if needed.
Relative paths are resolved from `plugin/`.

## Build and run

| Command | What it does |
| --- | --- |
| `make -C plugin` or `make -C plugin all` | Build both backend sets. |
| `make -C plugin native` | Build the native DLLs and copy the Scintilla dependencies. |
| `make -C plugin nvim` | Build the Neovim host and DLLs and stage its dependencies. |
| `make -C plugin runscintillanative` | Build and launch the native Scintilla host. |
| `make -C plugin runscintillanvim` | Build and launch the Neovim Scintilla host. |
| `make -C plugin installnative` / `make -C plugin installnvim` | Build and install the selected Notepad++ plugin. |
| `make -C plugin runnotepadnative` / `make -C plugin runnotepadnvim` | Install the selected plugin, then launch Notepad++. |
| `make -C plugin runnotepad` | Launch the configured Notepad++ without building or installing. |
| `make -C plugin runscintilla` | Launch an existing host without building it. |

For `runscintilla`, `RUN_SCINTILLA_APP` selects the executable; its default is
the native host in the selected build directory. `stagenvim` copies the Neovim
distribution into the Neovim build directory.

Both staging and installation preserve existing `neovim/init.lua`,
`neovim/init.vim`, and `neovim/vim-motions-init.lua`. Missing init files are
copied on first setup; other distribution files are updated normally.
To update the bundled bootstrap deliberately, copy `src/neovim_init.lua` to
the desired `neovim/vim-motions-init.lua` yourself. The separate `config/`
directory is not copied or overwritten by these targets.

The install targets write to `NOTEPADPP_PLUGIN_DIR`; `runnotepad` uses
`NOTEPADPP_EXE`. Override either path as needed:

```sh
make -C plugin installnvim NOTEPADPP_PLUGIN_DIR='/path/to/Notepad++/plugins/VimPad++'
make -C plugin runnotepad NOTEPADPP_EXE='/path/to/notepad++.exe'
```

Restart an already-running Notepad++ after installing a new DLL.

## Neovim configuration

Paths below are relative to the directory containing `VimPad++.dll` in
Notepad++, or `VimMotions.dll` in the standalone host:

| File | Purpose |
| --- | --- |
| `neovim/vim-motions-init.lua` | Bootstrap loaded when the embedded process starts. |
| `neovim/init.lua` | Settings loaded first. The Notepad++ **Open Neovim config** command opens this file. |
| `neovim/init.vim` | Vimscript fallback loaded first when `neovim/init.lua` is absent. |
| `config/init.lua` or `config/init.vim` | Optional user settings loaded afterward. Keep only one of these files. |
| `config/vimpad.lua` | VimPad++ settings, separate from Neovim options and mappings. |

The bootstrap adds `config/` and `config/after/` to Neovim's runtime search path.
When both bundled init files exist, `neovim/init.lua` takes precedence. Errors
in either loaded init file are reported during startup. Existing staged bootstrap
files are preserved; copy the updated `src/neovim_init.lua` as described above
to apply bootstrap changes to an existing runtime.

For example, `config/vimpad.lua` can let the host handle save and tab shortcuts:

```lua
return {passthrough_keys = {"<C-s>", "<C-Tab>", "<C-S-Tab>"}}
```

With no file, the passthrough list is empty. Invalid settings prevent the backend
from starting. In Notepad++, **Reload VimPad++ and Neovim configs** restarts the
document sessions and reloads their settings. Pending Vim commands and state
held only in Neovim are discarded; the visible Scintilla text is retained.
The native backend does not load these settings.

## Functional tests

```sh
make -C plugin testnative              # Native backend; test is an alias
make -C plugin testnvim WAIT_MS=100     # Neovim backend
make -C plugin testnvim TEST_PATH=tests/cases/vim_word_cases.json WAIT_MS=100
make -C plugin testnative TEST_PATH=tests/cases VISIBLE=1
```

| Option | Default | Purpose |
| --- | --- | --- |
| `TEST_PATH` | `tests/cases` | A JSON file, or a directory searched recursively for JSON files. |
| `WAIT_MS` | `0` | Milliseconds to wait after each case's keys, while processing Windows messages. |
| `VISIBLE` | unset | Set to `1` to show the Scintilla test window. |
| `TEST_REPORT_DIR` | `tests/functional/tmp/reports` | Destination for the text and JSON reports. |

The wait is a fixed delay, not a signal that Neovim has finished. Increase it
if needed; `0` checks immediately. The window closes when the run ends.
Reports are overwritten on each run, including runs for different backends.

**Fixture formatting:** `testnative`, `testnvim`, and `test-build` run
`format-test-cases` first. This formats **all** JSON files under `tests/cases`,
even when `TEST_PATH` selects a single file. `make -C plugin format-test-cases`
runs the formatter by itself. `make -C plugin test-build` formats the cases and
builds the native runner and dependencies without running the tests.

## Fuzzing

```sh
make -C plugin fuzznative              # fuzz is a native alias
make -C plugin fuzznvim
make -C plugin fuzz-seed-native SEED=123 # fuzz-seed is a native alias
make -C plugin fuzz-seed-nvim SEED=123
```

Fuzzing compares generated operator, motion, and paste sequences against local
Neovim. The comparison has [known limits](tests_fuzzing/README.md#current-coverage-and-limitations),
including expected text that is not taken from Neovim. Each run replaces
`plugin/tests_fuzzing/tmp/` and removes `plugin/.nvimlog` if it exists.
`make -C plugin fuzz-build` only builds the native test runner and its DLL
dependencies.

## Replay a test case

Replay opens a case's initial text and caret in the selected Scintilla host.
You can then enter the case's keys yourself to debug it.

```sh
make -C plugin replay CASE='motion.word_forward' REPLAY_CASES=tests/cases/vim_word_cases.json
make -C plugin replaynative CASE='motion.word_forward' REPLAY_CASES=tests/cases/vim_word_cases.json
make -C plugin replaynvim CASE='motion.word_forward' REPLAY_CASES=tests/cases/vim_word_cases.json
```

`replay` is an alias for `replaynative`. Both backend-specific targets build
their host and dependencies; `replaynvim` also stages Neovim.

| Parameter | Required | Meaning |
| --- | --- | --- |
| `CASE` | Yes | The case's exact `id` in the JSON file. Quote IDs containing spaces or shell-special characters. |
| `REPLAY_CASES` | No | Path to one JSON file containing the case. Defaults to `tests_fuzzing/tmp/cases/generated.json`, relative to `plugin/`. |

The default file is generated by fuzzing, so it must exist before replaying
from it. To launch an already-built host directly, use `make -C plugin run-replay` with
the same parameters and optionally set `REPLAY_APP` to its executable path.
`REPLAY_APP` defaults to the native host; this shared target does not build or
stage its runtime dependencies.

## Build directories and other targets

Native outputs default to `build/windows-x64/both/`; Neovim outputs go to
`build/windows-x64/neovim-build/`. `BACKENDS=both`, `native`, or `nvim` changes
the native directory suffix only. Choose `native` or `nvim` targets to select
the backend implementation.

| Command | What it does |
| --- | --- |
| `make -C plugin package` | Replace the native host staging folder and ZIP under `plugin/dist/`. Requires `zip` and the root `README.md`. |
| `make -C plugin clean` | Remove `plugin/build/`. Reports under `plugin/tests/functional/tmp/` and packages under `plugin/dist/` remain. |

See [makefile-map.md](makefile-map.md) for the build and test relationships,
[tests/README.md](tests/README.md) for fixture details, and
[tests_fuzzing/README.md](tests_fuzzing/README.md) for the fuzzing workflow.
