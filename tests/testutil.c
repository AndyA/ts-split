/* testutil.c */

#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "testutil.h"
#include "tap.h"

#define ELTPT(base, i, w) \
  (void *) ((char *) (base) + (i) * (w))

typedef struct tu__cleanup_file tu__cleanup_file;

struct tu__cleanup_file {
  tu__cleanup_file *next;
  char *name;
};

static char *exe_name = NULL;

static tu__cleanup_file *cl_file = NULL;
static unsigned next_tmp = 1;

static void
cleanup( void ) {
  tu__cleanup_file *next;
  while ( cl_file ) {
    unlink( cl_file->name );
    next = cl_file->next;
    free( cl_file->name );
    free( cl_file );
    cl_file = next;
  }
}

static int atexit_done = 0;
static void
atexit_hook( void ) {
  if ( !atexit_done++ )
    atexit( cleanup );
}

void *
tu_malloc( size_t sz ) {
  void *m;
  if ( !sz )
    sz++;
  if ( m = malloc( sz ), !m )
    die( "Out of memory" );
  memset( m, 0, sz );
  return m;
}

char *
tu_strdup( const char *s ) {
  if ( s ) {
    size_t l = strlen( s ) + 1;
    char *sd = tu_malloc( l );
    memcpy( sd, s, l );
    return sd;
  }
  return NULL;
}

char *
tu_cleanup( char *filename ) {
  tu__cleanup_file *cf = tu_malloc( sizeof( tu__cleanup_file ) );
  cf->name = tu_strdup( filename );
  cf->next = cl_file;
  cl_file = cf;
  atexit_hook(  );
  return filename;
}

void
check_rc( const char *file, int line, const char *src, int rc ) {
  if ( rc )
    die( "%s, %d (\"%s\"): error: %s", file, line, src,
         strerror( errno ) );
}

void
tu_rand_fill( void *mem, size_t size, unsigned seed ) {
  unsigned char *mp = ( unsigned char * ) mem;
  off_t i;
  for ( i = 0; i < size; i++ ) {
    mp[i] = rand_r( &seed );
  }
}

void *
tu_load( const char *name, size_t * sz, int is_ref ) {
  struct stat st;
  ssize_t got;
  void *buf;

  if ( is_ref ) {
    tssplit *rf;

    check( tssplit_stat( name, &st ) );
    buf = tu_malloc( st.st_size + 1 );
    if ( rf = tssplit_open( name, O_RDONLY ), !rf )
      die( "Can't read %s: %s", name, strerror( errno ) );
    check( ( got = tssplit_read( rf, buf, st.st_size ), got < 0 ) );
    if ( got != st.st_size )
      die( "Short read of %s", name );
    check( tssplit_close( rf ) );
  }
  else {
    int fd;

    check( stat( name, &st ) );
    buf = tu_malloc( st.st_size + 1 );
    if ( fd = open( name, O_RDONLY ), fd < 0 )
      die( "Can't read %s: %s", name, strerror( errno ) );
    check( ( got = read( fd, buf, st.st_size ), got < 0 ) );
    if ( got != st.st_size )
      die( "Short read of %s", name );
    check( close( fd ) );
  }
  *sz = st.st_size;
  return buf;
}

char *
tu_make_file( size_t sz, unsigned seed ) {
  unsigned char buf[16384];
  char *tmp = tu_tmp(  );
  int fd;

  if ( fd = open( tmp, O_CREAT | O_WRONLY, 0666 ), fd < 0 )
    die( LOC( "Can't write %s: %s" ), tmp, strerror( errno ) );

  while ( sz > 0 ) {
    size_t want = sz;
    size_t done;
    if ( want > sizeof( buf ) )
      want = sizeof( buf );
    tu_rand_fill( buf, want, seed );
    seed = rand_r( &seed );
    done = write( fd, buf, want );
    if ( done < 0 )
      die( LOC( "Write error on %s: %s" ), tmp, strerror( errno ) );
    if ( done != want )
      die( LOC( "Bad write on %s" ), tmp );
    sz -= done;
  }
  close( fd );
  return tmp;
}

static char *
tu__dirname( const char *name ) {
  char *tmp = tu_strdup( name );
  char *dir = tu_strdup( dirname( tmp ) );
  free( tmp );
  return dir;
}

void
tu_mkpath( const char *path, mode_t mode ) {
  struct stat st;
  char *dir;
  int rc;

  if ( rc = stat( path, &st ), rc && errno != ENOENT )
    goto fail;
  if ( rc == 0 && st.st_mode & S_IFDIR )
    return;
  dir = tu__dirname( path );
  if ( dir ) {
    tu_mkpath( dir, mode );
    free( dir );
  }
  if ( mkdir( path, mode ) )
    goto fail;
  return;

fail:
  die( "tu_mkpath %s failed: %s", path, strerror( errno ) );
}

void
tu_mkpath_for( const char *path, mode_t mode ) {
  char *dir = tu__dirname( path );
  tu_mkpath( dir, mode );
  free( dir );
}

char *
tu_tmp( void ) {
  const char *forensic = getenv( "RFILE_FORENSIC" );
  if ( forensic && *forensic ) {
    tu_mkpath( forensic, 0777 );
    size_t len = strlen( forensic );
    char *tmp = tu_malloc( len + MAXPATHLEN );
    memcpy( tmp, forensic, len );
    tmp[len++] = '/';
    len +=
        sprintf( tmp + len, "rf.%s.%lu.%05d.tmp",
                 exe_name, ( unsigned long ) getpid(  ), next_tmp++ );
    return tmp;
  }
  else {
    return tu_cleanup( tu_strdup( tmpnam( NULL ) ) );
  }
}

tssplit *
tu_create( char **name ) {
  tssplit *rf;

  *name = tu_tmp(  );
  if ( NULL == ( rf = tssplit_create( *name, 0600 ) ) )
    die( "Can't create %s: %s", *name, strerror( errno ) );

  return rf;
}

uint64_t
tu_bigrand( uint64_t max, unsigned *seed ) {
  uint64_t rlim = 1;
  uint64_t rn = 0;
  while ( rlim < max ) {
    rn = rn * ( ( uint64_t ) RAND_MAX + 1 ) + rand_r( seed );
    rlim *= ( ( uint64_t ) RAND_MAX + 1 );
  }
  return rn * max / rlim;
}

static int
cmp_range( const void *a, const void *b ) {
  const tssplit_range *ra = a;
  const tssplit_range *rb = b;
  return ra->start < rb->start ? -1
      : ra->start > rb->start ? 1
      : ra->end < rb->end ? -1 : ra->end > rb->end ? 1 : 0;
}

/* Create an ordered list of start, end pairs */
void
tu_mk_range_list( tssplit_range * rl, size_t rlcount, size_t dsize,
                  unsigned seed ) {
  unsigned rlpos = 0;

  /* build list of ranges */
  rl[rlpos].start = rl[rlpos].end = 0;
  rlpos++;
  while ( rlpos < rlcount ) {
    rl[rlpos].start = rl[rlpos].end = tu_bigrand( dsize, &seed );
    rlpos++;
  }
  /* sort */
  qsort( rl, rlcount, sizeof( rl[0] ), cmp_range );
  for ( rlpos = 0; rlpos < rlcount - 1; rlpos++ ) {
    rl[rlpos].end = rl[rlpos + 1].start;
  }
  rl[rlpos].end = dsize;
}

void
tu_shuffle( void *base, size_t nel, size_t width, unsigned seed ) {
  unsigned pos;
  void *tmp = tu_malloc( width );
  for ( pos = 0; pos < nel - 1; pos++ ) {
    unsigned pick = ( unsigned ) tu_bigrand( nel - pos - 1, &seed );
    memcpy( tmp, ELTPT( base, width, pick + pos + 1 ), width );
    memcpy( ELTPT( base, width, pick + pos + 1 ),
            ELTPT( base, width, pos ), width );
    memcpy( ELTPT( base, width, pos ), tmp, width );
  }
  free( tmp );
}

int
main( int argc, char *argv[] ) {
  int rc;
  exe_name = tu_strdup( basename( argv[0] ) );
  rc = test_main( argc, argv );
  free( exe_name );
  return rc;
}

/* vim:ts=2:sw=2:sts=2:et:ft=c 
 */
