AC_INIT([rclua], [5.1.4], [], [rclua])
AM_INIT_AUTOMAKE([dist-bzip2])
AC_PROG_CC
AC_PROG_LIBTOOL
AC_C_INLINE

AC_DEFINE(LUA_USE_POSIX, 1, [Use POSIX functions])
AC_DEFINE(LUA_USE_DLOPEN, 1, [Use dlopen for loading modules])
AC_DEFINE_UNQUOTED(LUA_ROOT, "$prefix/", [Installation prefix])

AC_DEFINE(LUA_VERMAJORMINOR, "5.1", [First two version number octets])
AC_SUBST(PACKAGE_VERMAJORMINOR, 5.1)

EXTRA_RPM_DEPS=""

AC_ARG_WITH(readline,
[  --with-readline    link rclua against libreadline
                      otherwise, use libedit if found],
[
	use_readline=1
	LDFLAGS_SAVE="$LDFLAGS"
	LDFLAGS="-L$withval"
	AC_CHECK_LIB(readline, readline,[
		AC_SUBST(RL_LDFLAGS, "-lreadline")
		AC_SUBST(RL_CFLAGS, "-DLUA_USE_READLINE")
		EXTRA_RPM_DEPS="readline"
		]
	)
	LDFLAGS="$LDFLAGS_SAVE"
])

if test "$use_readline" != "1" ; then
	# They didn't ask for readline, let's check for libedit
	for d in /opt/msys/3rdParty/lib* /usr/local/lib* /usr/lib* ; do
		LDFLAGS_SAVE="$LDFLAGS"
		LDFLAGS="-L$d -ledit"
		got_libedit=0
		AC_CHECKING(for libedit in $d)
		AC_TRY_LINK_FUNC(readline,[
			AC_MSG_RESULT(yes)
			AC_PATH_PROG(RPM, rpm)
			got_libedit=1
			AC_SUBST(RL_LDFLAGS, "-L$d -ledit -R$d")
			libeditinc=`dirname $d`/include
			AC_SUBST(RL_CFLAGS, "-DLUA_USE_READLINE -I$libeditinc")
			if test -x "$RPM" ; then
				EXTRA_RPM_DEPS="$EXTRA_RPMS_DEPS `rpm -qf --queryformat '%{NAME} ' $d/libedit*.so.*`"
			fi
			],[
			AC_MSG_RESULT(no)
			]
		)
		LDFLAGS="$LDFLAGS_SAVE"
		if test "$got_libedit" == "1" ; then
			break;
		fi
	done
fi

AC_SUBST(EXTRA_RPM_DEPS)
AC_CONFIG_HEADERS([rcluaconfig.h])
AC_CONFIG_FILES([Makefile src/Makefile doc/Makefile etc/rclua.pc etc/Makefile])
AC_OUTPUT

