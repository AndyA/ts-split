#!/usr/bin/env perl

use strict;
use warnings;

while ( <> ) {
  chomp;
  my @f = split /,/;
  print join( ',', @f, $f[2] - $f[1] ), "\n";
}

# vim:ts=2:sw=2:sts=2:et:ft=perl

