# Project context

The project is named **VimPad++**. Its repository is named `VimPadPP`.

The project provides Vim-style editing for Notepad++ and a standalone
Scintilla host. It has a native Vim engine and a backend that uses a Neovim
process. See [architecture.md](architecture.md) for their integration.

The Windows x64 code is C++20 and uses Win32 and Scintilla. The Makefile
supports cross-compiling with MinGW; its documented development setup runs
Windows executables with Wine. Python is used for fixture formatting and
differential fuzzing, and the Neovim backend stages a Windows Neovim
distribution and Lua initialization script. Dependencies and commands are
described in [Makefile commands](../plugin/makefile-commands.md).

The [functional suite](../plugin/tests/README.md) exercises the standalone
integration with a real Scintilla control. The
[fuzzing guide](../plugin/tests_fuzzing/README.md) describes the Neovim
comparison, its generated files, and current limitations. Neither suite
exercises the Notepad++ plugin lifecycle.

Each build selects one backend. Users cannot switch backends at runtime.
The Neovim backend keeps a separate process for each activated document; cloned
views share that document's process. See the
[document-session decision](decisions/neovim-document-sessions.md) for the
reasoning and tradeoffs.

External edits, undo behavior, Unicode and selections, multiple views, process
failure, and Neovim plugins still need broader integration coverage. The
[review findings](neovim-review-findings.md) describe known concerns and open
questions. Existing tests cover specific cases and do not establish full Vim or
Neovim compatibility.

Dependencies are Git-ignored and may not be available in another checkout. The
Neovim build also references `plugin/src/neovim_init.lua`; check that this file
is present in the checkout when investigating that build.
