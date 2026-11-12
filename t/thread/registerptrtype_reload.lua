-- vim:ts=2:sw=2:et:ft=lua:
-- #35: re-running luaopen_json (package.loaded.json = nil; require "json")
-- while other threads index json objects must not rewrite the shared
-- metatable. Before the fix, luaL_registerptrtype set __index to the
-- metatable and then to a new C closure, and a concurrent reader could see
-- a nil field, an "attempt to call" error, or segfault on a torn __index.
require('Test.More');
plan(7);

local json = require('json');
local o = json.decode('{"quotas":{"daily":10,"monthly":100}}');
local mt = getmetatable(o);
local idx = rawget(mt, "__index");
is(type(idx), "function", "json metatable __index is a function");

package.loaded.json = nil;
json = require('json');
ok(rawequal(getmetatable(o), mt), "re-require keeps the metatable");
ok(rawequal(rawget(mt, "__index"), idx), "re-require does not rewrite __index");
is(o.quotas.daily, 10, "existing object still indexes after re-require");

-- Concurrent: workers index a long-lived object while the main thread keeps
-- re-requiring json, the way a Momentum config reload does.
local DURATION = 2;
local WORKERS = 4;
local stop = false;
local nils, errs, reloads = {}, {}, 0;
local records = {};
for i = 1, 16 do
  records[i] = json.decode(string.format('{"quotas":{"daily":%d}}', i));
end

local function op(i)
  local q = records[i].quotas;
  if q == nil then return false end
  return q.daily == i;
end

local workers = {};
for w = 1, WORKERS do
  workers[w] = thread.create(function (t)
    local i, z, e = w, 0, 0;
    while not stop do
      i = i % 16 + 1;
      local ok, r = pcall(op, i);
      if not ok then e = e + 1 elseif not r then z = z + 1 end
    end
    nils[w] = z;
    errs[w] = e;
  end);
end

local deadline = os.time() + DURATION;
while os.time() < deadline do
  package.loaded.json = nil;
  json = require('json');
  reloads = reloads + 1;
end
stop = true;
local z, e = 0, 0;
for w = 1, WORKERS do
  workers[w]:join();
  z = z + nils[w];
  e = e + errs[w];
end

ok(reloads > 0, "main thread re-required json");
is(z, 0, "no nil reads of json fields during re-require");
is(e, 0, "no errors indexing json objects during re-require");
