require("bench.helpers")

-- test raw mutator performance

-- build out a data structure to read
local t = {}
local limit = 10000000
for i = 0, limit do
	t[i] = i;
end


run("raw-reader", function ()
for i = 0, limit do
	if t[i] ~= i then
		print("data mismatch", i, t[i])
		error("bang")
	end
end
end)
