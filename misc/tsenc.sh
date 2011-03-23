#!/bin/bash

INPUTFILE="$1"
OUTPUTFILE="$2"
AUDIO_NONE="-an"
AUDIO_OPTIONS="-acodec libfaac -ac 2 -ab 96k"
VIDEO_OPTIONS="-g 100 -keyint_min 50 -flags2 -mbtree -threads 0"
VIDEO_PASS1="-vcodec libx264 -vpre veryfast_firstpass ${VIDEO_OPTIONS}"
VIDEO_PASS2="-vcodec libx264 -vpre veryfast ${VIDEO_OPTIONS}"
BITRATE="800k"
SIZE="768x320"

set -x
ffmpeg -y -i "$INPUTFILE" $AUDIO_NONE $VIDEO_PASS1 -pass 1 -b $BITRATE -s $SIZE -f mpegts /dev/null
ffmpeg -y -i "$INPUTFILE" $AUDIO_OPTIONS $VIDEO_PASS2 -pass 2 -b $BITRATE -s $SIZE -f mpegts "$OUTPUTFILE"
