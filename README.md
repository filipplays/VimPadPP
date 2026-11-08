# VimPad++

VimPad++ is a Windows Notepad++ plugin that brings Vim-style editing to Scintilla.
The current development focus is the Neovim backend: it runs an embedded
Neovim process while Notepad++ remains the visible editor. A separate native
Vim engine is included and builds, but it is not the current focus. The two
backends are selected at build time; there is no in-app backend selector.

The project is still under development. Its implementation needs further
review and testing before a public release. Editing has worked acceptably in
informal tests with somewhat large files, but that is not a performance or
correctness guarantee.

## Repository layout

- `plugin/src/notepadpp_plugin.cpp` contains the Notepad++ entry points and
  attaches the plugin to its Scintilla views.
- `plugin/src/backends/neovim/` contains the Neovim process, RPC, and editor
  synchronization code. `plugin/src/backends/native/` contains the native
  engine; `plugin/src/backends/common/` has shared editor and backend code.
- `plugin/src/main.cpp` is a standalone Scintilla host used for development.
- `plugin/src/neovim_init.lua` is the committed Neovim bootstrap script.
- `plugin/tests/` contains functional and focused protocol tests;
  `plugin/tests_fuzzing/` contains differential fuzzing tools.
- `docs/` holds architecture and build details. Start with
  [docs/README.md](docs/README.md).

## Build prerequisites

The Makefile cross-compiles Windows x64 binaries with
`x86_64-w64-mingw32-g++`. Its documented development setup uses Wine to run
Windows binaries. Before building, supply these files, which are **not
committed**:

| Path under `plugin/` | Required content |
| --- | --- |
| `dependencies/neovim/` | A Windows Neovim distribution, including `bin/nvim.exe` and its runtime files. |
| `dependencies/scintilla/` | A Windows x64 Scintilla distribution with `Scintilla.dll` and `License.txt`. |

The dependency directories are Git-ignored. You can place them elsewhere and
pass `NEOVIM_DIR` and `SCINTILLA_DIR` to `make`; relative override paths are
resolved from `plugin/`. The Neovim backend starts its bundled `nvim.exe`, so
an installed macOS or Linux `nvim` does not replace that Windows distribution.
Development and the informal large-file checks used the Windows **Neovim
v0.12.6** release. Other releases have not been verified here.
The standalone test and fuzz workflows have additional requirements described
in [plugin/makefile-commands.md](plugin/makefile-commands.md).

From the repository root, build the Neovim plugin and install it into your
Notepad++ plugins directory with:

```sh
make -C plugin installnvim \
  NOTEPADPP_PLUGIN_DIR='/path/to/Notepad++/plugins/VimPad++'
```

The default install directory in the Makefile is specific to the original
developer's Wine setup, so set `NOTEPADPP_PLUGIN_DIR` for your installation.
The plugin DLL is `VimPad++.dll` and must be inside a matching
`plugins/VimPad++/` directory. When upgrading an earlier installation, close
Notepad++, move any `config/` files you want to keep from the old
`plugins/VIM-Motions-for-NotepadPP/` directory, and remove that old plugin
directory before restarting. Otherwise Notepad++ may load both copies.

The target builds the plugin DLL, stages Neovim with
`plugin/src/neovim_init.lua`, then copies the DLL and `neovim/` directory into
the plugin directory. Restart Notepad++ after installing. To build without
installing, run `make -C plugin nvim`; its output is under
`plugin/build/windows-x64/neovim-build/`. The native backend can be built with
`make -C plugin native` and installed with `make -C plugin installnative`.
See [the command guide](plugin/makefile-commands.md) for test and run targets.

## Neovim configuration

The committed `plugin/src/neovim_init.lua` is copied to
`neovim/vim-motions-init.lua` beside the installed plugin DLL. It first loads
`neovim/init.lua` from the staged distribution if present, then loads one
optional user file from the plugin's `config/` directory: `init.lua` or
`init.vim`. Having both user files is an error. It also adds `config/` and
`config/after/` to Neovim's runtime path. The plugin menu has commands to open
the config, open its folder, and reload it. Configuration and plugins may
behave differently from a normal Neovim UI; test them in this integration.

For example, the following mirrors the settings in an installed
`neovim/init.lua`. Put them in the plugin's `config/init.lua` (beside its
`neovim/` folder) and use **Reload Neovim config** from the plugin menu. This
keeps personal settings separate from the Neovim files copied during install:

```lua
vim.opt.hlsearch = false
vim.opt.incsearch = false

-- Leave Insert mode by typing jj or kk, and Visual mode with ii.
vim.keymap.set('i', 'jj', '<Esc>')
vim.keymap.set('i', 'kk', '<Esc>')
vim.keymap.set('v', 'ii', '<Esc>')

vim.opt.clipboard = 'unnamedplus'

-- Paste from the system clipboard with Ctrl+V.
vim.keymap.set({ 'n', 'v' }, '<C-v>', '"+p', { noremap = true, silent = true })
vim.keymap.set({ 'i', 'c' }, '<C-v>', '<C-r>+', { noremap = true, silent = true })

-- Save with Ctrl+S from Normal, Visual, or Insert mode.
vim.keymap.set({ 'n', 'v' }, '<C-s>', '<cmd>w<cr>', { noremap = true, silent = true })
vim.keymap.set('i', '<C-s>', '<Esc><cmd>w<cr>a', { noremap = true, silent = true })
```

The Ctrl+V mappings replace Neovim's usual Ctrl+V entry into Visual Block
mode. Remove those two mappings if you use Visual Block selection. The plugin
handles `:w` by asking Notepad++ to save the active document, so the Ctrl+S
mappings above use the same save path.

## Known limitations

- Folding does not yet behave properly in the Notepad++ integration.
- The viewport commands `zt`, `zz`, and `zb` do not scroll the Notepad++ view
  as they do in Neovim.
- `:q` and related quit commands can exit the background Neovim process,
  leaving the backend unusable until it is restarted or reloaded.
- The standalone functional harness exercises a Scintilla control, not the
  full Notepad++ plugin lifecycle or its keyboard shortcuts. Normal Notepad++
  routing, multiple views and documents, and interaction with other plugins
  need more validation.
- External editor changes still trigger full-text comparisons, and some
  startup or resynchronization RPC calls can block the UI while waiting for
  Neovim. Large-file behavior needs more systematic measurement.

This list is not exhaustive. Some other Vim commands may also fail or behave
differently in the plugin; not every command has been tested.

## License

Copyright 2026 Filip Zevnik. This project's original code is licensed under
the [Apache License 2.0](LICENSE). Neovim and Scintilla are separate projects
with their own licenses. They are not included in this repository; users must
obtain the required distributions themselves and retain their license notices.
