# JSON bindings for Lua

The JSON module provides access to the JSON encoding and decoding
functions provided by the json-c library.

The interface uses the metatable features of Lua to make the parsed JSON
datastructures appear as natural tables.

Due to limitations of the json-c library, it is impossible to
distinguish between JSON values having the "null" type and the Lua "nil"
value.

## Synopsis

    require 'json'

    obj, code, err = json.decode([[{ "hello": "world" }]])
    if not obj then
      error(err)
    end
    print(obj.hello)
    obj.int = 4
    obj.num = 3.5
    print(obj) -- { "hello": "world", "int": 4, "num": 3.500000 }

    obj.arr = { 1, 2, 3 }
    print(obj.arr) -- [ 1, 2, 3 ]

    obj.obj = { name = "value" }
    print(obj.obj) -- { "name": "value" }

    print(json.encode { hello = "world" }) -- { "hello": "world" }

## obj, code, str = json.decode(jsonstring)

Parses the jsonstring parameter and returns a json object
representation.  If the string cannot be parsed, this function will
return three values:

 * nil to indicate that the parse failed
 * an error code (see ERROR CODES below)
 * a human readable error string

A successful parse returns the native lua representation of scalar
values that have a direct mapping:

 * boolean
 * numeric
 * string
 * null

Objects and arrays are returned as a userdata with a metatable; they
have the following behaviors:

### tostring(jsonobj)

When cast to a string, the json object will return the json string
representation of the object or array.

### index and newindex

The json object allows key/value pairs to be set on json objects, or
integer offset and value pairs for json arrays.

json arrays accept only integer keys and the indices are 1-based rather
than 0-based to match up to the prevailing lua semantic.

json objects accept only string keys; if you attempt to use a non-string
key, the bindings will attempt to convert it to a string.

Note that assignments to object/array indices are subject to the same
ASSIGNMENT RULES as listed below.

### iteration

The json object metatable provides an __iter method so that you can
naturally iterate the key/value pairs for objects or the index/value
pairs for arrays using the following idiom:

    for k, v in obj do
      print(k, v)
    end

## jsonobj = json.new()

Creates a new, empty json object (of type object).
This is useful for building up json data from scratch:

    o = json.new()
    o.name = "value"
    print(o) -- { "name": "value" }

## jsonobj = json.encode(value)

Creates a json object representation of the provided parameter.  This is
used as a stepping stone for generating a json string version of the
same value:

    o = json.encode({ name = "value" })
    print(o.name) -- value
    -- note that print implicitly calls tostring
    print(o) -- { "name": "value" }

    -- get the encoded value as a string to pass somewhere else
    encoded = tostring(o)

### Assignment Rules

When mapping from a lua value to a json object representation, the
following rules apply:

 * If the value is itself a json object, a reference to that object is
   used and the assignment operation is very cheap.
 * If the value is numeric, boolean or string, a copy of the value is
   created and used.
 * If the value is a table, the correct mapping to JSON is ambiguous due
   to tables in Lua being overloaded to represent both arrays and
   objects.  The following simple rule is used to determine how to map a
   Lua table: if the table has a value for numeric index 1 (the first
   element of an array style table), then it is assumed to be an array,
   otherwise an object.
 * When mapping an array style table per the rule above, the mapper will
   read the first n contiguous array values starting at index 1 and
   stopping at the first nil value in the array.
 * When mapping an object style table per the rule above, the mapper
   will copy all the values from the table and apply them to the
   constructed json object, using the string representation of the keys
   and array indices.  In laymans terms, this means that if you have
   a table with both string and integer keys, the integer keys will be
   converted to strings in the generated object.
 * userdata, closures and other non-scalar values cannot be assigned to
   a json object and will raise a runtime error


## str = json.strerror(code)

Returns a human readable descriptive reason for the error code.
The error code is returned from json.decode when a parse fails.

## Error Codes

The follow constants are defined and can be used to determine the cause
of failure in certain json operations; you may pass any of these to
json.strerror to obtain a human readable description:

   * json.ERROR\_DEPTH
   * json.ERROR\_PARSE\_EOF
   * json.ERROR\_PARSE\_UNEXPECTED
   * json.ERROR\_PARSE\_NULL
   * json.ERROR\_PARSE\_BOOLEAN
   * json.ERROR\_PARSE\_NUMBER
   * json.ERROR\_PARSE\_ARRAY
   * json.ERROR\_PARSE\_OBJECT\_KEY\_NAME
   * json.ERROR\_PARSE\_OBJECT\_KEY\_SEP
   * json.ERROR\_PARSE\_OBJECT\_VALUE\_SEP
   * json.ERROR\_PARSE\_STRING
   * json.ERROR\_PARSE\_COMMENT

