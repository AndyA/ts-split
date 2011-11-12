/* testutil.h */

#ifndef TESTUTIL_H
#define TESTUTIL_H

#include "tssplit.h"

#define check( rc ) \
  check_rc(__FILE__, __LINE__, # rc, rc)

#define LOC( s ) \
  "%s, %d: " s, __FILE__, __LINE__

#define countof(ar) (sizeof(ar) / sizeof(ar[0]))

int test_main( int argc, char *argv[] );

void check_rc( const char *file, int line, const char *src, int rc );
void tu_rand_fill( void *mem, size_t size, unsigned seed );

void *tu_malloc( size_t sz );
char *tu_strdup( const char *s );

char *tu_cleanup( char *filename );
void *tu_load( const char *name, size_t * sz, int is_ref );
char *tu_make_file( size_t sz, unsigned seed );
void tu_mkpath( const char *path, mode_t mode );
void tu_mkpath_for( const char *path, mode_t mode );
char *tu_tmp( void );
tssplit *tu_create( char **name );
uint64_t tu_bigrand( uint64_t max, unsigned *seed );
void tu_mk_range_list( tssplit_range * rl, size_t rlcount, size_t dsize,
                       unsigned seed );
void tu_shuffle( void *base, size_t nel, size_t width, unsigned seed );

#endif

/* vim:ts=2:sw=2:sts=2:et:ft=c 
 */
