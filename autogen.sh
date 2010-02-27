#!/bin/sh
# vim:ts=2:sw=2:et:
if test `uname` == "Darwin" ; then
  glibtoolize --automake
else
  libtoolize --automake
fi
autoheader
aclocal 
automake --add-missing --foreign
autoconf
