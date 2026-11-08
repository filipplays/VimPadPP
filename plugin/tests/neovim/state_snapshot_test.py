"""Exercise the backend's actual Lua snapshot with a local headless Neovim.

No Python dependencies; all generated files stay in the system temporary directory.
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
    oMatch = re.search(r'StateSnapshotLua\[\]\s*=\s*R"lua\((.*?)\)lua"', sSource, re.S)
    if oMatch is None:
        raise RuntimeError("Could not find the backend's Lua snapshot")
    oRequest = re.search(r'StateRequestLua\[\]\s*=\s*"([^"]+)"', sSource)
    if oRequest is None:
        raise RuntimeError("Could not find the per-key request")
    sLua = ("local sSnapshot = [====[" + oMatch.group(1) + "]====]\n"
            + "local sRequest = [====[" + oRequest.group(1) + "]====]\n") + r'''
local nChannel = vim.fn.jobstart({vim.v.progpath, '--embed', '--headless', '-u', 'NONE', '-i', 'NONE', '-n'}, {rpc = true})
assert(nChannel > 0)
local api = setmetatable({}, {__index = function(_, sMethod)
  return function(...) return vim.rpcrequest(nChannel, sMethod, ...) end
end})
local bInstalled = false
local function snapshot(aKnown, bAcceptCurrent)
  local aArgs = aKnown and {aKnown[1], aKnown[2], aKnown[3], aKnown[5], bAcceptCurrent == true}
                       or {-1, -1, -1, -1, bAcceptCurrent == true}
  local sCode = (bAcceptCurrent or not bInstalled) and sSnapshot or sRequest
  local aState = api.nvim_exec_lua(sCode, aArgs)
  bInstalled = true
  local aDecoded = vim.mpack.decode(vim.mpack.encode(aState))
  assert(#aDecoded == 9 and type(aDecoded[1]) == 'number' and type(aDecoded[2]) == 'number')
  assert(#aDecoded[8] == 2 and #aDecoded[9] == 2)
  return aDecoded
end
local function apply(aKnown, aState, aMirror)
  if aState[6] == 1 then return vim.deepcopy(aState[7]) end
  assert(aKnown[1] == aState[1] and aKnown[3] == aState[3] and aKnown[5] == aState[4])
  if aState[6] == 0 then
    assert(aState[7] == vim.NIL and aState[5] == aKnown[5] and aState[2] == aKnown[2])
    return aMirror
  end
  assert(aState[6] == 2)
  local nSequence = aKnown[5]
  for _, aChange in ipairs(aState[7]) do
    nSequence = nSequence + 1
    assert(aChange[1] == nSequence)
    local nFirst, nLast, aLines = aChange[2], aChange[3], aChange[4]
    if nFirst == -1 then
      assert(nLast == -1 and #aLines == 0)
    else
      assert(nFirst >= 0 and nLast >= nFirst and nLast <= #aMirror)
      local aNext = {}
      for nIndex = 1, nFirst do aNext[#aNext + 1] = aMirror[nIndex] end
      for _, sLine in ipairs(aLines) do aNext[#aNext + 1] = sLine end
      for nIndex = nLast + 1, #aMirror do aNext[#aNext + 1] = aMirror[nIndex] end
      aMirror = #aNext == 0 and {''} or aNext
    end
  end
  assert(nSequence == aState[5])
  local aActual = api.nvim_buf_get_lines(0, 0, -1, true)
  assert(vim.deep_equal(aMirror, aActual), 'replayed text differs: ' .. #aMirror .. '/' .. #aActual ..
         ' updates=' .. vim.inspect(aState[7]) .. ' first=' .. tostring(aMirror[1]) .. '/' .. tostring(aActual[1]))
  return aMirror
end
api.nvim_exec_lua('vim.bo.undolevels = -1', {})
local aLines = {}
for nLine = 1, 5000 do aLines[nLine] = string.rep('large document ', 10) .. nLine end
api.nvim_buf_set_lines(0, 0, -1, true, aLines)
api.nvim_exec_lua('vim.bo.undolevels = 1000', {})
local aInitial = snapshot()
assert(aInitial[6] == 1 and vim.deep_equal(aInitial[7], aLines))
local nFullBytes = #vim.mpack.encode(aInitial)
assert(api.nvim_input('j') == 1)
local aMotion = snapshot(aInitial)
assert(aMotion[6] == 0 and aMotion[8][1] == 2)
local nMotionBytes = #vim.mpack.encode(aMotion)
assert(nMotionBytes < 128 and nFullBytes > 500000)
api.nvim_command('normal! v')
local aVisual = snapshot(aInitial)
assert(aVisual[6] == 0 and aVisual[9][1] == 'v')
api.nvim_command('normal! ' .. vim.api.nvim_replace_termcodes('<Esc>', true, false, true))
assert(api.nvim_input('x') == 1)
local aEdit = snapshot(aInitial)
assert(aEdit[6] == 2, 'a single-character edit must return a delta')
local aEdited = apply(aInitial, aEdit, aLines)
local nEditBytes = #vim.mpack.encode(aEdit)
assert(nEditBytes < 512)
-- A discarded response does not release its updates. Later edits extend the same history.
assert(vim.deep_equal(snapshot(aInitial)[7], aEdit[7]))
api.nvim_buf_set_lines(0, 3, 5, true, {'replacement', 'extra', 'third'})
local aCombined = snapshot(aInitial)
assert(aCombined[6] == 2 and #aCombined[7] > #aEdit[7])
local aCombinedText = apply(aInitial, aCombined, aLines)
local aRemaining = snapshot(aEdit)
assert(aRemaining[6] == 2 and #aRemaining[7] < #aCombined[7])
apply(aEdit, aRemaining, aEdited)
assert(snapshot(aCombined)[6] == 0)
assert(snapshot(aInitial)[6] == 1, 'acknowledged history must be released')

api.nvim_command('undo')
local aUndo = snapshot(aCombined)
assert(aUndo[6] == 2)
local aUndoText = apply(aCombined, aUndo, aCombinedText)
api.nvim_command('redo')
local aRedo = snapshot(aUndo)
assert(aRedo[6] == 2)
apply(aUndo, aRedo, aUndoText)

-- Distant edits, insertions, suffix deletion, appending and an empty document.
local aKnown, aMirror = aRedo, aCombinedText
for _, aCase in ipairs({
  {0, 0, {'prefix', ''}},
  {-1, -1, {'append', 'last'}},
  {-3, -1, {}},
  {0, -1, {}},
  {0, 1, {'', 'new last', ''}},
}) do
  api.nvim_buf_set_lines(0, aCase[1], aCase[2], true, aCase[3])
  local aNext = snapshot(aKnown)
  assert(aNext[6] == 2)
  aMirror = apply(aKnown, aNext, aMirror)
  aKnown = aNext
end
-- Multiple changes captured between snapshots must remain ordered.
api.nvim_exec_lua([[vim.api.nvim_buf_set_lines(0, 0, 1, true, {'one'});
                   vim.api.nvim_buf_set_lines(0, 1, 1, true, {'two'})]], {})
local aBatch = snapshot(aKnown)
assert(aBatch[6] == 2 and #aBatch[7] >= 2)
aMirror = apply(aKnown, aBatch, aMirror)
aKnown = aBatch

api.nvim_win_set_cursor(0, {1, 0})
assert(api.nvim_input('iX<Esc>') > 0)
local aInsert = snapshot(aKnown)
assert(aInsert[6] == 2 and #vim.mpack.encode(aInsert) < 512)
aMirror = apply(aKnown, aInsert, aMirror)
assert(aMirror[1] == 'Xone')
aKnown = aInsert

-- Deterministic varied splices, with some replies deliberately left unacknowledged.
math.randomseed(1947)
for nStep = 1, 40 do
  local nCount = api.nvim_buf_line_count(0)
  local nFirst = math.random(0, nCount)
  local nLast = math.random(nFirst, nCount)
  local aNew = {}
  for nLine = 1, math.random(0, 3) do
    aNew[nLine] = nLine == 2 and '' or ('edit ' .. nStep .. ':' .. nLine .. ' é🙂')
  end
  api.nvim_buf_set_lines(0, nFirst, nLast, true, aNew)
  local aNext = snapshot(aKnown)
  assert(aNext[6] ~= 1, 'retained history should support varied edits')
  if nStep % 3 ~= 0 or nStep == 40 then
    aMirror = apply(aKnown, aNext, aMirror)
    aKnown = aNext
  end
end

local aWrongTick = vim.deepcopy(aKnown)
aWrongTick[2] = aWrongTick[2] + 100
assert(snapshot(aWrongTick)[6] == 1)
local nOldBuffer = aKnown[1]
local nNewBuffer = api.nvim_create_buf(false, true)
api.nvim_set_current_buf(nNewBuffer)
local aOther = snapshot(aKnown)
assert(aOther[6] == 1 and aOther[1] ~= nOldBuffer)
api.nvim_set_current_buf(nOldBuffer)
local aBack = snapshot(aKnown)
assert(aBack[6] == 1 and aBack[3] ~= aKnown[3])

-- Reset after a host text push establishes a journal without retransmitting that text.
local aSync = api.nvim_call_atomic({
  {'nvim_buf_set_lines', {0, 0, -1, true, {'host text', 'second'}}},
  {'nvim_win_set_cursor', {0, {1, 0}}},
  {'nvim_exec_lua', {sSnapshot, {-1, -1, -1, -1, true}}},
})
assert(aSync[2] == vim.NIL)
aKnown = aSync[1][3]
assert(aKnown[6] == 0 and aKnown[7] == vim.NIL)
assert(snapshot(aKnown)[6] == 0)
api.nvim_buf_set_lines(0, 0, 1, true, {'host edited'})
local aAfterPush = snapshot(aKnown)
assert(aAfterPush[6] == 2)
apply(aKnown, aAfterPush, {'host text', 'second'})
aKnown = aAfterPush

-- Expired history and an individual edit exceeding the byte budget recover with full text.
api.nvim_exec_lua([[for n = 1, 1100 do vim.api.nvim_buf_set_lines(0, 0, 1, true, {tostring(n)}) end]], {})
local aExpired = snapshot(aKnown)
assert(aExpired[6] == 1)
assert(snapshot(aExpired)[6] == 0)
api.nvim_exec_lua([[vim.api.nvim_buf_set_lines(0, 0, 1, true, {string.rep('x', 5 * 1024 * 1024)})]], {})
local aLarge = snapshot(aExpired)
assert(aLarge[6] == 1 and #aLarge[7][1] == 5 * 1024 * 1024)
assert(api.nvim_exec_lua('return _G.__vim_motions_sync.sJournal.nBytes', {}) <= 4 * 1024 * 1024)

local sFile = vim.fn.tempname()
vim.fn.writefile({'before reload'}, sFile)
api.nvim_command('edit! ' .. vim.fn.fnameescape(sFile))
local aBeforeReload = snapshot(aLarge)
vim.fn.writefile({'after reload'}, sFile)
api.nvim_command('edit!')
local aReload = snapshot(aBeforeReload)
assert(aReload[6] == 1 and aReload[3] ~= aBeforeReload[3] and aReload[7][1] == 'after reload')
vim.fn.delete(sFile)
vim.fn.jobstop(nChannel)
print(string.format('State journal checks passed: full=%d bytes, motion=%d bytes, edit=%d bytes',
                    nFullBytes, nMotionBytes, nEditBytes))
'''
    with tempfile.TemporaryDirectory(prefix="vim-state-snapshot-") as sTemp:
        pScript = pathlib.Path(sTemp) / "check.lua"
        pScript.write_text(sLua)
        subprocess.run(
            [oArgs.nvim, "--headless", "-u", "NONE", "-i", "NONE", "-n", "-l", str(pScript)],
            check=True,
            timeout=30,
        )


if __name__ == "__main__":
    main()
