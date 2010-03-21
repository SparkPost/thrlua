AC_INIT([rclua], [5.1.4], [], [rclua])
AM_INIT_AUTOMAKE([dist-bzip2])
AC_PROG_CC
AC_PROG_LIBTOOL
AC_C_INLINE
AM_PROG_CC_C_O

AC_DEFINE(LUA_USE_POSIX, 1, [Use POSIX functions])
AC_DEFINE(LUA_USE_DLOPEN, 1, [Use dlopen for loading modules])
AC_DEFINE_UNQUOTED(LUA_ROOT, "$prefix/", [Installation prefix])

AC_DEFINE(LUA_VERMAJORMINOR, "5.1", [First two version number octets])
AC_SUBST(PACKAGE_VERMAJORMINOR, 5.1)

# with coverage enabled, "make lcov" to generate a report in lcovhtml
AC_ARG_ENABLE(coverage,
  [ --enable-coverage   instrument for code coverage  ],
  [
    CFLAGS="$CFLAGS -fprofile-arcs -ftest-coverage -O0 -g"
    LIBS="$LIBS -lgcov"
  ]
)
AC_CHECK_HEADERS([ \
/opt/msys/3rdParty/include/valgrind/valgrind.h \
])

PCRE_LIBS=`pcre-config --libs`
PCRE_CFLAGS=`pcre-config --cflags`
AC_SUBST(PCRE_LIBS)
AC_SUBST(PCRE_CFLAGS)

AC_CHECK_LIB(dl,dlopen)
AC_CHECK_LIB(rt,clock_gettime)

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
			got_libedit=1
			AC_SUBST(RL_LDFLAGS, "-L$d -ledit -R$d")
			libeditinc=`dirname $d`/include
			AC_SUBST(RL_CFLAGS, "-DLUA_USE_READLINE -I$libeditinc")
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

if test -x /usr/bin/optisa ; then
	JARCH=`/usr/bin/optisa amd64 sparcv9 i386 sparcv8`
else
	JARCH=`uname -i 2>/dev/null`
	if test -z "$JARCH" ; then
		JARCH=`uname -p`
	fi
fi
if test "$JARCH" == "x86_64" ; then
	JARCH="amd64"
fi

case $host in
	*-*-linux*)
		AC_DEFINE_UNQUOTED(DEFAULT_JVM, "/usr/java/default/lib/$JARCH/server/libjvm.so", [jvm])
		JAVA_LIBS=""
		dnl find latest jdk
		for jdk in /usr/java/jdk* ; do
			JAVA_CFLAGS="-I$jdk/include -I$jdk/include/linux"
		done
		;;
	*-*-solaris*)
		AC_DEFINE_UNQUOTED(DEFAULT_JVM, "/usr/java/jre/lib/$JARCH/server/libjvm.so", [jvm])
		JAVA_CFLAGS="-I/usr/java/include -I/usr/java/include/solaris"
		;;
	*-*-darwin*)
		AC_DEFINE_UNQUOTED(DEFAULT_JVM, "libjvm is system framework on OS/X Darwin")
		JAVA_CFLAGS="-I/System/Library/Frameworks/JavaVM.framework/Versions/A/Headers"
		JAVA_LIBS="-framework JavaVM"
		;;
esac
AC_SUBST(JAVA_CFLAGS)
AC_SUBST(JAVA_LIBS)

AC_CONFIG_HEADERS([rcluaconfig.h])
AC_CONFIG_FILES([Makefile etc/rclua.pc])
AC_OUTPUT

dnl vim:ts=2:sw=2:
