#!/bin/bash

IN="tg.ts"
OUT="out"
BITRATES="400k 800k 1600k"
AUDIOBR="1600k"
VIDEO_OPTIONS="-g 100 -keyint_min 50 -flags2 -mbtree -threads 0"

rm -rf "$OUT"
pipe="tee"
for br in $BITRATES; do
  outdir="$OUT/$br"
  mkdir -p $outdir
  if [ "$br" = "$AUDIOBR" ]; then
    audio="-acodec libfaac -ac 2 -ab 96k"
  else
    audio="-an"
  fi
  video="-vcodec libx264 -vpre veryfast ${VIDEO_OPTIONS}"
  ffcmd="ffmpeg -f mpegts -i - $audio $video -b $br -f mpegts -"
  tscmd="./ts-split - $outdir/%08x.ts"
  pipe="$pipe >( $ffcmd | $tscmd )"
done

#echo $pipe
cat "$IN" | eval $pipe > /dev/null

# vim:ts=2:sw=2:sts=2:et:ft=sh

