-- vim:ts=2:sw=2:et:ft=lua:
-- GC trace threads must not block fault signals. A fault on a blocked
-- signal kills the process without running the host's handler.
require('Test.More');

local function read(path)
  local f = io.open(path);
  if not f then return nil end
  local s = f:read('*a');
  f:close();
  return s;
end

if not read('/proc/self/status') then
  skip_all('needs /proc/<pid>/task (Linux)');
  return;
end

-- bit (sig - 1) of a hex signal mask from /proc/<pid>/task/<tid>/status
local function blocked(mask, sig)
  local low = tonumber(mask:sub(-4), 16);
  return math.floor(low / 2 ^ (sig - 1)) % 2 == 1;
end

-- POSIX numbers on Linux
local SIGILL, SIGBUS, SIGFPE, SIGSEGV, SIGTERM = 4, 7, 8, 11, 15;

local pid = read('/proc/self/stat'):match('^(%d+)');
local masks = {};
local ls = io.popen('ls /proc/' .. pid .. '/task');
for tid in ls:lines() do
  local comm = read('/proc/' .. pid .. '/task/' .. tid .. '/comm');
  local status = read('/proc/' .. pid .. '/task/' .. tid .. '/status');
  if comm and status and comm:match('^lua%-gtrace') then
    masks[#masks + 1] = status:match('SigBlk:%s*(%x+)');
  end
end
ls:close();

if #masks == 0 then
  skip_all('no lua-gtrace threads (trace threads or thread names disabled)');
  return;
end

plan(5);

local function none_block(sig)
  for _, m in ipairs(masks) do
    if blocked(m, sig) then return false end
  end
  return true;
end

ok(none_block(SIGSEGV), 'trace threads do not block SIGSEGV');
ok(none_block(SIGBUS), 'trace threads do not block SIGBUS');
ok(none_block(SIGILL), 'trace threads do not block SIGILL');
ok(none_block(SIGFPE), 'trace threads do not block SIGFPE');
ok(not none_block(SIGTERM), 'trace threads still block asynchronous signals');
