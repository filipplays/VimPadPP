"""Stage isolated real/faulty Neovim runtimes and run the Windows RPC checks."""

import argparse
import os
import pathlib
import shutil
import subprocess
import tempfile


def main():
    pPlugin = pathlib.Path(__file__).resolve().parents[2]
    oParser = argparse.ArgumentParser(description=__doc__)
    oParser.add_argument("--build-dir", type=pathlib.Path,
                         default=pPlugin / "build/windows-x64/neovim-build")
    oParser.add_argument("--peer", type=pathlib.Path, default=pathlib.Path("/private/tmp/VimPadRpcPeer.exe"))
    oParser.add_argument("--cases", nargs="+", default=[
        "large", "no-read", "write-stall", "notifications", "trickle",
        "oversized-header", "value-budget", "async-budget",
    ])
    oArgs = oParser.parse_args()
    aEnvironment = dict(os.environ, WINEDEBUG="-all")
    with tempfile.TemporaryDirectory(prefix="vim-rpc-") as sDirectory:
        pRoot = pathlib.Path(sDirectory)
        for sRuntime in ("real", "fake"):
            pRuntime = pRoot / sRuntime
            pRuntime.mkdir()
            for sFile in ("VimMotions.dll", "Scintilla.dll", "RpcTransportTests.exe"):
                shutil.copy2(oArgs.build_dir / sFile, pRuntime / sFile)
        if "large" in oArgs.cases:
            # Use the actual distribution with the current source bootstrap,
            # without inheriting preserved user init files from a build folder.
            pSource = (oArgs.build_dir / "neovim").resolve()

            def ignore_init(sPath, aNames):
                if pathlib.Path(sPath) == pSource:
                    return [sName for sName in aNames if sName in (
                        "init.lua", "init.vim", "vim-motions-init.lua")]
                return []

            shutil.copytree(pSource, pRoot / "real/neovim", ignore=ignore_init)
            shutil.copy2(pPlugin / "src/neovim_init.lua", pRoot / "real/neovim/vim-motions-init.lua")
        (pRoot / "fake/neovim/bin").mkdir(parents=True)
        shutil.copy2(oArgs.peer, pRoot / "fake/neovim/bin/nvim.exe")
        for sCase in oArgs.cases:
            pRuntime = pRoot / ("real" if sCase == "large" else "fake")
            print(f"Running {sCase}", flush=True)
            subprocess.run(["wine", "RpcTransportTests.exe", sCase], cwd=pRuntime,
                           env=aEnvironment, check=True, timeout=20)


if __name__ == "__main__":
    main()
