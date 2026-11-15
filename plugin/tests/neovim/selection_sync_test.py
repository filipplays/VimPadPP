"""Exercise the backend's selection RPC with a real embedded Neovim."""
import pathlib
import re
import subprocess
import tempfile

pPlugin = pathlib.Path(__file__).resolve().parents[2]
sSource = (pPlugin / "src/backends/neovim/neovim_backend.cpp").read_text()
sHelper = re.search(r'SelectionSyncLua\[\]\s*=\s*R"lua\((.*?)\)lua"', sSource, re.S).group(1)
sLua = "local sHelper = [====[" + sHelper + "]====]\n" + r'''
local nChannel = vim.fn.jobstart({vim.v.progpath, '--embed', '--headless', '-u', 'NONE', '-i', 'NONE', '-n'}, {rpc=true})
local function rpc(sMethod, ...) return vim.rpcrequest(nChannel, sMethod, ...) end
for _, sSelection in ipairs({'inclusive', 'exclusive'}) do
  rpc('nvim_exec_lua', 'vim.o.selection = ...', {sSelection})
  for _, aCase in ipairs({
    {{'abcdef'}, {1,1,1,4,1,3}, {'aef'}},
    {{'abcdef'}, {1,4,1,1,1,3}, {'aef'}},
    {{'abc','def'}, {1,0,2,0,1,3}, {'def'}},
    {{'abc','def'}, {2,0,1,0,1,3}, {'def'}},
    {{'aé中Z'}, {1,1,1,6,1,3}, {'aZ'}},
    {{'aé中Z'}, {1,6,1,1,1,3}, {'aZ'}},
    {{'abcdef'}, {1,0,1,6,1,5}, {''}},
    {{'abcdef'}, {1,2,1,3,1,2}, {'abdef'}},
  }) do
    rpc('nvim_buf_set_lines', 0, 0, -1, true, aCase[1])
    rpc('nvim_exec_lua', sHelper, aCase[2])
    assert(rpc('nvim_eval', 'mode()') == 'v')
    rpc('nvim_input', 'd')
    vim.wait(20)
    local aActual = rpc('nvim_buf_get_lines', 0,0,-1,true)
    assert(vim.deep_equal(aActual, aCase[3]), sSelection .. ' ' .. vim.inspect(aCase) .. ' got ' .. vim.inspect(aActual))
  end
end
-- A plain click preserves Insert mode; dragging switches it to Visual.
rpc('nvim_buf_set_lines', 0, 0, -1, true, {'abcdef'})
rpc('nvim_input', 'i')
vim.wait(20)
rpc('nvim_exec_lua', sHelper, {1,2,1,2,1,1})
assert(rpc('nvim_eval', 'mode()') == 'i')
rpc('nvim_exec_lua', sHelper, {1,1,1,4,1,3})
assert(rpc('nvim_eval', 'mode()') == 'v')
-- Collapsing a Visual selection returns to Normal mode.
rpc('nvim_exec_lua', sHelper, {1,2,1,2,1,1})
assert(rpc('nvim_eval', 'mode()') == 'n')
vim.fn.jobstop(nChannel)
print('Selection sync checks passed')
'''
with tempfile.TemporaryDirectory(prefix="vim-selection-") as sDirectory:
    pScript = pathlib.Path(sDirectory) / "selection.lua"
    pScript.write_text(sLua)
    subprocess.run(["nvim", "--headless", "-u", "NONE", "-i", "NONE", "-n", "-l", str(pScript)], check=True, timeout=30)
