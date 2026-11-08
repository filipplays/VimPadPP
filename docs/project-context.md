# Project context

The current desired product name is **VimPad++**, as selected by the user on
2026-11-08. This is a naming preference pending a rename; name availability
has not been verified.

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

The complete intended feature set, product priorities, and future direction
are not established by this documentation. Treat current code and tests as
evidence of implementation, not proof of a broader product requirement.

Some local planning material and dependencies are Git-ignored. Do not assume
they are available in another checkout or that ignored plans describe current
behavior. The Neovim build also references `plugin/src/neovim_init.lua`; check
that this file is present in the checkout when investigating that build.
