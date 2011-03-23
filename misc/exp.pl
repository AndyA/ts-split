#!/usr/bin/env perl

use strict;
use warnings;

use constant LOW   => 220;
use constant HIGH  => 3000;
use constant STEPS => 8;

print
 join( ' ',
  map { "${_}k" } map { int( $_ + 0.5 ) } reverse range( LOW, HIGH, STEPS ) ),
 "\n";

sub range {
  my ( $low, $high, $steps ) = @_;
  my $ratio = ( $high / $low )**( 1 / ( $steps - 1 ) );
  my $br    = $low;
  my @r     = ();
  for ( 1 .. $steps ) {
    push @r, $br;
    $br *= $ratio;
  }
  return @r;
}

# vim:ts=2:sw=2:sts=2:et:ft=perl

