/* tap.c */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "tap.h"

static int test_no = -1;
static int planned = 0;

void
diag( const char *fmt, ... ) {
  va_list ap;
  va_start( ap, fmt );
  fprintf( stderr, "# " );
  vfprintf( stderr, fmt, ap );
  fprintf( stderr, "\n" );
  va_end( ap );
}

void
die( const char *fmt, ... ) {
  va_list ap;
  va_start( ap, fmt );
  vfprintf( stderr, fmt, ap );
  fprintf( stderr, "\n" );
  va_end( ap );
  exit( 1 );
}

static void
cleanup( void ) {
  if ( test_no != planned ) {
    die( "Planned %d tests but ran %d", planned, test_no );
  }
}

void
plan( int tests ) {
  if ( test_no != -1 ) {
    die( "Already planned (on test %d)", test_no );
  }
  printf( "1..%d\n", tests );
  test_no = 0;
  planned = tests;
  atexit( cleanup );
}

static void
test( int flag, const char *msg, va_list ap ) {
  if ( test_no == -1 ) {
    die( "Test without plan" );
  }
  printf( "%sok %d - ", flag ? "" : "not ", ++test_no );
  vprintf( msg, ap );
  printf( "\n" );
}

#define TF(flag) \
  va_list ap;          \
  int _c = (flag);     \
  va_start( ap, msg ); \
  test( _c, msg, ap ); \
  va_end( ap );        \
  return _c;

int
ok( int flag, const char *msg, ... ) {
  TF( flag );
}

int
pass( const char *msg, ... ) {
  TF( 1 );
}

int
fail( const char *msg, ... ) {
  TF( 0 );
}

int
is( long long got, long long want, const char *msg, ... ) {
  TF( got == want );
}

int
not_null( const void *p, const char *msg, ... ) {
  TF( !!p );
}

int
null( const void *p, const char *msg, ... ) {
  TF( !p );
}

/* vim:ts=2:sw=2:sts=2:et:ft=c 
 */
