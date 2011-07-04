# Differences from Stock Lua

This version of Lua has a number of enhancements that alter its behavior
from that of the standard Lua distribution.  Each of these changes are
detailed below.

## Hex escapes in strings

The following two statements are equivalent in Threaded Lua; the latter
is not possible in stock Lua:

    s = string.char(0xaa)
    s = "\xaa"

In addition, Threaded Lua supports unicode escapes via "\uAAAA" and
"\uBBBBBB".  These are 4 and 6 digit hex numbers specifying unicode code
points.  When these strings are parsed, they are internally encoded as a
UTF-8 encoded byte sequence.

    print("\u00a3") -- emits POUND SIGN, assuming your terminal is utf-8

This feature was inspired by equivalent functionality in the [literals
power patch][literals].

## Octal literals for numbers

To make it easier to work with POSIX style functions, this feature
causes the parser to interpret a literal number in base 8 (octal) when
it has a leading 0.  The following two lines are equivalent in Threaded
Lua, but the latter does not behave quiet as you might expect in stock
lua:

    mode = 432
    mode = 0660 -- stock lua treats this as decimal 660 !

## Bitwise Operators

To make it easier to work with libraries that use bitmasks as part of
their API, this feature enables bitwise logical operators as detailed
below.  The bitwise operators first convert their numeric operands to an
integer, apply the operator and then generate a standard lua number
result.

    a & b -- bitwise AND, uses the __and metatable event (if defined)
    a | b -- bitwise OR            __or
    a ^^ b -- bitwise XOR          __xor
    a << 1 -- bitwise shift left   __shl
    a >> 1 -- bitwise shift right  __shr
    ~a     -- bitwise negation     __not
    a \ 2  -- integer division     __intdiv

In addition, an alternative "not equals" syntax is provided as an aid to
those coming to Lua from other languages.  The following two lines are
equivalent in Threaded Lua, with the second line causing a syntax error
in stock Lua:

    if a ~= 2 then print("not two") end
    if a != 2 then print("not two") end

This feature set is inspired by the [bitwise power patch][bitwise].

## Iterator metatable event

The "pairs" function provides a means for iterating over tables, but
stock lua does not provide a generic mechanism for userdata to
participate in the generic for loop construct, pushing this to the
module author either as a helper function that must be imported, or a
helper method that must be provided on the object in question.  Both of
these solutions "pollute" the namespace, with the latter risking
collision with the properties or methods that might be present on the
object being exposed.

This is why Threaded Lua provides an "__iter" metatable event; here's
how it works:

When executing a generic for loop, if the parameter specified by the
"in" clause is not a function, the VM will attempt to resolve the
"__iter" metatable event.  It is expected that this will be a function
with the following signature:

    function iter_event(object)
      return iterfunc, state, initial
    end

The return values from the iter_event are used in the for construct.
The following two pieces of code are equivalent in effect:

    t = {"one", "two"}
    for _, v in ipairs(t) do
      print(v)
    end

And:

    t = {"one", "two"}
    setmetatable(t, { __iter = ipairs });
    for _, v in t do -- note that the table is used in the "in" clause
      print(v)
    end

This functionality is inspired by the [iterator power patch][iter], although
the implementation in Threaded Lua does not change the semantics of the
runtime when the "in" clause is not a function and does not provide the
"__iter" metatable event.

## Threads

One of the most significant changes in Threaded Lua is (not
surprisingly) the introduction of threading support.  This is different
from the coroutine support that exists in stock Lua in several ways, but
fundamentally allows an embedding application the ability to migrate a
lua_State between operating system threads with using data marshalling
tricks, and also allows a Lua script to spawn threads!

Threading support is founded on an important principle:

> When multiple threads are in use with the same global state,
> concurrent operations on the same variables/objects are both
> possible and safe to make in that they will not result in the
> application faulting.  However, the burden of consistency is on the
> script author to employ appropriate locks in scenarios where multiple
> threads are reading and writing to the same variables/objects.

### Scoping

The semantics of variables and scoping, while technically unchanged from
the stock Lua behavior, are worth calling out when it comes to their
interaction with threads.  All of these points are applicable to
lua_States (the internal reprsentation of coroutines) that share the same
global state.  In Message Systems applications, we typically create one
global state for the whole process.

 * Global variables are readable and writable to all threads
 * Up-Values (variables used in closures) are readable and writable to
   all threads that execute the associated closure.
 * Use the "local" keyword as best-practice habit, especially inside
   function bodies, to avoid unintentional consistency problems with
   multi-threaded scripts

The following code sketch provides some tips on using scoping correctly:

    global_counter = 1 -- this global is visible to all threads and to
                       -- functions in other modules
    local upval_counter = 2 -- this file scoped variable is visible to
                            -- all threads and functions that follow its
                            -- declaration in the current file.
                            -- Note that this type of variable is
                            -- typically visible and usable only in
                            -- closures in most Message Systems'
                            -- integrations; it is not a complete substitute
                            -- for a global variable
    function func(arg1) -- the arg1 name is newly created and local to each
                        -- thread that executes "func", however, the
                        -- parameter it was passed may be visible to other
                        -- threads, depending on the nature of the script!
      mistake = 1       -- mistake is a global being changed without
                        -- guarding the access with a mutex; undesirable
                        -- consistency problems will ensue in threaded
                        -- scripts!
      upval_counter = upval_counter + 1 -- no mutex is used here either; same
                                        -- caveats as "mistake"
      local val = 1     -- similar to "arg1", "val" is local to the current
                        -- thread.  However, if it is used in a closure,
                        -- then see "upval_counter" above!
    end


### require 'threads'

A "threads" module is provided; it enables thread creation and the use
of mutexes.  Consult its documentation for more details.






 [literals]: svn://slugak.dyndns.org/public/lua-patches/literals.patch
 [bitwise]: http://darkmist.newmail.ru/bitwise_operators_for_lua_5.1.3.patch
 [iter]:
 http://lua-users.org/files/wiki_insecure/power_patches/5.1/jh-lua-iter-5.1.4.patch

