#!/bin/sh

[ -f Makefile ] && make clean
CFLAGS="-g -O0" ./configure --prefix=$PWD/foo/usr/local --disable-shared --enable-static

# vim:ts=2:sw=2:sts=2:et:ft=sh

