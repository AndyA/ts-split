/* tap.h */

#ifndef __TAP_H
#define __TAP_H

void diag( const char *fmt, ... );
void die( const char *fmt, ... );
void plan( int tests );
int ok( int flag, const char *msg, ... );
int pass( const char *msg, ... );
int fail( const char *msg, ... );
int is( long long got, long long want, const char *msg, ... );
int not_null( const void *p, const char *msg, ... );
int null( const void *p, const char *msg, ... );

#endif

/* vim:ts=2:sw=2:sts=2:et:ft=c 
 */
