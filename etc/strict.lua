--
-- strict.lua
-- checks uses of undeclared global variables
-- All global variables must be 'declared' through a regular assignment
-- (even assigning nil will do) in a main chunk before being used
-- anywhere or assigned to inside a function.
--

local getinfo, error, rawset, rawget = debug.getinfo, error, rawset, rawget

local mt = getmetatable(_G)
if mt == nil then
  mt = {}
  setmetatable(_G, mt)
end

-- The _G table is not reinitialised across "config reload"s in Momentum.
-- So, the first time that strict is loaded,
-- we need to save the metamethods from the old _G metatable,
-- and chain to those metamethods.
if mt.__strict == nil then
  mt.__strict = true

  mt.__chain_newindex = mt.__newindex
  mt.__chain_index = mt.__index
  mt.__exceptions = {}

  -- Detect if thread-local storage is defined (as it would be under thrlua).
  for _, k in ipairs{ "_TLS", "_OSTLS" } do
    if _G[k] ~= nil then
      mt.__exceptions[k] = true
    end
  end
end

-- Always compute these from scratch -- global variables
-- will need to be redeclared/redefined when scripts are loaded.
mt.__declared = {}

local function what ()
  local d = getinfo(3, "S")
  return d and d.what or "C"
end

mt.__newindex = function (t, n, v)
  if not mt.__declared[n] and not mt.__exceptions[n] then
    local w = what()
    if w ~= "main" and w ~= "C" then
      error("assign to undeclared variable '"..n.."'", 2)
    end
    mt.__declared[n] = true
  end

  if mt.__chain_newindex ~= nil then
    mt.__chain_newindex(t, n, v)
  else
    rawset(t, n, v)
  end
end
  
mt.__index = function (t, n)
  if not mt.__declared[n] and not mt.__exceptions[n] and what() ~= "C" then
    error("variable '"..n.."' is not declared", 2)
  end

  if mt.__chain_index ~= nil then
    return mt.__chain_index(t, n)
  else
    return rawget(t, n)
  end
end

