SCRIPT_DIR=$(prefix)/share/lua/@PACKAGE_VERMAJORMINOR@

EXTRA_DIST = lua.hpp strict.lua tap.lua

install-data-local:
	$(mkdir_p) "$(DESTDIR)$(includedir)/rclua"
	$(INSTALL_HEADER) $(srcdir)/lua.hpp "$(DESTDIR)$(includedir)/rclua/lua.hpp"
	$(mkdir_p) "$(DESTDIR)$(SCRIPT_DIR)"
	$(INSTALL) $(srcdir)/*.lua "$(DESTDIR)$(SCRIPT_DIR)/"

