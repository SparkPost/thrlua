require 'Test.More';
plan(11);

require 'javabridge';
require 'java';

like(javabridge.DEFAULT_JVM, "libjvm", javabridge.DEFAULT_JVM);
is(javabridge.startVM(), true, "started vm");

error_is(function ()
	javabridge.findClass("an invalid class name");
end, "java.lang.NoClassDefFoundError: an invalid class name");

strc = javabridge.findClass("java/lang/String");
is(type(strc), "userdata", "got string class");
is(tostring(strc.getName), 'public java.lang.String java.lang.Class.getName()');
name = strc:getName();
is(tostring(name), 'java.lang.String');
is(tostring(strc), 'class java.lang.String');
name = strc.getName;
is(tostring(name(strc)), 'java.lang.String');

h = name.hashCode;
is(tostring(h), 'public int java.lang.reflect.Method.hashCode()');
is(tostring(h:getReturnType()), 'int');
-- l = h(name);
is(tostring(name:getReturnType()), 'class java.lang.String');

p = java.new('java/util/Properties');
c = p.getClass;
diag(type(c));
diag(c);
is(tostring(p:getClass()), 'class java.util.Properties');
is(tostring(p), '{}');

l = java.new('java/lang/Double', 2.5);
is(tostring(l), '2.5');


is(javabridge.stopVM(), true, "stopped vm");


