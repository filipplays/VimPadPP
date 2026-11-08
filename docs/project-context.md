# Project context

The project is named **VimPad++**. Its GitHub repository uses the `VimPadPP`
slug because GitHub repository names cannot contain `+`.

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

Earlier planning explored strong Neovim editing compatibility alongside an
independent native backend, with a user-facing choice of backend. Runtime
selection is not implemented; its priority and details remain open. The notes
also identified questions about safe switching and state preservation,
document and undo ownership, external edits, Unicode and selections, multiple
views, recovery after process failure, and support for Neovim configuration,
plugins, and UI features. These are areas to investigate, not promises of
support. Treat current code and tests as evidence of implementation, not proof
of a broader product requirement.

Dependencies are Git-ignored and may not be available in another checkout. The
Neovim build also references `plugin/src/neovim_init.lua`; check that this file
is present in the checkout when investigating that build.
