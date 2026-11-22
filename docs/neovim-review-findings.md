# Neovim and host integration review findings

Review date: 2026-11-22.

This report aggregates the synchronization, host lifecycle/input routing,
selection/encoding, and RPC/configuration reviews. All native-backend findings
are excluded. No fixes have been implemented as part of these reviews.

P1 indicates a high-priority failure involving lost work, malformed normal
traffic, or an unresponsive host. P2 indicates other user-visible failures or
resource-handling defects. These priorities are review assessments.

Unless explicitly marked as reproduced, findings are based on code inspection;
their proposed Windows/Notepad++ triggers have not been exercised. Line numbers
refer to the reviewed source and may move as the code changes.

Documentation status check, 2026-11-22: finding 4 (mouse input during an
incomplete command) has been removed from this list. The current working tree
uses `PrepareMouseSelection()` to cancel incomplete input with the first mouse
gesture; the next gesture can move the caret or select text. See
[current behavior](architecture.md) and
[mouse regression coverage](../plugin/tests/README.md#neovim-mouse-selections).
Runtime checks were not repeated for this documentation update. The remaining
finding numbers are kept so existing references remain usable. Other source
changes are in progress in the shared working tree. The findings below describe
the original reviewed code unless a status note says otherwise.

## Synchronization and recovery

### 1. [P1] External edits can terminate the session and discard pending input

**Trigger:** Leave a Neovim state request pending, for example by typing `f`
without its target. Change Scintilla text through another plugin or host command,
then type the next Vim key.

**Expected:** Reconcile the external edit and continue, or explicitly resolve the
pending command.

**Actual:** The external-edit branch treats an outstanding state request as
fatal. It stops Neovim and consumes the triggering key. Input and edits held only
in Neovim are lost; visible Scintilla text survives. Failed sessions remain failed
across tab switches.

**References:** [neovim_backend.cpp](../plugin/src/backends/neovim/neovim_backend.cpp),
lines 1432–1450 (external-edit handling), 866–870 (failure handling), and
1595–1602 (failed-session resume).

**Focused test:** Send `f`, insert text directly into Scintilla, then send the
target character. Assert that the process stays alive, external text survives,
and the key receives defined handling.

**Verification:** Code inspection only.

### 2. [P2] An external edit can silently consume a save request

**Trigger:** Issue `:w`, then externally modify Scintilla before the corresponding
state response is applied. Hold the response in a harness to make this deterministic.

**Expected:** Save authoritative host text or retain/report the unsuccessful request.

**Actual:** `SyncDocument()` returns `SKIPPED` because host text changed.
`PollResponses()` nevertheless clears `bSavePending`, then declines to invoke
the save handler because the text differs. There is no retry or error. The Lua
write callback has already cleared Neovim's modified flag.

**References:** [neovim_backend.cpp](../plugin/src/backends/neovim/neovim_backend.cpp),
lines 901–904 and 1134–1149;
[neovim_init.lua](../plugin/src/neovim_init.lua), lines 31–35.

**Focused test:** Deliver a save notification, modify Scintilla, then deliver the
state response. Verify that saving succeeds or remains pending with an explicit
failure indication.

**Verification:** Code inspection only.

### 3. [P2] Idle Neovim edits and saves can remain unapplied indefinitely

**Trigger:** Run the following command and wait without typing:

```vim
:lua vim.defer_fn(function() vim.api.nvim_buf_set_lines(0,0,1,false,{'changed'}) end,1000)
```

Add `vim.cmd('write')` to the callback to exercise deferred saving as well.

**Expected:** The active host document eventually reflects the edit and processes
the save.

**Actual:** The response timer drains RPC messages without requesting fresh state.
Journal callbacks retain edits locally without notifying the host. A save
notification only sets a flag; processing it requires a later state response.
Text and saving therefore wait for another input or caret operation. Host Ctrl+S
during that interval saves the older visible text, consistent with the chosen
visible-text save policy but exposing the stale synchronization state.

**References:** [neovim_backend.cpp](../plugin/src/backends/neovim/neovim_backend.cpp),
lines 485–499 (journal callbacks), 394–397 (save notification), 1300–1304
(response timer), and 1144–1149 (save processing).

**Focused test:** Schedule an edit and write after the initial response completes.
Pump host timers without keyboard input; assert updated text and one save-handler
invocation.

**Verification:** Code inspection only.

## Host lifecycle and input routing

### 6. [P2] Supplementary Unicode input is converted as separate invalid characters

**Trigger:** In Insert mode, enter an emoji through a Windows input method that
delivers a UTF-16 surrogate pair as two `WM_CHAR` messages.

**Expected:** Forward one UTF-8 code point.

**Actual:** Each message is independently converted using one `wchar_t`. Neither
surrogate is a complete character, so the intended emoji becomes replacement
characters rather than being reconstructed.

**Reference:** [neovim_backend.cpp](../plugin/src/backends/neovim/neovim_backend.cpp),
lines 1423–1430.

**Focused test:** Deliver `WM_CHAR` values `0xD83D`, `0xDE00` and assert exactly
UTF-8 `F0 9F 98 80` in the document.

**Verification:** Code inspection only; actual input-method delivery and Windows
conversion behavior were not exercised.

## Selection, text positions, and line endings

### 7. [P2] Edits silently change existing mixed line endings

**Trigger:** Load `aa\nbb\r\ncc`, place the caret on the first character, and press `x`.

**Expected:** `a\nbb\r\ncc`; unrelated line separators remain unchanged.

**Actual:** `DocumentLineEnding()` chooses CRLF whenever any CRLF exists.
Replacing the first logical line reconstructs its separator as CRLF, producing
`a\r\nbb\r\ncc`. Full recovery snapshots can normalize every separator. Identical
logical edits can therefore produce different byte changes depending on whether
synchronization uses deltas or a snapshot.

**References:** [neovim_backend.cpp](../plugin/src/backends/neovim/neovim_backend.cpp),
lines 750–769 (EOL choice/restoration), 957–958 (snapshot), and 963–981 (delta).

**Focused test:** Apply one character edit to mixed LF/CRLF/CR fixtures through
both delta and snapshot paths; compare exact bytes, including untouched separators.

**Verification:** Code inspection only.

### 8. [P2] Exclusive block selections highlight characters outside Neovim's selection

**Trigger:** With two lines containing `abcd`, set `selection=exclusive`, then
enter Ctrl+V, `l`, `j`.

**Expected:** Highlight only `a` on each line, matching the selected Neovim block.

**Actual:** The block renderer always advances the right boundary with
`POSITIONAFTER`, highlighting `ab`. Unlike characterwise rendering, it never
checks `selection`. Deleting or yanking operates on less text than the host highlights.

**Reference:** [neovim_backend.cpp](../plugin/src/backends/neovim/neovim_backend.cpp),
lines 1029–1043.

**Focused test:** Compare each Scintilla selection range with Neovim's yanked block
for inclusive/exclusive mode and both horizontal directions.

**Verification:** A headless Neovim check confirmed that this exclusive block
yanks `"a\na"`. The host highlighting discrepancy is established by inspection;
it was not reproduced in Windows.

## Verification performed across the reviews

- State-journal regression passed discarded-response replay, acknowledgement,
  expiry, oversized edits, buffer switches, and reloads.
- Line-replacement regression passed LF, CRLF, CR, and EOF cases.
- Selection Lua regression passed forward/backward, inclusive/exclusive, and
  UTF-8 endpoint cases.
- An additional headless check established exclusive block-yank behavior.
- MessagePack framing regression passed supported tags, fragmentation, repeated
  partial scans, depth checks, and invalid-tag/length rejection.
- Plugin-configuration regression passed missing-file defaults, syntax/runtime
  errors, unknown settings, and malformed lists.
- A temporary bootstrap test reproduced ignored `neovim/init.vim` configuration.

These tests do not establish end-to-end correctness in Windows or Notepad++.
Verification artifacts were generated outside the project. No project formatter
was run because the reviews did not edit source files.

## Reviewed paths

- Repository guidance, [architecture](architecture.md), and the
  [document-session decision](decisions/neovim-document-sessions.md).
- `plugin/src/backends/neovim/`: backend/header, line-update helper, frame scanner.
- `plugin/src/neovim_init.lua`.
- `plugin/src/backends/common/`: manager/interface, editor-text and Scintilla
  wrappers, shortcut parser.
- `plugin/src/notepadpp_plugin.cpp` and `plugin/src/main.cpp`.
- `plugin/copy_neovim.py`, staging references in `plugin/Makefile`, and build/test
  documentation.
- Relevant journal, line-update, selection, mouse, passthrough, framing, and
  configuration tests under `plugin/tests/neovim/`.

## Remaining questions for the next pass

- Should Neovim `:enew`/`:edit` replace host-document text? Snapshots follow the
  current Neovim buffer, while save interception belongs to the initially named buffer.
- Can `:wq` exit before host text application and saving complete?
- Can clone-view transfer or inactive-document external edits restore outdated
  caret/selection positions? Check queued translated input across switches as well.
- Verify Notepad++ notification ordering during clone close and shutdown, and
  reentrant disable/close during RPC or error dialogs.
- Exercise process failure explicitly: applied host text survives, but pending
  Neovim-only work has no automatic recovery.
- Check legacy Scintilla code pages against Neovim's UTF-8 assumptions,
  combining marks, wide-character block geometry, virtual space, tab widths,
  and rectangular/multiple-selection fallback.
- Check buffered asynchronous bytes when a blocking RPC starts, detailed RPC
  error preservation, bootstrap failures after `load_config()`, and cleanup under
  Windows pipe failures or inherited child-process handles.

These questions are not additional confirmed findings.
