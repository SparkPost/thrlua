AC_INIT([rclua], [5.1.8], [], [rclua])
AM_INIT_AUTOMAKE([dist-bzip2])
AC_CANONICAL_HOST
AC_PROG_CC

# Same basic environmental bits that we need to get straight before we probe
# for libraries
case $host in
	i386*solaris*)
		arch=x86_64
		;;
	sparc*solaris*)
		arch=sparcv9
		;;
	x86_64*linux*)
		arch=x86_64
		;;
	i*86*linux*)
		arch=i386
		;;
	*darwin*)
		arch=x86_64
		;;
esac
AM_CONDITIONAL(ARCH_X86_64, test "$arch" = x86_64)
AM_CONDITIONAL(ARCH_I386, test "$arch" = i386)
AM_CONDITIONAL(ARCH_SPARCV9, test "$arch" = sparcv9)

case $host in
	*solaris*)
		CFLAGS="$CFLAGS -D_XOPEN_SOURCE=600 -D_BSD_SOURCE -std=gnu99"
		LDFLAGS="-R/usr/sfw/lib/64/"
		;;
esac
export CFLAGS LDFLAGS CC

AC_PROG_LIBTOOL
AC_C_INLINE
AM_PROG_CC_C_O
AM_PROG_AS

AC_DEFINE(LUA_USE_POSIX, 1, [Use POSIX functions])
AC_DEFINE(LUA_USE_DLOPEN, 1, [Use dlopen for loading modules])

if test "$prefix" == "NONE" ; then
	AC_DEFINE_UNQUOTED(LUA_ROOT, "/opt/msys/3rdParty/", [Installation prefix])
else
	AC_DEFINE_UNQUOTED(LUA_ROOT, "$prefix/", [Installation prefix])
fi

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
/usr/local/include/valgrind/valgrind.h \
])

PCRE_LIBS=`pcre-config --libs`
PCRE_CFLAGS=`pcre-config --cflags`
AC_SUBST(PCRE_LIBS)
AC_SUBST(PCRE_CFLAGS)

CK_CFLAGS=`pkg-config --cflags ck`
AC_SUBST(CK_CFLAGS)

XML_LIBS=`xml2-config --libs`
XML_CFLAGS=`xml2-config --cflags`
AC_SUBST(XML_LIBS)
AC_SUBST(XML_CFLAGS)

CURL_LIBS=`curl-config --libs`
CURL_CFLAGS=`curl-config --cflags`
AC_SUBST(CURL_LIBS)
AC_SUBST(CURL_CFLAGS)

if pkg-config json ; then
	BUILD_JSON="yes"
	JSON_LIBS=`pkg-config --libs json`
	JSON_CFLAGS=`pkg-config --cflags json`
	AC_SUBST(JSON_LIBS)
	AC_SUBST(JSON_CFLAGS)
else
	BUILD_JSON="no"
fi
AM_CONDITIONAL(BUILD_JSON, test "$BUILD_JSON" = "yes")

AC_CHECK_LIB(dl,dlopen)
AC_CHECK_LIB(rt,clock_gettime)
AC_CHECK_LIB(crypt,crypt)
AC_CHECK_LIB(crypt_d, crypt)

# __thread
AC_MSG_CHECKING([for native TLS in gcc])
AC_TRY_COMPILE([static __thread int a;],[int b = a;],
[AC_DEFINE(HAVE_TLS)
 AC_MSG_RESULT([yup, using native TLS])],
[AC_MSG_RESULT([nope, using pthread TLS])])

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
		AC_DEFINE(LUA_OS_LINUX, 1, [Running on Linux])
		;;
	*-*-solaris*)
		AC_DEFINE_UNQUOTED(DEFAULT_JVM, "/usr/java/jre/lib/$JARCH/server/libjvm.so", [jvm])
		JAVA_CFLAGS="-I/usr/java/include -I/usr/java/include/solaris"
		AC_DEFINE(LUA_OS_SOLARIS, 1, [Running on Solaris])
		;;
	*-*-darwin*)
		AC_DEFINE_UNQUOTED(DEFAULT_JVM, "libjvm is system framework on OS/X Darwin")
		JAVA_CFLAGS="-I/System/Library/Frameworks/JavaVM.framework/Versions/A/Headers"
		JAVA_LIBS="-framework JavaVM"
		AC_DEFINE(LUA_OS_DARWIN, 1, [Running on Darwin OS/X])
		;;
esac
AC_SUBST(JAVA_CFLAGS)
AC_SUBST(JAVA_LIBS)

AC_CONFIG_HEADERS([rcluaconfig.h])
AC_CONFIG_FILES([Makefile etc/rclua.pc])
AC_OUTPUT

dnl vim:ts=2:sw=2:
