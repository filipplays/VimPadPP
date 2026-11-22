"""Run the actual staged bootstrap against Lua and Vimscript config combinations."""

import pathlib
import subprocess
import sys
import tempfile


pPlugin = pathlib.Path(__file__).resolve().parents[2]
aCases = [
    ({}, "", False),
    ({"neovim/init.vim": "let g:bootstrap_marker = 'vim'"}, "vim", False),
    ({"neovim/init.lua": "vim.g.bootstrap_marker = 'lua'"}, "lua", False),
    ({"neovim/init.lua": "vim.g.bootstrap_marker = 'lua'",
      "neovim/init.vim": "let g:bootstrap_marker = 'vim'"}, "lua", False),
    ({"neovim/init.vim": "let g:bootstrap_marker = 'vim'",
      "config/init.lua": "vim.g.bootstrap_marker = vim.g.bootstrap_marker .. '+override'"},
     "vim+override", False),
    ({"neovim/init.lua": "vim.g.bootstrap_marker = 'lua'",
      "config/init.vim": "let g:bootstrap_marker .= '+override'"}, "lua+override", False),
    ({"neovim/init.vim": "throw 'invalid bundled Vimscript'"}, "", True),
    ({"config/init.lua": "", "config/init.vim": ""}, "", True),
]

with tempfile.TemporaryDirectory(prefix="vim bootstrap ") as sDirectory:
    pRoot = pathlib.Path(sDirectory)
    for nCase, (aFiles, sExpected, bError) in enumerate(aCases):
        pCase = pRoot / str(nCase)
        (pCase / "neovim").mkdir(parents=True)
        for sName, sContents in aFiles.items():
            pFile = pCase / sName
            pFile.parent.mkdir(parents=True, exist_ok=True)
            pFile.write_text(sContents)
        # Exercise the distribution staging script, including its preserved files.
        pDistribution = pCase / "distribution"
        pDistribution.mkdir()
        (pDistribution / "init.vim").write_text("let g:bootstrap_marker = 'must-not-overwrite'")
        subprocess.run([
            sys.executable, str(pPlugin / "copy_neovim.py"), str(pDistribution),
            str(pCase / "neovim"), "--bootstrap", str(pPlugin / "src/neovim_init.lua"),
        ], check=True)
        if "neovim/init.vim" not in aFiles:
            (pCase / "neovim/init.vim").unlink()
        pCheck = pCase / "check.lua"
        pCheck.write_text(
            "assert((vim.g.vim_motions_startup_error ~= '') == "
            + str(bError).lower() + ", vim.g.vim_motions_startup_error)\n"
            + "assert((vim.g.bootstrap_marker or '') == '" + sExpected + "')\n"
        )
        subprocess.run([
            "nvim", "--headless", "-u", str(pCase / "neovim/vim-motions-init.lua"),
            "-i", "NONE", "-n", "-l", str(pCheck),
        ], check=True, timeout=15)

print("Bootstrap configuration checks passed")
