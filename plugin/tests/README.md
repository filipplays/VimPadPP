# Functional tests

The functional suite tests Vim-style editing in a standalone harness: a test
executable creates a real Scintilla control, loads `VimMotions.dll`, installs
the Vim integration, and sends keyboard messages directly to the editor.
It then reads Scintilla's document text and caret position to check the result.

## Running the suite

From the repository root, run:

```sh
make -C plugin testnative
make -C plugin testnvim WAIT_MS=100
```

`make -C plugin test` is an alias for the native suite.
`make -C plugin test-build` builds the native test executable and its runtime DLL
dependencies without executing tests. Test targets run the executable through
Wine from the build directory, so `Scintilla.dll` and `VimMotions.dll` are found
next to the test executable.

By default, the runner recursively loads every JSON file under `tests/cases/`
in stable path order, while preserving the case order inside each file. Set
`TEST_PATH` to select a file or another directory. See the
[Makefile commands](../makefile-commands.md#functional-tests) for backend,
wait, visibility, and report options.

The Makefile expects a compatible Windows x64 Scintilla DLL at
`plugin/dependencies/scintilla/Scintilla.dll`. The build also requires the MinGW
x86-64 C++ compiler and Python 3; running it requires Wine. Neovim tests also
need the Windows Neovim distribution described in the build guide. Functional
test targets format all JSON fixtures under `plugin/tests/cases/`, even when
`TEST_PATH` selects one file. Unless stated otherwise, paths below are relative
to `plugin/`; shell commands run from the repository root.

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

The executable accepts a file or directory and a report directory, followed by
optional visibility and wait arguments:

```text
VimMotionsTests.exe <test-case-file-or-directory> <report-directory> [--visible] [--wait-ms <milliseconds>]
```

## Reports and exit codes

Each functional run writes these generated files under
`tests/functional/tmp/reports/` by default:

- `functional-report.txt` is the human-readable summary and diagnostics.
- `functional-report.json` is the machine-readable result artifact.

The executable returns `0` when every case passes, `1` when any case fails,
and `2` for setup, parsing, or report-writing failures.

## Focused Neovim checks

The checks below have no Makefile targets and are separate from the JSON suite.
Python checks use local Neovim. Windows integration checks need Wine and the
built Windows DLLs. Coverage described here comes from the test code; a passing
check does not establish that the same behavior works inside Notepad++.

### State protocol

These focused checks do not format fixtures or write build outputs into the
repository. From the repository root:

```sh
clang++ -std=c++20 -Wall -Wextra -Wpedantic -fsanitize=address,undefined \
  -Iplugin/src/backends/neovim plugin/tests/neovim/msgpack_frame_test.cpp \
  -o /private/tmp/vim-msgpack-frame-test
/private/tmp/vim-msgpack-frame-test
clang++ -std=c++20 -Wall -Wextra -Wpedantic -fsanitize=address,undefined \
  -Iplugin/src/backends/neovim plugin/tests/neovim/line_update_test.cpp \
  -o /private/tmp/vim-line-update-test
/private/tmp/vim-line-update-test
python3 plugin/tests/neovim/state_snapshot_test.py
```

The framing test covers fragmented and coalesced MessagePack frames, encoding
and decoding at 65,535/65,536/65,537 elements, all tag families, large payloads,
nesting limits, invalid frames, aggregate value counts, and shared decoding
budgets for nested containers and strings. The line-update
test compares byte-range replacements with independent line-array splices
across LF, CRLF, CR, Unicode bytes, EOF insertions/deletions, trailing empty
lines, and empty documents.

The Python test requires a local Neovim (`--nvim /path/to/nvim` overrides the
executable) and uses an embedded RPC process to exercise the Lua helper and
per-key request extracted from the backend. It checks motion-only responses,
small edit payloads, ordered replay after a discarded response, acknowledgment
and history release, undo/redo, multiple changes between requests, buffer
switches, reloads, retention limits, and the baseline after a host text push.
Generated Lua scripts stay in the system temporary directory. These checks do
not exercise the Windows pipe transport or Scintilla application path.

## Plugin configuration and host shortcuts

Run the actual Lua config-loader checks from the repository root:

```sh
python3 plugin/tests/neovim/plugin_config_test.py
```

This requires local Neovim (`--nvim` overrides its path) and writes only to a
system temporary directory. It checks missing files, empty/default settings,
changed settings, invalid Lua, unknown options, invalid list types and sparse
lists. Key-name parsing is checked separately by the Windows test below.

Compile the focused Windows checks from the repository root, putting outputs
in a temporary directory:

```sh
x86_64-w64-mingw32-g++ -std=c++20 -Wall -Wextra -Wpedantic \
  -Iplugin/src/backends/common plugin/tests/neovim/host_key_test.cpp \
  -o /private/tmp/HostKeyTests.exe -static -static-libgcc -static-libstdc++
wine /private/tmp/HostKeyTests.exe

x86_64-w64-mingw32-g++ -std=c++20 -Wall -Wextra -Wpedantic \
  -Iplugin/src/backends/common plugin/tests/neovim/passthrough_test.cpp \
  -o /private/tmp/PassthroughTests.exe -lcomctl32 -static -static-libgcc -static-libstdc++
```

Run `PassthroughTests.exe` in an isolated directory containing the built Neovim
`VimMotions.dll`, `Scintilla.dll`, and the staged `neovim/` distribution with
`vim-motions-init.lua`. Add `config/vimpad.lua` there with:

```lua
return {passthrough_keys = {"<C-s>", "<C-Tab>", "<C-S-Tab>"}}
```

To exercise the manager's Notepad++ document activation/close entry points as
well as shared-view attachment, compile the same test directly with the backend:

```sh
x86_64-w64-mingw32-g++ -std=c++20 -Wall -Wextra -Wpedantic \
  -DTEST_DOCUMENT_MANAGER -DVIM_MOTIONS_BUILD \
  -Iplugin/src/backends/common -Iplugin/src/backends/neovim \
  plugin/tests/neovim/passthrough_test.cpp plugin/src/backends/common/backend_manager.cpp \
  plugin/src/backends/common/backend_common.cpp plugin/src/backends/common/editor_text.cpp \
  plugin/src/backends/common/scintilla_editor.cpp plugin/src/backends/common/helpers.cpp \
  plugin/src/backends/neovim/neovim_backend.cpp -o /private/tmp/DocumentManagerTests.exe \
  -lcomctl32 -static -static-libgcc -static-libstdc++
```

Stage and run this executable in the same isolated runtime directory described
above. This variant also checks shared document attachment to a second view and
closing an inactive document with pending Vim input, including a duplicate close.

The integration test creates a host window with accelerator bindings. It checks
that configured shortcuts reach the host without waiting for pending edits, extra
modifiers do not match, Ctrl+V still belongs to Neovim, and translated
characters pass through even after Ctrl is released. It also checks that
subsequent ordinary input returns to Neovim, and a pending `f` survives switching
Scintilla documents while another document is edited independently. The test
does not load Notepad++ or verify its actual tab switching, menu commands, or
two-view lifecycle.

## RPC transport and bootstrap configuration

Build the backend and the Windows RPC harness from the repository root:

```sh
make -C plugin nvim
x86_64-w64-mingw32-g++ -std=c++20 -Wall -Wextra -Wpedantic -Wno-cast-function-type \
  -Iplugin/src/backends/common -Iplugin/src/backends/neovim \
  plugin/tests/neovim/rpc_transport_test.cpp \
  -o plugin/build/windows-x64/neovim-build/RpcTransportTests.exe \
  -static -static-libgcc -static-libstdc++
x86_64-w64-mingw32-g++ -std=c++20 -Wall -Wextra -Wpedantic -DRPC_TEST_PEER \
  -Iplugin/src/backends/neovim plugin/tests/neovim/rpc_transport_test.cpp \
  -o /private/tmp/VimPadRpcPeer.exe -static -static-libgcc -static-libstdc++
python3 plugin/tests/neovim/rpc_transport_test.py
python3 plugin/tests/neovim/bootstrap_config_test.py
```

The RPC runner stages temporary runtimes; it never replaces the real Neovim
executable or existing init files. It checks installation and authoritative host
replacement at 65,535, 65,536, and 65,537 lines against real Neovim. A separate
faulty peer checks a full input pipe, no response, continuous notifications,
slow partial replies, oversized array headers, aggregate value rejection, and
invalid asynchronous input. Timeout cases must fail within 7.5 seconds including
startup and process cleanup; malformed input must fail within 2.5 seconds.
Each failure checks preserved host text, usable ordinary editing, and no live
child process. Use `--cases large` or another case name to select a subset.

The bootstrap check runs the actual staging script and source bootstrap with
local headless Neovim. It covers Vimscript-only bundled configuration, Lua
precedence when both bundled files exist, later `config/` overrides, preserved
init files, and configuration errors. These checks do not run Notepad++ itself.

## Neovim failure cleanup

From the repository root, build the backend and the focused plugin-DLL test:

```sh
make -C plugin nvim
x86_64-w64-mingw32-g++ -std=c++20 -Wall -Wextra -Wpedantic -Wno-cast-function-type \
  -Iplugin/src/backends/common plugin/tests/neovim/failure_cleanup_test.cpp \
  -o plugin/build/windows-x64/neovim-build/FailureCleanupTests.exe \
  -static -static-libgcc -static-libstdc++
cd plugin/build/windows-x64/neovim-build
wine FailureCleanupTests.exe
```

This loads the actual `VimPad++.dll` into a small host implementing the required
Notepad++ messages. It terminates a test-owned Neovim process and checks global
cleanup across two views and an inactive document, input routing during the
error dialog, the menu checkmark, Ctrl+F reaching its host accelerator exactly
once, preserved text and ordinary editing in both views, and fresh sessions
after re-enabling. A second failure is detected inside the keyboard hook and
checks that the triggering shortcut also reaches the host. It also checks that
ordinary input does not recreate sessions while disabled. It does not run
Notepad++ itself.

## Neovim mouse selections

Run the Lua selection conversion checks from the repository root:

```sh
python3 plugin/tests/neovim/selection_sync_test.py
```

They extract the actual backend helper and check forward/backward selections,
Unicode, line boundaries, inclusive/exclusive options, deletion, and mode
transitions using a local embedded Neovim.

The Windows integration test sends mouse messages to real Scintilla and checks
selection persistence while polling, release, and deletion immediately after
release. It also checks that the first gesture cancels incomplete commands
without moving the selection, the next gesture works, and ordinary clicks still
work with a state response pending. Build the backend, then compile and run the
test alongside its DLLs:

```sh
make -C plugin nvim
x86_64-w64-mingw32-g++ -std=c++20 -Wall -Wextra -Wpedantic -Wno-cast-function-type \
  -Iplugin/src/backends/common plugin/tests/neovim/mouse_selection_test.cpp \
  -o plugin/build/windows-x64/neovim-build/MouseSelectionTests.exe \
  -static -static-libgcc -static-libstdc++
(cd plugin/build/windows-x64/neovim-build && wine MouseSelectionTests.exe)
```

The test uses the staged Neovim configuration. It covers ordinary stream
selections, not rectangular/multiple selections or Notepad++ lifecycle behavior.

## Neovim UI and keyboard integration

`tests/neovim/main.cpp` checks caret styles, character/line/block Visual
selections, Enter and Backspace, Ctrl+W, Ctrl+V routing past a host accelerator,
and search-result visibility after wrapping, zooming, and resizing. It also
checks that uninstall restores the scrolling policy.

Build and run it from the repository root:

```sh
make -C plugin nvim
x86_64-w64-mingw32-g++ -std=c++20 -Wall -Wextra -Wpedantic -Wno-cast-function-type \
  -Iplugin/src/backends/common plugin/tests/neovim/main.cpp \
  -o plugin/build/windows-x64/neovim-build/NeovimIntegrationTests.exe \
  -static -static-libgcc -static-libstdc++
(cd plugin/build/windows-x64/neovim-build && wine NeovimIntegrationTests.exe)
```

This uses the staged Neovim configuration and a real Scintilla control. It does
not load Notepad++.
