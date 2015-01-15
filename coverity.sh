#!/bin/bash
CFLAGS="$CFLAGS -I/opt/msys/3rdParty/include"

if test -f Makefile ; then
  # remove cruft if we're building by copying a development dir locally
  cruft=1
fi
if test -d autom4te.cache ; then
  rm -rf autom4te.cache
  cruft=1
fi

libtoolize --automake
aclocal
autoheader
automake --add-missing --foreign
autoconf

PKG_CONFIG_PATH="/opt/msys/3rdParty/lib64/pkgconfig/"
export PKG_CONFIG_PATH

./configure

if test "$cruft" = "1" ; then
  make clean
fi

# does not support parallel make
make
