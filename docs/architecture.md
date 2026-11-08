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

The Neovim state helper is installed during authoritative host-to-Neovim
synchronization. Per-key requests invoke it with the last applied buffer,
`changedtick`, journal epoch, and edit sequence. The nine-field result is
`[buffer, changedtick, epoch, base-sequence, sequence, kind, payload, cursor,
[mode, visual-anchor]]`. Kind 0 omits text, kind 1 carries a full line snapshot,
and kind 2 carries ordered `[sequence, first-line, last-line, replacement-lines]`
updates. Line ranges are zero-based and end-exclusive; `[-1, -1, []]` marks a
sequence entry where only `changedtick` advanced.

The helper uses `nvim_buf_attach()` callbacks to retain actual edits rather
than inferring a command's scope. Issuing a response does not release edits:
a later request acknowledging an issued checkpoint releases its older history.
This lets discarded state responses be retried without losing text changes.
Retention is bounded to 1,024 entries and approximately 4 MiB of payload plus
per-entry accounting. Expired checkpoints, reloads, detach, buffer switches,
and untracked tick changes fall back to a full snapshot with a fresh baseline
where needed. The epoch distinguishes journal lifetimes; the sequence orders
callbacks independently of buffer ticks.

The host validates an entire update batch against the acknowledged baseline
and evolving line count before applying it. It maps line boundaries through
Scintilla, preserves the document's selected line-ending convention, and
limits prefix/suffix comparisons to replacement ranges. Both editors retain
one empty logical line after a delete-all update. Full recovery snapshots use
the same replacement helper over the whole document. Existing full-text
Scintilla comparisons still protect against external edits; this path does
not yet replace those checks with modification notifications.

The acknowledged buffer/tick/epoch/sequence advances only after text and
cursor/selection state are applied, or after authoritative Scintilla text is
successfully pushed to Neovim. Stale and skipped responses do not advance it;
a mismatched delta baseline requests a full snapshot. The host's input revision
counters remain separate and continue to reject outdated state replies.

Incoming asynchronous RPC bytes pass through a persistent MessagePack frame
scanner before value decoding. It preserves progress across pipe chunks and
skips payload bytes without constructing values. Complete messages are decoded
once; malformed or oversized frames fail communication. The existing response
timer still schedules polling.

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
