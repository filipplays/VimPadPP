-- Load the plugin's bundled defaults first; the user's config may override them.
local sBootstrap = debug.getinfo(1, "S").source:sub(2)
local sNeovimDir = vim.fn.fnamemodify(sBootstrap, ":h")
local sConfigDir = vim.fn.fnamemodify(sNeovimDir, ":h") .. "/config"

local function load_config()
  local sDefaults = sNeovimDir .. "/init.lua"
  if vim.fn.filereadable(sDefaults) == 1 then
    dofile(sDefaults)
  elseif vim.fn.filereadable(sNeovimDir .. "/init.vim") == 1 then
    vim.cmd("source " .. vim.fn.fnameescape(sNeovimDir .. "/init.vim"))
  end

  vim.opt.runtimepath:prepend(sConfigDir)
  vim.opt.runtimepath:append(sConfigDir .. "/after")
  local sLua = sConfigDir .. "/init.lua"
  local sVim = sConfigDir .. "/init.vim"
  local bLua = vim.fn.filereadable(sLua) == 1
  local bVim = vim.fn.filereadable(sVim) == 1
  if bLua and bVim then
    error("Both init.lua and init.vim exist in " .. sConfigDir .. "; keep only one.")
  end
  if bLua or bVim then
    vim.cmd("source " .. vim.fn.fnameescape(bLua and sLua or sVim))
  end
end

local bOk, sError = xpcall(load_config, debug.traceback)
if bOk then
  local sBufferName = vim.fn.tempname() .. "-vim-motions-buffer"
  vim.api.nvim_buf_set_name(0, sBufferName)
  vim.bo.buftype = "acwrite"
  vim.api.nvim_create_autocmd("BufWriteCmd", {
    pattern = vim.fn.fnamemodify(sBufferName, ":t"),
    callback = function()
      vim.rpcnotify(1, "vim_motions_save")
      vim.bo.modified = false
    end,
  })
end
vim.g.vim_motions_startup_error = bOk and "" or tostring(sError)
