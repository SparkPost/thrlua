-- vim:ts=2:sw=2:et:ft=lua:
require("Test.More")
require("json")

plan(151);

local json_string = [[{"foo": "bar"}]]

o = json.decode(json_string)
ok(o, "parsed string")
is(tostring(o), [[{ "foo": "bar" }]], "__tostring works")

is(o.foo, "bar", "__index read of existing prop")
o.num = 42
is(o.num, 42, "number round-tripped")
o.str = "hello world"
is(o.str, "hello world", "string round-tripped")

local array = { 0, 1, 3 }

is(tostring(json.encode(array)), "[ 0, 1, 3 ]", "encoder works")

o.arr = array
is(#o.arr, 3, "json array is size 3")
is(#o, 0, "json object is size 0")

local got = {}
for k, v in o.arr do
	table.insert(got, v)
  is(o.arr[k], v, "sanity check array index operator")
end
is(#got, #array, "got all elements")
for k, v in ipairs(array) do
  is(got[k], v)
end

is(tostring(o), [[{ "foo": "bar", "num": 42, "str": "hello world", "arr": [ 0, 1, 3 ] }]])

o.obj = { lemon = "barley" }
is(tostring(o), [[{ "foo": "bar", "num": 42, "str": "hello world", "arr": [ 0, 1, 3 ], "obj": { "lemon": "barley" } }]])

kid = json.new()
kid.name = "kid"
is(tostring(kid), [[{ "name": "kid" }]])

o.kid = kid
is(tostring(o), [[{ "foo": "bar", "num": 42, "str": "hello world", "arr": [ 0, 1, 3 ], "obj": { "lemon": "barley" }, "kid": { "name": "kid" } }]])

is(o.notset, nil, "no such value")

-- can we delete a value?
is(o.obj.lemon, "barley", "can see lemon value")
o.obj.lemon = nil
is(o.obj.lemon, nil, "can no longer see lemon value")

local expectobj = {
  arr = "[ 0, 1, 3 ]",
  foo = "bar",
  num = "42",
  str = "hello world",
  obj = "{ }",
  kid = [[{ "name": "kid" }]]
}
got = {}
for k, v in o do
  got[k] = tostring(v)
end
is_deeply(got, expectobj, "iterated object")

diag(o.arr)
o.arr[1] = 42
is(o.arr[1], 42, "set array")
is(tostring(o.arr), "[ 42, 1, 3 ]", "set array correctly")

o.bool = true
is(o.bool, true, "set true")
o.bool = false
is(o.bool, false, "set false")


o.float = 1.5
is(o.float, 1.5, "can round-trip floats")
is(tostring(o.float), "1.5", "1.5 is 1.5")
like(tostring(o), [["float": 1.5%d*]])

-- integer number handling bounds check.
-- Our mapper has an is-integer check that needs specific boundary
-- checking to ensure that it doesn't truncate or cast away significant
-- information about the value; test across that boundary to verify
-- that we see something equivalent to what we put in
local int_max = 2147483647
for bigger_than_int = int_max - 10, int_max + 10 do
  o.bigint = bigger_than_int
  is(o.bigint, bigger_than_int,
    "didn't break big number value " .. bigger_than_int)
end
local int_min = -int_max - 1
for smaller_than_int = int_min - 10, int_min + 10 do
  o.smallint = smaller_than_int
  is(o.smallint, smaller_than_int,
    "didn't break small number value " .. smaller_than_int)
end

ok(json.ERROR_DEPTH, "constants are registered")
is(json.strerror(json.ERROR_PARSE_NULL), "null expected", "strerror works")
is(json.strerror(-1), "unknown error")
is(json.strerror(20000), "unknown error")

-- null vs nil
-- The json-c library doesn't allow us to distinguish the two, which is
-- unfortunate from a high-fidelity perspective, but in practical terms
-- it should not pose a huge problem
null = json.decode([[{ "null": null }]])
is(null.null, nil, "can't tell null from nil")
is(null.nothere, nil, "can't tell nil from null")
is(tostring(null), [[{ "null": null }]])
null.null = nil
is(null.null, nil, "can't tell if we deleted null")
is(tostring(null), [[{ }]], "deleted null")


fail, code, str = json.decode([[{ bad: "json" }]])
is(fail, nil, "error indicated")
is(code, json.ERROR_PARSE_OBJECT_KEY_NAME)
is(str, "quoted object property name expected at offset 2", str)

error_like(function ()
  o.fail = coroutine.create(function () return 1 end)
end, "cannot assign values of type thread")

error_like(function ()
  o.arr[1] = nil
end, "cannot delete from json arrays")

error_like(function ()
  o.arr["moo"] = "bang"
end, "json array index must be numeric")

error_like(function ()
  o.arr[0] = "bang"
end, "lua array semantics are that index operations are 1")

local mixedbag = { foo = "bar", [32] = "dude" }

local mj = json.encode(mixedbag)
is(mj["32"], "dude", "saw integer key")
is(mj.foo, "bar", "saw string key")

o = json.encode(42)
is(type(o), "userdata", "42 => userdata")
error_like(function ()
  o.foo = "bang"
end, "cannot assign to this type of json object")
is(o.foo, nil, "no props in a json integer")

nprops = 0
for k, v in o do
  nprops = nprops + 1
  diag(k, v)
end
is(nprops, 0, "no properties iterated")

-- Nested tables
local nested = {
  foo = {
    bar = 32,
    baz = "quux"
  }
}
local nj = json.encode(nested)
is(nj.foo.bar, 32, "saw integer value")
is(nj.foo.baz, "quux", "saw string value")

local nested2 = {
  val = "hello",
  val2 = "hello again",
  t = {
   t2 = {
     t3 = {
       nested = true,
       t4 = {
         [ "very_nested" ] = true
       }
     }
   }
  },
  trailer = "bye",
  trailer2 = false
}
local nj2 = json.encode(nested2)
is(nj2.val, "hello")
is(nj2.val2, "hello again")
is(nj2.t.t2.t3.nested, true)
is(nj2.t.t2.t3.t4.very_nested, true)
is(nj2.trailer, "bye")
is(nj2.trailer2, false)

-- Table nested in array
local nj3 = json.new()

local nested3_id = "1234567890123456789"
local nested3_str = "0:1234567890123456%ab1234c5d6ef7abc"
local nested3 = {
  [ 1 ] = {
    [ "message_id" ] = nested3_str
  }
}

nj3.multicast_id = nested3_id
nj3.results = nested3

is(nj3.multicast_id, nested3_id)
is(nj3.results[1].message_id, nested3_str, "Table nested in array") 

nj3.results[2] = { [ "message_id" ] = nested3_str }
is(nj3.results[2].message_id, nested3_str, "Table nested in array 2")

-- Array nested in array
local nj4 = json.new()

local nested4 = {
  [ 1 ] = {
    {
      [ "flag" ] = true
    },
    {
      [ "flag2" ] = false
    }
  }
}

nj.results = nested4

is(nj.results[1][1].flag, true, "Table nested in array nested in array")
is(nj.results[1][2].flag2, false, "Table nested in array nested in array 2")

-- json_is_array
local jia_str = [[
{
  "recipients": [
  ],
  "recipients2": [
    "fred@domain.com",
  ],
  "obj": {
  },
  "obj2": {
    "foo": "bar"
  },
  "bool": true,
  "number": 42,
  "str": "string",
  "anull": null
}
]]

local jia = json.decode(jia_str)

ok(json.is_json(jia), "JSON userdata recognised")
is(json.is_json("notjson"), false, "Non-JSON userdata not detected as JSON")

ok(json.is_array(jia.recipients), "is_array on empty array")
ok(json.is_array(jia.recipients2), "is_array on non-empty array")
is(json.is_array(jia.obj), false, "is_array on empty object")
is(json.is_array(jia.obj2), false, "is_array on non-empty object")
is(json.is_array(jia.bool), false, "is_array on boolean")
is(json.is_array(jia.number), false, "is_array on number")
is(json.is_array(jia.str), false, "is_array on string")
is(json.is_array(jia.anull), false, "is_array on null")

-- json_is_object
ok(json.is_object(jia), "is_object on JSON")
ok(json.is_object(jia.obj), "is_object on empty object")
ok(json.is_object(jia.obj2), "is_object on non-empty object")
is(json.is_object(jia.recipients), false, "is_object on empty array")
is(json.is_object(jia.recipients2), false, "is_object on non-empty array")
is(json.is_object(jia.bool), false, "is_object on boolean")
is(json.is_object(jia.number), false, "is_object on number")
is(json.is_object(jia.str), false, "is_object on string")
is(json.is_object(jia.anull), false, "is_object on null")

-- json_is_boolean
ok(json.is_boolean(jia.bool), "is_boolean on boolean")
is(json.is_boolean(jia.recipients), false, "is_boolean on empty array")
is(json.is_boolean(jia.recipients2), false, "is_boolean on non-empty array")
is(json.is_boolean(jia.obj), false, "is_boolean on empty object")
is(json.is_boolean(jia.obj2), false, "is_boolean on non-empty object")
is(json.is_boolean(jia.number), false, "is_boolean on number")
is(json.is_boolean(jia.str), false, "is_boolean on string")
is(json.is_boolean(jia.anull), false, "is_boolean on null")

-- json_is_number
ok(json.is_number(jia.number), "is_number on number")
is(json.is_number(jia.recipients), false, "is_number on empty array")
is(json.is_number(jia.recipients2), false, "is_number on non-empty array")
is(json.is_number(jia.bool), false, "is_number on boolean")
is(json.is_number(jia.obj), false, "is_number on empty object")
is(json.is_number(jia.obj2), false, "is_number on non-empty object")
is(json.is_number(jia.str), false, "is_number on string")
is(json.is_number(jia.anull), false, "is_number on null")

-- json_is_string
ok(json.is_string(jia.str), "is_string on string")
is(json.is_string(jia.recipients), false, "is_string on empty array")
is(json.is_string(jia.recipients2), false, "is_string on non-empty array")
is(json.is_string(jia.bool), false, "is_string on boolean")
is(json.is_string(jia.number), false, "is_string on number")
is(json.is_string(jia.obj), false, "is_string on empty object")
is(json.is_string(jia.obj2), false, "is_string on non-empty object")
is(json.is_string(jia.anull), false, "is_string on null")

-- json_is_null
local jin_str = [[
{ "fred": null }
]]

local jin = json.decode(jin_str)
ok(json.is_null(jin.fred), "null value for key")

