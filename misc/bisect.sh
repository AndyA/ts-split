#!/bin/sh

{
  make clean && make && ./ts-split tg.ts foo.ts
} >/dev/null 2>&1

[ $(stat -f '%z' foo.ts) -eq $(stat -f '%z' tg.ts) ]

# vim:ts=2:sw=2:sts=2:et:ft=sh

