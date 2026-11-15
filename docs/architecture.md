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

Each installed Neovim backend session starts its own embedded Neovim process
and exchanges MessagePack-RPC messages over standard input and output pipes.
Requests have the form `[0, id, method, arguments]`, responses are
`[1, id, error, result]`, and notifications are `[2, method, arguments]`.
For example, the input key `j` is sent as the notification
`[2, "nvim_input", ["j"]]`; names such as `<Esc>` are Neovim key notation
inside the string. The encoder writes multi-byte numbers in big-endian order.

The process starts with the staged `vim-motions-init.lua`. That bootstrap
loads bundled defaults and then an optional user configuration. Its buffer
write handler sends a save notification to the host, which performs the save
after pending state has been applied. Normal response polling uses a timer;
startup and authoritative host-to-Neovim synchronization also make blocking
RPC calls, whose pipe reads have a five-second timeout.

The Neovim backend separately evaluates `config/vimpad.lua` through its existing
Lua RPC connection at installation. This returned table holds plugin settings;
`init.lua` and `init.vim` remain ordinary Neovim configuration. A missing plugin
config uses empty defaults. Invalid settings fail installation with an error.
The shared manager's message hook checks each session's configured physical
shortcuts before intercepting Ctrl accelerators. Configured shortcuts first
drain pending state/caret replies (with a five-second timeout), then continue
through the host accelerator path. The backend also passes their keydown and
translated character messages through to Scintilla when no accelerator claims
them. Refresh recreates both sessions, reloading both configurations. The native
backend currently has no plugin-config loader.

The Neovim state helper is installed during authoritative host-to-Neovim
synchronization. Per-key requests invoke it with the last applied buffer,
`changedtick`, journal epoch, and edit sequence. The nine-field result is
`[buffer, changedtick, epoch, base-sequence, sequence, kind, payload, cursor,
[mode, visual-anchor, selection-option]]`. Kind 0 omits text, kind 1 carries a full line snapshot,
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

Ordinary mouse selections are owned by Scintilla until button release or loss
of mouse capture. The backend drains pending edits before the gesture, ignores
caret notifications generated during it, and rejects state replies that could
restore an older selection. It samples the final anchor and caret after
Scintilla processes the mouse message, including in the standalone host.
A nonempty range becomes characterwise Visual mode through a Lua RPC;
subsequent keys wait for this operation to finish. Conversion preserves drag
direction and accounts for Neovim's inclusive/exclusive selection option and
Scintilla's half-open byte positions. Rectangular and multiple selections are
outside this path. An empty click preserves Insert mode or exits Visual mode.

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

Notepad++ integration needs separate coverage for plugin loading and shutdown,
view and document switches, Unicode input, shortcuts, and interactions with
other plugins. Tests that send messages directly to Scintilla can exercise the
subclass while bypassing Notepad++ keyboard routing and shortcuts; exercise
the normal application input path as well when testing those interactions.

Build and test targets write files. Functional test targets format all JSON
fixtures; fuzzing replaces generated output and may remove `.nvimlog`. Review
the linked guides and obtain required authorization before running them.
