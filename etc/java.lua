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
local lua_to_jtype = {
  ["number"] = "java/lang/Double";
  ["string"] = "java/lang/String";
};
local java_primitives = {
  ["double"] = "java/lang/Double";
};
function new(cls, ...)
  if type(cls) == 'string' then
    cls = javabridge.findClass(cls);
  end

  local args = {...};
  local argtypes = {};
  for i = 1, #args do
    local t = type(args[i]);
    if t == 'userdata' then
      t = args[i].getClass;
      if t ~= nil then
        t = t();
      else
        error("Unable to pass " .. args[i] .. " to java");
      end
    else
      if lua_to_jtype[t] ~= nil then
        t = javabridge.findClass(lua_to_jtype[t]);
      else
        error("don't know how to map lua type " .. t .. " to java");
      end
    end
    argtypes[i-1] = t;
    diag(string.format("argtypes[%d] = %s", i-1, tostring(t:getName())));
  end

  local ctors = cls:getConstructors();
  local thector = nil
  local choices = {};
  for i = 0, #ctors - 1 do
    local c = ctors[i];
    local t = c:getGenericParameterTypes();
    if #t == #args then
      local cdata = {};
      cdata.ctor = c;
      cdata.label = tostring(c:toGenericString());
      cdata.ptypes = {};
      local compat = true
      for j = 0, #t - 1 do
        local jt = t[j];

        -- if javabridge.isAssignableFrom(jt, argtypes[j]) then
        if jt:isAssignableFrom(argtypes[j]) then
          cdata.ptypes[j] = jt;
          diag(string.format("ctor %d: param %d: %s (compatible with %s)",
            i, j, tostring(jt), tostring(argtypes[j])))
        else
          -- maybe it's a primitive?
          jt = tostring(jt);
          if java_primitives[jt] == nil then
            diag(string.format("%s is not assignable from %s",
              tostring(jt), tostring(argtypes[j])));
            compat = false
          else
            jt = javabridge.findClass(java_primitives[jt]);
            if javabridge.isAssignableFrom(jt, argtypes[j]) then
              cdata.ptypes[j] = jt;
              diag(string.format("ctor %d: param %d: %s (compatible with %s)",
                i, j, tostring(jt), tostring(argtypes[j])))
            else
              compat = false
            end
          end
        end
      end
      if compat then
        table.insert(choices, cdata);
      end
    end
  end

  if #choices == 0 then
    error(string.format("class %s has no callable constructors",
      tostring(cls:getName())));
  end

  -- now we have two pieces of information:
  --   argtypes: an array representing the types of the args passed to new
  --   choices: an array with information about available ctors
  if #choices > 1 then
    error("could not find a suitable constructor for " .. tostring(cls));
  else
    thector = choices[1].ctor;
  end
  if Array == nil then
    Array = javabridge.findClass('java/lang/reflect/Array'):newInstance();
  end
  if Object == nil then
    Object = javabridge.findClass('java/lang/Object');
  end
  local a = nil;
  diag("calling with " .. #args .. " args");
  if #args > 0 then
    a = Array.newInstance(Object, #args);
    for i = 0, #args - 1 do
      diag("populating arg " .. i .. " " .. tostring(args[i+1]));
      Array.set(a, i, args[i+1])
    end
  end
  local r = thector:newInstance(a);
  return r;
end

-- vim:ts=2:sw=2:et:
