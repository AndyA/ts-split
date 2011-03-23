#!/bin/bash

IN="tg.ts"
OUT="out"
BITRATES="3000k 2065k 1422k 979k 674k 464k 320k 220k"
VIDEO="-g 100 -keyint_min 50 -flags2 -mbtree -threads 0"
AUDIO="-acodec libfaac -ac 2 -ab 96k"
NOAUDIO="-an"

rm -rf "$OUT"
pipe="tee"
audio="$AUDIO"
for br in $BITRATES; do
  outdir="$OUT/$br"
  mkdir -p $outdir
  video="-vcodec libx264 -vpre veryfast ${VIDEO}"
  ffcmd="ffmpeg -f mpegts -i - $audio $video -b $br -f mpegts -"
  tscmd="./ts-split - $outdir/%08x.ts"
  pipe="$pipe >( $ffcmd | $tscmd )"
  audio="$NOAUDIO"
done

#echo $pipe
cat "$IN" | eval "$pipe" > /dev/null

# vim:ts=2:sw=2:sts=2:et:ft=sh

