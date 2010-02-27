require("tap");
plan(6);

is("\x00", "\000", "NUL byte");
is("\x01", "\001", "1 byte");
is("\x01\x02", "\001\002", "1, 2");
is("\u0001", "\001", "1 byte");
is("\u00a9", "\xc2\xa9", "copyright");
is("\u0000a9", "\xc2\xa9", "copyright");

