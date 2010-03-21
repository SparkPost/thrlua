--[[
 * Copyright (c) 2010 Message Systems, Inc. All rights reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MESSAGE SYSTEMS
 * The copyright notice above does not evidence any
 * actual or intended publication of such source code.
 *
 * Redistribution of this material is strictly prohibited.
]]
-- Convenience accessors for the java bridge
-- The bridge provides low-level access to JNI which is used by this
-- module to provide a more convenient way to work with java

module('java', package.seeall);
require 'javabridge';

local Array = nil;
local Object = nil;

function new(cls, ...)
  if type(cls) == 'string' then
    cls = javabridge.findClass(cls);
  end
  local args = {...};
  local ctors = cls:getConstructors();
--  print(#ctors, ctors);
  local thector = nil
  for i = 0, #ctors - 1 do
    local c = ctors[i];
    local p = c:getParameterTypes();
    if #args == #p then
      -- a candidate
      if #args == 0 then
        -- the only one
        thector = c
        break
      end
    end
  end
  if thector == nil then
    error("could not find a suitable constructor for " .. tostring(cls));
  end
  if Array == nil then
    Array = javabridge.findClass('java/lang/reflect/Array'):newInstance();
  end
  if Object == nil then
    Object = javabridge.findClass('java/lang/Object');
  end
  local a = nil;
  if #args > 0 then
    a = Array.newInstance(Object, #args);
    for i = 0, #args - 1 do
      Array.set(a, i, args[i+1])
    end
  end
  local r = thector:newInstance(a);
  return r;
end

-- vim:ts=2:sw=2:et:
