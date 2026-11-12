"""Update a Neovim distribution without overwriting existing init files."""

import argparse
import pathlib
import shutil


def copy_neovim(pSource, pDestination, pBootstrap=None):
    aInitNames = ("init.lua", "init.vim", "vim-motions-init.lua")
    pSource = pSource.resolve()

    def ignore_init(sDirectory, aNames):
        if pathlib.Path(sDirectory) == pSource:
            return [sName for sName in aNames if sName in aInitNames]
        return []

    shutil.copytree(pSource, pDestination, dirs_exist_ok=True, ignore=ignore_init)
    for sName in aInitNames:
        pInit = pBootstrap if sName == "vim-motions-init.lua" and pBootstrap else pSource / sName
        if not pInit.is_file():
            continue
        # Exclusive creation also preserves existing symlinks and empty files.
        with pInit.open("rb") as oSource:
            try:
                oDestination = (pDestination / sName).open("xb")
            except FileExistsError:
                continue
            with oDestination:
                shutil.copyfileobj(oSource, oDestination)


def main():
    oParser = argparse.ArgumentParser(description=__doc__)
    oParser.add_argument("source", type=pathlib.Path)
    oParser.add_argument("destination", type=pathlib.Path)
    oParser.add_argument("--bootstrap", type=pathlib.Path)
    oArgs = oParser.parse_args()
    copy_neovim(oArgs.source, oArgs.destination, oArgs.bootstrap)


if __name__ == "__main__":
    main()
