#!/usr/bin/env perl

use strict;
use warnings;

use Data::Dumper;
use File::Path qw( make_path );
use File::Spec;
use IO::Handle;
use IO::Select;
use IPC::Run qw( run );
use List::Util qw( min max sum );

use constant IN          => 'pingu';
use constant OUT         => 'out';
use constant MIN_BATCH   => 1179;
use constant MIN_OVERLAP => 327;
use constant DROP_FUDGE  => 3;
use constant WORKERS     => 4;
use constant BUFSIZE     => 8192;
use constant FPS         => 25;

my @FFCOPY = (
  'ffmpeg',
  -i => '-',
  '-copyts',
  -acodec => 'copy',
  -vcodec => 'copy',
);

my $asset = load_asset( IN );
encode_asset( $asset, OUT );

sub encode_asset {
  my ( $asset, $out ) = @_;

  my @spans = partition( $asset );
  for my $span ( @spans ) {
    $span->{name}
     = encode_span( $asset, $out, $span->{start}, $span->{overlap},
      $span->{frame} );
  }

  my @pl = splice_spans( @spans );
  print Dumper( \@pl );
}

sub splice_spans {
  my @spans = @_;

  my $skip  = 0;
  my %error = ();
  my @pl    = ();
  for my $span ( @spans ) {
    my $asset = load_asset( $span->{name} );
    print Dumper( $asset );
    my ( $pos, $dur ) = ( 0, 0 );
    my $chunks    = $asset->{chunks};
    my $best_diff = undef;
    my $best_dur  = undef;
    my $best_pos  = undef;
    CHUNK: while ( $pos < @$chunks && $dur < $skip + DROP_FUDGE ) {
      my $diff = abs( $dur - $skip );
      ( $best_diff, $best_dur, $best_pos ) = ( $diff, $dur, $pos )
       if !defined $best_diff || $diff < $best_diff;
      $dur += $chunks->[ $pos++ ]{duration};
    }
    if ( $best_diff ) {
      warn "mismatch: skip should be $skip, is $best_dur\n";
      $error{mismatch}++;
    }
    push @pl, @{$chunks}[ $best_pos .. @$chunks - 1 ];
    $skip = $span->{od};
  }
  return @pl;
}

sub get_span {
  my ( $asset, $pos, $need ) = @_;
  my $chunks = $asset->{chunks};
  return ( $pos, $pos ) if $pos >= @$chunks;
  my $dur   = 0;
  my $start = $pos;
  while ( $pos < @$chunks && $dur < $need ) {
    $dur += $chunks->[ $pos++ ]{duration};
  }
  return ( $start, $pos, $dur );
}

sub partition {
  my $asset = shift;
  my $pos   = 0;

  my @spans = ();

  while ( $pos < @{ $asset->{chunks} } ) {
    my ( $ss, $se, $sd ) = get_span( $asset, $pos, MIN_BATCH );
    my ( $os, $oe, $od ) = get_span( $asset, $se,  MIN_OVERLAP );
    push @spans,
     {
      start   => $ss,
      end     => $se,
      overlap => $oe,
      sd      => $sd,
      od      => $od,
      frame   => $asset->{chunks}[$ss]{start},
     };
    $pos = $se;
  }
  return @spans;
}

sub manifest { $_[0] . '.manifest' }

sub make_cat {
  my @files = @_;
  return sub {
    for my $file ( @files ) {
      open my $fh, '<', $file or die "Can't read $file: $!\n";
      catfile( $fh, \*STDOUT );
    }
  };
}

sub make_feeder {
  my ( $asset, $from, $to ) = @_;
  return make_cat( map { $_->{file} }
     @{ $asset->{chunks} }[ $from .. $to - 1 ] );
}

sub ffmpeg_cmd {
  return [
    'ffmpeg',
    -i => '-',
    @_,
    -acodec  => 'libfaac',
    -ac      => 2,
    -ab      => '96k',
    -ar      => 44100,
    -vcodec  => 'libx264',
    -b       => '800k',
    -vpre    => 'veryfast',
    -vpre    => 'main',
    -threads => 0,
    -f       => 'mpegts',
    '-deinterlace',
    '-'
  ];
}

sub encode_span {
  my ( $asset, $out, $from, $to, $frame ) = @_;
  my $sname
   = File::Spec->catfile( $out, join '-', $asset->{name}, $from, $to );
  my $sman = manifest( $sname );

  unless ( -f $sman ) {
    make_path( $sname );

    my $feeder = make_feeder( $asset, $from, $to );

    my @split = (
      './ts-split', '-M', $sman, '-',
      File::Spec->catfile( $sname, '%08x.ts' )
    );

    run $feeder, '|',
     ffmpeg_cmd( -timestamp => frame_to_time( $frame ) ), '|', \@split;
  }

  return $sname;
}

sub catfile {
  my ( $in, $out ) = @_;
  while () {
    my $got = sysread $in, my $buf, BUFSIZE;
    die "I/O error: $!\n" unless defined $got;
    last unless $got;
    syswrite $out, $buf;
  }
}

sub analyse_asset {
  my $asset = shift;

  my @dur = ();
  for my $chunk ( @{ $asset->{chunks} } ) {
    my $dur = $chunk->{end} - $chunk->{start};
    push @dur, $dur;
    $chunk->{duration} = $dur;
  }
  my $total   = sum( @dur );
  my $average = $total / @dur;

  $asset->{metrics} = {
    total   => $total,
    average => $average,
    min     => min( @dur ),
    max     => max( @dur ),
  };
}

sub read_manifest {
  my $name     = shift;
  my $manifest = manifest( $name );
  my $asset    = { name => $name };
  open my $mh, '<', $manifest or die "Can't read $manifest: $!\n";
  while ( <$mh> ) {
    chomp;
    my @f = split /,/;
    die "Bad manifest line: $_\n" unless @f >= 5;
    my ( $file, $start, $end, $dts, $pts ) = @f;
    die "Can't find $file\n" unless -f $file;
    push @{ $asset->{chunks} },
     {
      file  => $file,
      start => $start,
      end   => $end,
      dts   => $dts,
      pts   => $pts
     };
  }
  return $asset;
}

sub load_asset {
  my $name  = shift;
  my $asset = read_manifest( $name );
  analyse_asset( $asset );
  return $asset;
}

sub frame_to_time {
  my $fr = shift;
  my ( $seconds, $frames ) = ( int( $fr / FPS ), $fr % FPS );
  return sprintf '%02d:%02d:%02d.%06d',
   int( $seconds / 3600 ) % 24, int( $seconds / 60 ) % 60,
   $seconds % 60, $frames * 1000000 / FPS;
}

# vim:ts=2:sw=2:sts=2:et:ft=perl

