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

int
main( int argc, char *argv[] ) {
  AVFormatContext *fmt_ctx;
  AVPacket packet;
  int gop = 1;
  int vstream;
  int i;

  if ( argc < 1 ) {
    die( 1, "Please name a file to split" );
  }

  av_register_all(  );

  if ( av_open_input_file( &fmt_ctx, argv[1], NULL, 0, NULL ) ) {
    die( 2, "Can't read \"%s\"", argv[1] );
  }

  if ( av_find_stream_info( fmt_ctx ) < 0 ) {
    die( 3, "Can't find stream" );
  }

  dump_format( fmt_ctx, 0, argv[1], false );

  vstream = -1;
  for ( i = 0; i < fmt_ctx->nb_streams; i++ ) {
    if ( fmt_ctx->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO ) {
      vstream = i;
      break;
    }
  }
  if ( vstream == -1 ) {
    die( 4, "Can't find a video stream" );
  }

  while ( av_read_frame( fmt_ctx, &packet ) >= 0 ) {
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

  av_close_input_file( fmt_ctx );

  return 0;
}

/* vim:ts=2:sw=2:sts=2:et:ft=c 
 */
