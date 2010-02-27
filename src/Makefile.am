lib_LTLIBRARIES = librclua.la
bin_PROGRAMS = rclua rcluac

install-data-local:
	$(mkdir_p) "$(DESTDIR)$(includedir)/rclua"
	for p in lua.h luaconf.h lauxlib.h lualib.h; do \
		$(INSTALL_HEADER) "$(srcdir)/$$p" "$(DESTDIR)$(includedir)/rclua/$$p"; \
	done
	$(INSTALL_HEADER) ../rcluaconfig.h "$(DESTDIR)$(includedir)/rclua/rcluaconfig.h"

uninstall-local:
	rm -rf $(DESTDIR)$(includedir)/rclua	

noinst_HEADERS = lapi.h lcode.h ldebug.h ldo.h lfunc.h lgc.h \
	llex.h llimits.h lmem.h lobject.h lopcodes.h lparser.h \
	lstate.h lstring.h ltable.h ltm.h lundump.h lvm.h lzio.h \
	lua.h luaconf.h lauxlib.h lualib.h

librclua_la_LDFLAGS = -lpthread -ldl -lm 
librclua_la_SOURCES = \
	lapi.c lcode.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c lmem.c \
	lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c  \
	lundump.c lvm.c lzio.c \
	lauxlib.c lbaselib.c ldblib.c liolib.c lmathlib.c loslib.c ltablib.c \
	lstrlib.c loadlib.c linit.c

rclua_SOURCES = \
	lua.c
rclua_LDADD = -lrclua
rclua_LDFLAGS = @RL_LDFLAGS@
rclua_CPPFLAGS = @RL_CFLAGS@

rcluac_SOURCES = \
	luac.c \
	print.c
rcluac_LDADD = -lrclua 
