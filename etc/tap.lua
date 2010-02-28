-- tap.lua
-- Copyright (c) 2008-2010 Message Systems, Inc.
-- Implements a Test::More style test framework
-- vim:ts=2:sw=2:et:

module(..., package.seeall)
local num_planned = 0;
local num_passed = 0;
local num_failed = 0;
local num_skips = 0;
local cur_test = 1;

function _G.plan(num)
  num_planned = num
  print("1.." .. num)
end

local printable = {}
for i = 32, 126 do
  printable[i] = true;
end
printable[10] = true;
printable[9] = true;

function _G.diag(...)
  local message = string.format(...)
  message = '# ' .. string.gsub(message, "\n", "\n# ")
  local escaped = '';
  for i = 1, #message do
    local b = string.byte(message, i)
    if printable[b] then
      escaped = escaped .. string.sub(message, i, i);
    elseif b == 9 then
      escaped = escaped .. "\\t";
    else
      escaped = escaped .. string.format("\\x%x", b);
    end
  end
  print(escaped)
end

function _G.ok(expr, label)
  if label == nil then
    label = ""
  end
  if expr then
    print(string.format("ok %d - %s", cur_test, label))
  else
    print(string.format("not ok %d - %s", cur_test, label))
    diag(debug.traceback(2));
  end
  cur_test = cur_test + 1
  return expr
end

function _G.is(got, expected, label)
  return cmp_ok(got, "=", expected, label)
end

function _G.isnt(got, notexpected, label)
  return cmp_ok(got, '!=', notexpected, label)
end

function _G.cmp_ok(got, rel, val, label)
  local pass = "not implemented"
  local A = got
  local B = val

  -- floating pointer number type...
  if type(A) == "number" then
    A = tostring(A)
  end
  if type(B) == "number" then
    B = tostring(B)
  end

  if rel == ">" then
    pass = A > B
  elseif rel == ">=" then
    pass = A >= B
  elseif rel == "<" then
    pass = A < B
  elseif rel == "<=" then
    pass = A <= B
  elseif rel == "=" then
    pass = A == B
  elseif rel == "==" then
    pass = A == B
  elseif rel == "!=" then
    pass = A ~= B
  elseif rel == "like" then
    pass = string.match(A, B)
  elseif rel == "unlike" then
    pass = not string.match(A, B)
  end
  if not ok(pass, label) then
    diag(" got        " .. tostring(got))
    diag("            " .. rel)
    diag(" expected   " .. tostring(val))
  end
  return pass
end

function _G.like(got, pattern, label)
  return cmp_ok(got, "like", pattern, label)
end

function _G.unlike(got, pattern, label)
  return cmp_ok(got, "unlike", pattern, label)
end

function _G.is_deeply(a, b, label)
  if type(a) ~= type(b) or type(a) ~= 'table' then
    ok(false, label);
    diag('parameters are not tables');
    return false
  end
  local diff = {};
  for k, v in pairs(a) do
    if b[k] ~= v then
      table.insert(diff, k);
    end
  end
  for k, v in pairs(b) do
    if a[k] ~= v then
      table.insert(diff, k);
    end
  end
  table.sort(diff);
  if #diff == 0 then
    return ok(true, label)
  end
  ok(false, label);
  local once = {};
  for _, k in pairs(diff) do
    if not once[k] then
      once[k] = true
      if a[k] ~= nil and b[k] == nil then
        diag('key ' .. k .. ' is present in A but not in B');
      elseif a[k] == nil and b[k] ~= nil then
        diag('key ' .. k .. ' is present in B but not in A');
      else
        diag('expected A[' .. k .. '] = ' .. tostring(a[k]));
        diag('but got  B[' .. k .. '] = ' .. tostring(b[k]));
      end
    end
  end
  return false
end

