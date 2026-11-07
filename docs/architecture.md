# Architecture

## Components

- `plugin/src/notepadpp_plugin.cpp` implements the Notepad++ plugin interface
  and attaches the integration to the host's Scintilla views.
- `plugin/src/main.cpp` is the standalone Scintilla host.
- `plugin/src/backends/common/` contains the `Manager`, the `Backend`
  interface, and Scintilla/editor-text helpers. `Manager` owns a backend
  session for each installed Scintilla window and subclasses the window to
  route messages.
- `plugin/src/backends/native/` implements the native backend and `VimEngine`.
  The engine's commands, motions, actions, and operators are split across
  source files in that directory.
- `plugin/src/backends/neovim/` implements the Neovim backend. It manages a
  Neovim process and message exchange, synchronizing editor state with
  Scintilla. Its initialization script is `plugin/src/neovim_init.lua`.

The [Makefile](../plugin/Makefile) selects backend sources when building:
native targets define `ENABLE_NATIVE_BACKEND`; Neovim targets compile the
Neovim backend instead. `BACKENDS` changes the native output directory suffix;
it does not provide runtime backend selection. See
[Makefile commands](../plugin/makefile-commands.md) for targets and paths.

## Input and editor flow

The Notepad++ plugin attaches a `Manager` to its Scintilla views. The standalone
integration exposes `InstallVim` and `UninstallVim` for a Scintilla window.
The manager forwards window messages to the selected backend. The native
backend passes relevant keyboard input to `VimEngine`; the Neovim backend
communicates with its process and applies state to the editor. Shared editor
helpers wrap Scintilla messages.

## Verification boundaries

The [functional harness](../plugin/tests/README.md) loads the standalone Vim
DLL, creates a Scintilla control, sends Windows input messages, and checks
document text and caret position. It does not load the Notepad++ plugin DLL or
test its lifecycle. [Differential fuzzing](../plugin/tests_fuzzing/README.md)
uses this harness and a local Neovim reference, with documented limitations
for text-changing sequences and Unicode coverage.

Build and test targets write files. Functional test targets format all JSON
fixtures; fuzzing replaces generated output and may remove `.nvimlog`. Review
the linked guides and obtain required authorization before running them.
