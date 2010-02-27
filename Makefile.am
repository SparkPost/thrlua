AUTOMAKE_OPTIONS = subdir-objects
hdrdir = $(includedir)/rclua
cmoddir = $(prefix)/lib/lua/@PACKAGE_VERMAJORMINOR@
smoddir = $(prefix)/share/lua/@PACKAGE_VERMAJORMINOR@

lib_LTLIBRARIES = librclua.la
bin_PROGRAMS = rclua rcluac

hdr_HEADERS = src/lua.h src/luaconf.h src/lauxlib.h src/lualib.h \
	rcluaconfig.h etc/lua.hpp

noinst_HEADERS = src/lapi.h src/lcode.h src/ldebug.h src/ldo.h \
	src/lfunc.h src/lgc.h src/llex.h src/llimits.h src/lmem.h \
	src/lobject.h src/lopcodes.h src/lparser.h src/lstate.h \
	src/lstring.h src/ltable.h src/ltm.h src/lundump.h src/lvm.h src/lzio.h 

librclua_la_LDFLAGS = -lpthread -ldl -lm 
librclua_la_SOURCES = \
	src/lapi.c src/lcode.c src/ldebug.c src/ldo.c src/ldump.c src/lfunc.c \
	src/lgc.c src/llex.c src/lmem.c src/lobject.c src/lopcodes.c \
	src/lparser.c src/lstate.c src/lstring.c src/ltable.c src/ltm.c  \
	src/lundump.c src/lvm.c src/lzio.c src/lauxlib.c src/lbaselib.c \
	src/ldblib.c src/liolib.c src/lmathlib.c src/loslib.c src/ltablib.c \
	src/lstrlib.c src/loadlib.c src/linit.c

# lua executable
rclua_SOURCES = src/lua.c
rclua_LDADD = -lrclua
rclua_LDFLAGS = @RL_LDFLAGS@
rclua_CPPFLAGS = @RL_CFLAGS@

# lua compiler
rcluac_SOURCES = src/luac.c src/print.c
rcluac_LDADD = -lrclua 

EXTRA_DIST = COPYING HISTORY INSTALL README

smod_HEADERS = etc/strict.lua etc/tap.lua

dist_man_MANS = doc/rclua.1 doc/rcluac.1

.PHONY: test

test:
	./runtests
