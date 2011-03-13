/* ts-split.c */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

static void
die( const char *msg, ... ) {
  va_list ap;
  va_start( ap, msg );
  fprintf( stderr, "Fatal: " );
  vfprintf( stderr, msg, ap );
  fprintf( stderr, "\n" );
  va_end( ap );
  exit( 1 );
}

static void
close_chunk_writer( AVFormatContext * out_ctx ) {
  if ( out_ctx ) {
    av_write_trailer( out_ctx );
    url_fclose( out_ctx->pb );
    av_free( out_ctx );
  }
}

static AVStream *
add_video_stream( AVFormatContext * out_ctx,
                  const AVFormatContext * in_ctx, int vstream ) {
  AVStream *ist, *ost;

  ist = in_ctx->streams[vstream];
  ost = av_new_stream( out_ctx, 0 );
  if ( !ost ) {
    die( "Could not alloc stream\n" );
  }

  ost->quality = ist->quality;
  ost->codec = NULL;
  ost->sample_aspect_ratio = ist->sample_aspect_ratio;
  ost->r_frame_rate = ist->r_frame_rate;
  ost->time_base = ist->time_base;
  ost->pts.val = 0;
  ost->pts.num = 0;
  ost->pts.den = 1;
  ost->stream_copy = 1;

  return ost;

}

static AVFormatContext *
open_chunk_writer( const char *filename, const char *fmtname,
                   const AVFormatContext * in_ctx, int vstream ) {
  AVFormatContext *out_ctx;
  AVOutputFormat *ofmt;

  ofmt = av_guess_format( fmtname, filename, "video/mpeg2" );
  if ( !ofmt ) {
    die( "Can't guess output format" );
  }

  out_ctx = avformat_alloc_context(  );
  if ( !out_ctx ) {
    die( "Can't allocate format context" );
  }

  avcodec_get_context_defaults(out_ctx);

  out_ctx->oformat = ofmt;

  snprintf( out_ctx->filename, sizeof( out_ctx->filename ), "%s",
            filename );

  if ( av_set_parameters( out_ctx, NULL ) < 0 ) {
    die( "Invalid output format parameters" );
  }

  add_video_stream( out_ctx, in_ctx, vstream );

  if ( url_fopen( &out_ctx->pb, filename, URL_WRONLY ) < 0 ) {
    die( "Can't write \"%s\"", filename );
  }

  av_write_header( out_ctx );

  return out_ctx;
}

static void
new_segment( const char *name, int *seq, const char *fmtname,
             AVFormatContext ** out_ctx, AVFormatContext * in_ctx,
             int vstream ) {
  char filename[256];
  close_chunk_writer( *out_ctx );
  snprintf( filename, sizeof( filename ), name, ( *seq )++ );
  *out_ctx = open_chunk_writer( filename, fmtname, in_ctx, vstream );
}

int
main( int argc, char *argv[] ) {
  AVFormatContext *in_ctx = NULL;
  AVFormatContext *out_ctx = NULL;
  AVPacket packet;
  int gop = 1;
  int vstream;
  int i, seq;

  if ( argc < 1 ) {
    die( "Please name a file to split" );
  }

  av_register_all(  );

  if ( av_open_input_file( &in_ctx, argv[1], NULL, 0, NULL ) ) {
    die( "Can't read \"%s\"", argv[1] );
  }

  if ( av_find_stream_info( in_ctx ) < 0 ) {
    die( "Can't find stream" );
  }

  /* dump_format( in_ctx, 0, argv[1], false ); */

  vstream = -1;
  for ( i = 0; i < in_ctx->nb_streams; i++ ) {
    if ( in_ctx->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO ) {
      vstream = i;
      break;
    }
  }

  if ( vstream == -1 ) {
    die( "Can't find a video stream" );
  }

  seq = 1;
  new_segment( "seq%00004d.ts", &seq, in_ctx->iformat->name, &out_ctx,
               in_ctx, vstream );
  while ( av_read_frame( in_ctx, &packet ) >= 0 ) {
    if ( packet.stream_index == vstream ) {
      if ( packet.flags & AV_PKT_FLAG_KEY ) {
        printf( "\nGOP %d ", gop++ );
      }
      else {
        printf( "." );
      }
    }
    av_write_frame( out_ctx, &packet );
    av_free_packet( &packet );
  }
  printf( "\n" );

  close_chunk_writer( out_ctx );
  av_close_input_file( in_ctx );
  av_free( out_ctx );

  return 0;
}

/* vim:ts=2:sw=2:sts=2:et:ft=c 
 */
