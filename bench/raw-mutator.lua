
-- test raw mutator performance

t = {}
local iter = 0;

pcall(function()
	require 'posix'
end)

if not posix then
	posix = {}
	function posix.gettimeofday()
		local t = os.time();
		print("os.time is", t)
		return t, 0;
	end
end

print 'starting iteration test'

local ss, su = posix.gettimeofday()

for j = 1, 10 do
	for i = 0, 1000000 do
		t[i] = j;
		iter = iter + 1
	end
end

local es, eu = posix.gettimeofday()

function difftime(ss, su, es, eu)
  local u = eu - su;
  local s = es - ss;
  if u < 0 then
	s = s - 1
	u = u + 1000000
  end
  return s, u
end

local ds, du = difftime(ss, su, es, eu)

print(string.format("Performed %d iterations", iter));
print(string.format("in %d.%d", ds, du))

