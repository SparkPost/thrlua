-- tap.lua
-- Copyright (c) 2008 Message Systems, Inc.
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

function _G.diag(...)
  local message = string.format(...)
  message = string.gsub(message, "\n", "\n# ")
  print("# " .. message)
end

function _G.ok(expr, label)
  if expr then
    print(string.format("ok %d - %s", cur_test, label))
  else
    print(string.format("not ok %d - %s", cur_test, label))
  end
  cur_test = cur_test + 1
  return expr
end

function _G.is(got, expected, label)
  return cmp_ok(got, "=", expected, label)
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
    diag("    " .. tostring(got))
    diag("    " .. rel)
    diag("    " .. tostring(val))
  end
  return pass
end

function _G.like(got, pattern, label)
  return cmp_ok(got, "like", pattern, label)
end

function _G.unlike(got, pattern, label)
  return cmp_ok(got, "unlike", pattern, label)
end


