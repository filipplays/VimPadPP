"""Check the backend's actual plugin-config loader using headless Neovim.

Generated files stay in a system temporary directory.
"""

import argparse
import pathlib
import re
import subprocess
import tempfile


def main():
    oParser = argparse.ArgumentParser(description=__doc__)
    oParser.add_argument("--nvim", default="nvim")
    oArgs = oParser.parse_args()
    pPlugin = pathlib.Path(__file__).resolve().parents[2]
    sSource = (pPlugin / "src/backends/neovim/neovim_backend.cpp").read_text()
    oMatch = re.search(r'PluginConfigLua\[\]\s*=\s*R"lua\((.*?)\)lua"', sSource, re.S)
    if oMatch is None:
        raise RuntimeError("Could not find the backend's plugin-config loader")
    sLua = "local fLoad = assert(loadstring([====[" + oMatch.group(1) + "]====]))\n" + r'''
local sPath = vim.fn.getcwd() .. '/vimpad.lua'
local function check(sConfig, bValid, nKeys)
  if sConfig then
    vim.fn.writefile({sConfig}, sPath)
  else
    vim.fn.delete(sPath)
  end
  local aResult = fLoad(sPath)
  assert(type(aResult) == 'table' and #aResult == 2)
  assert((aResult[1] == '') == bValid, vim.inspect(aResult))
  assert(type(aResult[2]) == 'table')
  if bValid then assert(#aResult[2] == nKeys) end
  -- Empty lists must also survive the actual RPC wire representation.
  local aDecoded = vim.mpack.decode(vim.mpack.encode(aResult))
  assert(type(aDecoded[2]) == 'table')
  return aResult[2]
end
check(nil, true, 0)
check('return {}', true, 0)
check('return {passthrough_keys = {}}', true, 0)
local aKeys = check('return {passthrough_keys = {"<C-s>", "<C-Tab>", "<C-S-Tab>"}}', true, 3)
assert(aKeys[1] == '<C-s>' and aKeys[2] == '<C-Tab>' and aKeys[3] == '<C-S-Tab>')
check('return {passthrough_keys = {"<F5>"}}', true, 1) -- Refresh replaces the old list.
check(nil, true, 0) -- Removing the file restores the defaults.
check('return nil', false)
check('return "bad"', false)
check('this is not Lua', false)
check('error("config failure")', false)
check('return {unknown_setting = true}', false)
check('return {passthrough_keys = false}', false)
check('return {passthrough_keys = "<C-s>"}', false)
check('return {passthrough_keys = {42}}', false)
check('return {passthrough_keys = {key = "<C-s>"}}', false)
check('return {passthrough_keys = {[2] = "<C-s>"}}', false)
check('return {passthrough_keys = {[0] = "<C-s>"}}', false)
check('return {passthrough_keys = {[1.5] = "<C-s>"}}', false)
vim.fn.delete(sPath)
vim.fn.mkdir(sPath)
assert(fLoad(sPath)[1] ~= '') -- An unreadable file must not silently use defaults.
print('Plugin configuration checks passed')
'''
    with tempfile.TemporaryDirectory(prefix="vimpad-config-") as sTemporary:
        pScript = pathlib.Path(sTemporary) / "check.lua"
        pScript.write_text(sLua)
        subprocess.run(
            [oArgs.nvim, "--headless", "-u", "NONE", "-i", "NONE", "-n",
             "-c", "lua local ok, err = pcall(dofile, 'check.lua'); "
             "if not ok then print(err); vim.cmd('cquit 1') end", "-c", "qa!"],
            cwd=sTemporary, check=True,
        )


if __name__ == "__main__":
    main()
