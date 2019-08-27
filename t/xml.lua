-- vim:ts=2:sw=2:et:ft=lua:
require("Test.More");
require("xml");

plan(29);

local xml_sample = [[<?xml version="1.0" encoding="utf-8"?>
<doc>
  <item name="one">
    <property name="prop1" value="value1">hello</property>
  </item>
  <item name="two">
    <property name="prop1" value="value2">there</property>
  </item>
</doc>
]]

local doc = xml.parsexml(xml_sample)

is(doc:tostring(), xml_sample, "tostring operates")
is(tostring(doc), xml_sample, "__tostring operates")

local root = doc:root()
is(root:name(), "doc", "root node name is doc")
is(root:attr("does-not-exist"), nil, "nil for non-existent attr")
root:attr("foo", "bar")
is(root:attr("foo"), "bar", "set foo = bar")
root:attr("foo", nil);
isnt(root:attr("foo"), "bar", "cleared foo = bar")

-- counts the non-text nodes
function count_children(node)
	local counter = 0
	for kid in node:children() do
		if kid:name() != "text" then
			counter = counter + 1
		end
	end
	return counter
end

is(count_children(root), 2, "two item nodes")
like(root:contents(), "hello", "can see hello text")

local n = root:addchild("fruit");
ok(n, "new node")
is(n:name(), "fruit")
like(tostring(doc), "<fruit/>", "can see fruit")
n:contents("and vegetables");
is(n:contents(), "and vegetables", "can see new contents")
is(tostring(n), "<fruit>and vegetables</fruit>", "fruit and veg")

-- what about copying!?
local copy = n:copy();
is(n:name(), copy:name(), "copied name matches")
copy:attr("cloned", "trooper")
is(n:attr("cloned"), nil, "did not modify original")
is(copy:attr("cloned"), "trooper", "updated original")

-- namespaced attr reader
is(copy:attr("cloned"), copy:attrns("cloned"), "attrns gives sane results")
is(copy:attrns("cloned", ""), nil, "attrns with empty namespace -> nil")
is(copy:attrns("cloned", "boo"), nil, "attrns with bogus namespace -> nil")
copy:free()

for n in doc:xpath('//item[@name="one"]') do
	is(n:name(), "item", "found an item")
	is(n:attr("name"), "one", "it's the one we selected")
	like(n:contents(), "hello", "can see hello in there too")
end

-- try it again with context
for n in doc:xpath('//item[@name="one"]', root) do
	is(n:name(), "item", "found an item under root")
	is(n:attr("name"), "one", "it's the one we selected")
	like(n:contents(), "hello", "can see hello in there too")
end

-- and these are for the coverage
error_like(function ()
	root:name("boo");
end, "must be called with no arguments");

n = xml.newnode('foo')
ok(n, "made a node")
is(n:name(), 'foo', 'is called foo')
n:free()
error_like(function ()
  n:name()
end, "bad self")

