
-- test raw mutator performance

t = {}
local iter = 0;

require 'posix'

print 'starting iteration test'

local ss, su = posix.gettimeofday()

for j = 1, 10 do
	for i = 0, 1000000 do
		t[i] = j;
		iter = iter + 1
	end
end

local es, eu = posix.gettimeofday()

print(string.format("Performed %d iterations", iter));
print(string.format("in %d.%d", es - ss, eu - su));

