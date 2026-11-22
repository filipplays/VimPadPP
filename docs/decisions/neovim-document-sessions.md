# Neovim sessions belong to documents

Status: Accepted, 2026-11-22.

Host shortcuts must remain usable while Neovim waits for a mapping suffix or a
motion character such as the target of `f`. Waiting synchronously for the state
snapshot prevents additional editor input and can reach the host's timeout.

The manager owns one embedded Neovim process per Scintilla document, created on
first activation. A document cloned into two views shares its process. Switching
documents detaches the view without cancelling the pending Vim command. Polling
resumes when that document is attached again; inactive responses stay in its
pipe and cannot update another document. Final close and plugin shutdown release
the processes and retained Scintilla document references.

Configured host shortcuts pass through immediately. Ctrl+S saves the current
Scintilla text. Edits applied afterward leave the document modified again. This
visible-text save behavior was explicitly chosen by the user.

We considered resolving incomplete input with `<Ignore>` and sharing a Neovim
process across buffers. Separate processes provide independent command input
state without requiring a pending command to resolve before switching documents.
The cost is additional process memory and startup work per activated document.
Inactive sessions currently defer updates rather than applying background edits.

See [architecture](../architecture.md) and the
[manager](../../plugin/src/backends/common/backend_manager.cpp). The focused
Windows regression covers pending finds across switches, independent editing,
shared views, and close cleanup; it does not run Notepad++ itself.
