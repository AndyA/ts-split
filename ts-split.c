/* ts-split.c */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

static void
die( int rc, const char *msg, ... ) {
  va_list ap;
  va_start( ap, msg );
  fprintf( stderr, "Fatal: " );
  vfprintf( stderr, msg, ap );
  fprintf( stderr, "\n" );
  va_end( ap );
  exit( rc );
}

static AVFormatContext *
open_chunk_writer( const char *filename, const AVFormatContext * in_ctx ) {
  AVFormatContext *out_ctx;
  AVOutputFormat *ofmt;

  ofmt = guess_format( in_ctx->name, filename, "video/mpeg2" );
  if ( !ofmt ) {
    die( 5, "Can't guess output format" );
  }

  out_ctx = avformat_alloc_context(  );
  if ( !out_ctx ) {
    die( 6, "Can't allocate format context" );
  }
}

int
main( int argc, char *argv[] ) {
  AVFormatContext *in_ctx;
  AVFormatContext *out_ctx;
  AVPacket packet;
  int gop = 1;
  int vstream;
  int i, seq;

  const char *outname[256];

  if ( argc < 1 ) {
    die( 1, "Please name a file to split" );
  }

  av_register_all(  );

  if ( av_open_input_file( &in_ctx, argv[1], NULL, 0, NULL ) ) {
    die( 2, "Can't read \"%s\"", argv[1] );
  }

  if ( av_find_stream_info( in_ctx ) < 0 ) {
    die( 3, "Can't find stream" );
  }

  dump_format( in_ctx, 0, argv[1], false );

  vstream = -1;
  for ( i = 0; i < in_ctx->nb_streams; i++ ) {
    if ( in_ctx->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO ) {
      vstream = i;
      break;
    }
  }

  if ( vstream == -1 ) {
    die( 4, "Can't find a video stream" );
  }

  out_ctx = open_chunk_writer( "", in_ctx );
  while ( av_read_frame( in_ctx, &packet ) >= 0 ) {
    if ( packet.stream_index == vstream ) {
      if ( packet.flags & AV_PKT_FLAG_KEY ) {
        printf( "\nGOP %d ", gop++ );
      }
      else {
        printf( "." );
      }
    }
    av_free_packet( &packet );
  }
  printf( "\n" );

  av_close_input_file( in_ctx );
  av_free( out_ctx );

  return 0;
}

/* vim:ts=2:sw=2:sts=2:et:ft=c 
 */
