# Functional tests

The functional suite exercises the shared Vim integration in a standalone harness: a test
executable creates a real Scintilla control, loads `VimMotions.dll`, installs
the plugin, and sends it the same Windows messages processed during normal
keyboard input. It then reads Scintilla's document text and caret position to
check the result.

## Running the suite

From the project root, run:

```sh
make test
```

`make test-build` builds the test executable and its runtime DLL dependencies
without executing tests. `make test` runs the executable through Wine from the
build directory, so `Scintilla.dll` and `VimMotions.dll` are found next to the
test executable.

The runner recursively loads every JSON file under `tests/cases/` in stable
path order, while preserving the case order inside each file. The resulting
test order is deliberately deterministic.

The current Makefile expects `wscite/Scintilla.dll` from the old Scintilla
project, but this checkout does not contain that directory. Supply a compatible
Windows x64 Scintilla DLL at that path before using these targets. The build
also requires the MinGW x86-64 C++ compiler and Python 3; running it requires
Wine. `make test` formats the JSON fixtures before building.

## Test cases

Every JSON file under `tests/cases/` contains an array of cases. Nested
folders may group related cases. Each case has this structure:

```json
{
  "id": "motion.word_forward",
  "initial_text": "one two",
  "initial_caret": 0,
  "keys": ["w"],
  "expected_text": "one two",
  "expected_caret": 4
}
```

`initial_caret` and `expected_caret` are zero-based Scintilla byte positions.
`keys` contains either a single ASCII character or the special `"Esc"` token.
Each case starts from a fresh document state and returns Vim to normal mode
before its fixture is installed, so cases do not depend on preceding cases.

Add a test by appending a complete JSON object to an existing array or by
adding a JSON case file anywhere below `tests/cases/`. Use a stable,
descriptive `id`; the generated reports use it to identify failures. JSON
escapes apply to document text: use `\n` for LF and `\r\n` for CRLF. In
particular, Scintilla's `SCI_NEWLINE`, used by `o` and `O`, inserts CRLF on the
Windows target.

Successive `j` and `k` motions retain the column where the vertical movement
started. If an intervening line is too short, the caret stops at that line's
end; a later long enough line restores the retained column. Add coverage for
this behavior with consecutive vertical-motion keys and lines of differing
lengths.

## Input path and assertions

For ordinary keys, the harness sends `WM_CHAR` directly to the Scintilla
window. This is the message that the Vim subclass handles in normal mode and
passes to Scintilla's default procedure in insert mode. For Escape, it sends
`WM_KEYDOWN` with `VK_ESCAPE`, matching the plugin's Escape handling path.

The suite tests the standalone Vim DLL through its public
`InstallVim`/`UninstallVim` API instead of constructing `VimEngine` directly.
That covers dynamic loading, Scintilla window subclassing, keyboard handling,
and observable editor state. It does not load the Notepad++ plugin DLL or
exercise the Notepad++ plugin lifecycle.

## Code layout

- `tests/common/include/TestCase.h` defines the shared test-case and result
  data.
- `tests/common/src/json_test_cases.cpp` parses and validates JSON case files.
- `tests/common/src/test_runner.cpp` owns the Scintilla/DLL harness, executes
  cases, and produces text and JSON report content.
- `tests/functional/src/main.cpp` is the command-line entry point that loads
  cases from a directory, invokes the runner, and writes reports.
- `tests/cases/` contains the ordered fixtures.

The executable accepts two arguments when run directly:

```text
VimMotionsTests.exe <test-cases-directory> <report-directory>
```

## Reports and exit codes

Each `make test` run writes these generated files under
`build/windows-x64/test-results/`:

- `functional-report.txt` is the human-readable summary and diagnostics.
- `functional-report.json` is the machine-readable result artifact.

The executable returns `0` when every case passes, `1` when any case fails,
and `2` for setup, parsing, or report-writing failures.
