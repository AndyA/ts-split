#!/bin/bash

IN="tg"
EXT="ts"

for br in 200k 400k 800k; do
  out="$IN-$br"
  rm -rf $out; mkdir -p $out
  ffmpeg -y -i "$IN.$EXT" -acodec libfaac -ab 96k -vcodec libx264 -b $br -vpre fast -threads 0 "$out.$EXT"
  ./tssplit "$out.$EXT" "$out/%08x.$EXT"
done

# vim:ts=2:sw=2:sts=2:et:ft=sh

