AUTOMAKE_OPTIONS = subdir-objects
hdrdir = $(includedir)/rclua
cmoddir = $(prefix)/lib/lua/@PACKAGE_VERMAJORMINOR@
smoddir = $(prefix)/share/lua/@PACKAGE_VERMAJORMINOR@
MD_FILES = @MD_FILES@

MAKEFLAGS = -s

lib_LTLIBRARIES = libthrlua.la
bin_PROGRAMS = rclua rcluac
cmod_LTLIBRARIES = pcre.la mathx.la javabridge.la lpeg.la yaml.la \
	posix.la xml.la curl.la socket.la

if BUILD_JSON
cmod_LTLIBRARIES += json.la
endif

hdr_HEADERS = src/lua.h src/luaconf.h src/lauxlib.h src/lualib.h \
	rcluaconfig.h etc/lua.hpp

noinst_HEADERS = src/lcode.h src/ldebug.h src/ldo.h \
	src/lfunc.h src/lgc.h src/llex.h src/llimits.h src/lmem.h \
	src/lobject.h src/lopcodes.h src/lparser.h src/lstate.h \
	src/lstring.h src/ltable.h src/ltm.h src/lundump.h src/lvm.h src/lzio.h 

CFLAGS += @CK_CFLAGS@ -Isrc

libthrlua_la_LDFLAGS = -lpthread -lm
libthrlua_la_CPPFLAGS = $(commonCFLAGS)
libthrlua_la_SOURCES = \
	src/lapi.c src/lcode.c src/ldebug.c src/ldo.c src/ldump.c src/lfunc.c \
	src/lgc.c src/llex.c src/lmem.c src/lobject.c src/lopcodes.c \
	src/lparser.c src/lstate.c src/lstring.c src/ltable.c src/ltm.c  \
	src/lundump.c src/lvm.c src/lzio.c src/lauxlib.c src/lbaselib.c \
	src/ldblib.c src/liolib.c src/lmathlib.c src/loslib.c src/ltablib.c \
	src/lstrlib.c src/loadlib.c src/linit.c src/thrlib.c src/buf.c

if ARCH_X86_64
libthrlua_la_SOURCES += src/amd64/setjmp.S
CPPFLAGS += -DLUA_ARCH_X86_64=1
endif
if ARCH_I386
libthrlua_la_SOURCES += src/i386/setjmp.S
CPPFLAGS += -DLUA_ARCH_I386=1
endif
if ARCH_SPARCV9
CPPFLAGS += -DLUA_ARCH_SPARCV9=1
endif

# lua executable
rclua_SOURCES = src/lua.c
rclua_LDADD = -lthrlua
rclua_LDFLAGS = @RL_LDFLAGS@
rclua_CPPFLAGS = @RL_CFLAGS@

# lua compiler
rcluac_SOURCES = src/luac.c src/print.c
rcluac_LDADD = -lthrlua

# pcre
pcre_la_SOURCES = modules/pcre.c
pcre_la_LDFLAGS = -module @PCRE_LIBS@
pcre_la_CPPFLAGS = $(commonCFLAGS) @PCRE_CFLAGS@

# mathx
mathx_la_SOURCES = modules/mathx.c
mathx_la_LDFLAGS = -module
mathx_la_CPPFLAGS = $(commonCFLAGS)

# xml 
xml_la_SOURCES = modules/xml.c
xml_la_LDFLAGS = -module @XML_LIBS@
xml_la_CPPFLAGS = $(commonCFLAGS) @XML_CFLAGS@

# curl 
curl_la_SOURCES = modules/curl.c
curl_la_LDFLAGS = -module @CURL_LIBS@
curl_la_CPPFLAGS = $(commonCFLAGS) @CURL_CFLAGS@

# javabridge
javabridge_la_SOURCES = modules/java.c
javabridge_la_LDFLAGS = -module @JAVA_LIBS@
javabridge_la_CPPFLAGS = $(commonCFLAGS) @JAVA_CFLAGS@

# lpeg
lpeg_la_SOURCES = modules/lpeg.c
lpeg_la_LDFLAGS = -module
lpeg_la_CPPFLAGS = $(commonCFLAGS)

# yaml
yaml_la_SOURCES = modules/yaml/lyaml.c modules/yaml/api.c \
	modules/yaml/dumper.c modules/yaml/emitter.c \
	modules/yaml/loader.c modules/yaml/parser.c \
	modules/yaml/reader.c modules/yaml/scanner.c \
	modules/yaml/writer.c modules/yaml/b64.c
yaml_la_LDFLAGS = -module
yaml_la_CPPFLAGS = $(commonCFLAGS) -Imodules/yaml

# posix
posix_la_SOURCES = modules/lposix.c
posix_la_LDFLAGS = -module
posix_la_CPPFLAGS = $(commonCFLAGS)

# socket
socket_la_SOURCES = modules/socket.c
socket_la_LDFLAGS = -module
socket_la_CPPFLAGS = $(commonCFLAGS)

if BUILD_JSON
json_la_SOURCES = modules/json.c
json_la_LDFLAGS = -module @JSON_LIBS@
json_la_CPPFLAGS = $(commonCFLAGS) @JSON_CFLAGS@
endif

EXTRA_DIST = COPYING HISTORY INSTALL README

smod_HEADERS = etc/strict.lua etc/tap.lua

dist_man_MANS = doc/rclua.1 doc/rcluac.1

.PHONY: test lcov

test:
	./runtests

lcov:
	-find . -name \*.gcda -o -name \*.da -o -name \*.bbg\? -o -name \*.lcovinfo | xargs rm -f
	-LUA_LCOV=./coverage/lcov ./runtests $(COV_TESTS)
	-rm -rf lcovhtml
	mkdir -p lcovhtml
	-find . -name \*.gcov | xargs rm -f
	PATH="./coverage:$$PATH" genhtml --output-directory lcovhtml --prefix `cd .. ; pwd` --show-details --title "Lua Code Coverage " $$(find . -name \*.lcovinfo)

cov:
	nocrap init
	-LUA_COV=nocrap ./runtests $(COV_TESTS)
	nocrap compute

