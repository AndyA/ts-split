#!/bin/bash

#set -x
STASH="$(pwd)/stash-$(date +%Y%m%d-%H%M%S)/ts-split"
EXT="c"

function gcover() {
  local srcdir=$1
  local gcda ext
  find $srcdir -name \*.gcda | while read gcda; do
    local dir=$(dirname $gcda)
    local name=$(basename $gcda .gcda)
    pushd $dir >/dev/null
    local src=
    for ext in $EXT; do
      [ -f "$name.$ext" ] && src="$name.$ext"
    done
    if [ -z $src ]; then
      echo "Can't find source for $name"
      exit 1
    fi
    local gcov="$src.gcov"
    gcov $src >/dev/null
    if [ -f $gcov ]; then
      sg="$STASH/$dir"
      mkdir -p $sg
      perl -pi -e 's/^\s+\d+:/        1:/' $gcov
      mv $gcov $sg
    fi
    popd >/dev/null
    rm -f $gcda
  done
}

echo $STASH
gcover './'
gcover '../ffmpeg/ffmpeg/'

# vim:ts=2:sw=2:sts=2:et:ft=sh

