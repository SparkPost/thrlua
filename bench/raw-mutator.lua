require("bench.helpers")

-- test raw mutator performance

-- updating a global table
t = {}

run("raw-mutator", function ()
-- incrementing a local counter
local iter = 0;
for j = 1, 10 do
	for i = 0, 1000000 do
		t[i] = j;
		iter = iter + 1
	end
end
end)

