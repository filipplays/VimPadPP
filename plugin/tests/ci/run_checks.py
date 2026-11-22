"""Build and run the existing Neovim checks with bounded runtimes and logs."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

P_ROOT = Path(__file__).resolve().parents[3]
P_PLUGIN = P_ROOT / "plugin"
P_BUILD = P_PLUGIN / "build/windows-x64/neovim-build"
P_REPORTS = P_PLUGIN / "tests/functional/tmp/ci"


def run_check(sName, aCommand, pCwd=P_ROOT, nTimeout=180):
    print(f"Running {sName}", flush=True)
    P_REPORTS.mkdir(parents=True, exist_ok=True)
    with (P_REPORTS / f"{sName}.log").open("w") as oLog:
        try:
            oResult = subprocess.run(
                [str(xArgument) for xArgument in aCommand], cwd=pCwd,
                stdout=oLog, stderr=subprocess.STDOUT, timeout=nTimeout,
                check=False,
            )
            bPassed = oResult.returncode == 0
        except (subprocess.TimeoutExpired, OSError) as oError:
            oLog.write(f"\n{oError}\n")
            bPassed = False
    print((P_REPORTS / f"{sName}.log").read_text(errors="replace"), flush=True)
    print(f"{'PASS' if bPassed else 'FAIL'}: {sName}", flush=True)
    return bPassed


def portable():
    aResults = []
    pBuild = P_PLUGIN / "build/ci-portable"
    pBuild.mkdir(parents=True, exist_ok=True)
    for sTest in ("msgpack_frame", "line_update"):
        pExecutable = pBuild / sTest
        bBuilt = run_check(sTest + "-build", [
            os.environ.get("CXX_PORTABLE", "clang++"), "-std=c++20", "-Wall",
            "-Wextra", "-Wpedantic", "-fsanitize=address,undefined", "-g",
            "-Iplugin/src/backends/neovim",
            f"plugin/tests/neovim/{sTest}_test.cpp", "-o", pExecutable,
        ])
        aResults.append(bBuilt and run_check(sTest, [pExecutable]))
    for sTest in ("state_snapshot", "plugin_config", "selection_sync", "bootstrap_config"):
        aResults.append(run_check(sTest, [
            sys.executable, f"plugin/tests/neovim/{sTest}_test.py",
        ]))
    return all(aResults)


def build():
    if not run_check("nvim-build", ["make", "-C", "plugin", "nvim"], nTimeout=600):
        return False
    aResults = [run_check("functional-build", [
        "make", "-C", "plugin", "build/windows-x64/neovim-build/VimMotionsTests.exe",
        "TEST_BUILD_DIR=build/windows-x64/neovim-build",
    ])]
    aTests = [
        ("HostKeyTests", "host_key_test.cpp", []),
        ("PassthroughTests", "passthrough_test.cpp", ["-lcomctl32"]),
        ("MouseSelectionTests", "mouse_selection_test.cpp", []),
        ("NeovimIntegrationTests", "main.cpp", []),
        ("FailureCleanupTests", "failure_cleanup_test.cpp", []),
        ("RpcTransportTests", "rpc_transport_test.cpp", []),
        ("VimPadRpcPeer", "rpc_transport_test.cpp", ["-DRPC_TEST_PEER"]),
        ("DocumentManagerTests", "passthrough_test.cpp", [
            "-DTEST_DOCUMENT_MANAGER", "-DVIM_MOTIONS_BUILD",
            *[f"plugin/src/backends/common/{sFile}.cpp" for sFile in (
                "backend_manager", "backend_common", "editor_text", "scintilla_editor", "helpers")],
            "plugin/src/backends/neovim/neovim_backend.cpp", "-lcomctl32",
        ]),
    ]
    for sName, sFile, aExtra in aTests:
        aResults.append(run_check(sName + "-build", [
            os.environ.get("CXX_WINDOWS", "x86_64-w64-mingw32-g++"),
            "-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Wno-cast-function-type",
            "-Iplugin/src/backends/common", "-Iplugin/src/backends/neovim",
            f"plugin/tests/neovim/{sFile}", *aExtra, "-o", P_BUILD / f"{sName}.exe",
            "-static", "-static-libgcc", "-static-libstdc++",
        ]))
    return all(aResults)


def integration():
    aResults = []
    # Never inherit a developer's preserved init files or write test settings
    # into their build runtime. The source bootstrap is used for every run.
    with tempfile.TemporaryDirectory(prefix="vimpad-ci-") as sTemporary:
        pRuntime = Path(sTemporary)
        for pFile in [*P_BUILD.glob("*.exe"), *P_BUILD.glob("*.dll")]:
            shutil.copy2(pFile, pRuntime / pFile.name)
        shutil.copytree(P_BUILD / "neovim", pRuntime / "neovim")
        for sName in ("init.lua", "init.vim"):
            (pRuntime / "neovim" / sName).unlink(missing_ok=True)
        shutil.copy2(P_PLUGIN / "src/neovim_init.lua", pRuntime / "neovim/vim-motions-init.lua")
        for sName in ("HostKeyTests", "MouseSelectionTests", "NeovimIntegrationTests", "FailureCleanupTests"):
            aResults.append(run_check(sName, ["wine", f"{sName}.exe"], pRuntime))
        pConfig = pRuntime / "config"
        pConfig.mkdir()
        (pConfig / "vimpad.lua").write_text(
            'return {passthrough_keys = {"<C-s>", "<C-Tab>", "<C-S-Tab>"}}\n'
        )
        for sName in ("PassthroughTests", "DocumentManagerTests"):
            aResults.append(run_check(sName, ["wine", f"{sName}.exe"], pRuntime))
    aResults.append(run_check("rpc-transport", [
        sys.executable, "plugin/tests/neovim/rpc_transport_test.py",
        "--peer", P_BUILD / "VimPadRpcPeer.exe",
    ], nTimeout=240))
    return all(aResults)


def main():
    oParser = argparse.ArgumentParser(description=__doc__)
    oParser.add_argument("stage", choices=("portable", "build", "integration"))
    oArgs = oParser.parse_args()
    return 0 if globals()[oArgs.stage]() else 1


if __name__ == "__main__":
    sys.exit(main())
