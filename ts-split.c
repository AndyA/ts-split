/* ts-split.c */

#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libavformat/avformat.h"
#include "libavutil/avstring.h"
#include "libavutil/avutil.h"

#define PROG "ts-split"
#define VERSION "0.01"
#define CHUNK 1                 /* default # gops per chunk */
#define THREAD_COUNT 1

typedef struct {
  AVStream *st;
  int discard;
} input_stream;

typedef struct {
  AVStream *st;
  input_stream *ist;
} output_stream;

typedef struct {
  AVFormatContext *file;
  input_stream **st;
} tss_input;

typedef struct {
  AVFormatContext *file;
  output_stream **st;
} tss_output;

static int verbose = 0;
static int chunk_size = CHUNK;

static float mux_preload = 0.5;
static float mux_max_delay = 0.7;

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
mention( const char *msg, ... ) {
  if ( verbose ) {
    va_list ap;
    va_start( ap, msg );
    vprintf( msg, ap );
    printf( "\n" );
    va_end( ap );
  }
}

static void
oom( void ) {
  die( "Out of memory" );
}

static void *
mallocz( size_t size ) {
  void *p = av_mallocz( size );
  if ( !p ) {
    oom(  );
  }
  return p;
}

static AVFormatContext *
alloc_context( void ) {
  AVFormatContext *c = avformat_alloc_context(  );
  if ( !c ) {
    oom(  );
  }
  return c;
}

static AVFormatContext *
set_input_file( const char *name ) {
  AVFormatContext *ic;
  AVFormatParameters params, *ap = &params;
  int i, ret;

  if ( !strcmp( name, "-" ) ) {
    name = "pipe:";
  }

  ic = alloc_context(  );

  memset( ap, 0, sizeof( *ap ) );
  ap->prealloced_context = 1;
  ap->channel = 0;
  ap->standard = NULL;

  ic->video_codec_id = CODEC_ID_NONE;
  ic->audio_codec_id = CODEC_ID_NONE;
  ic->subtitle_codec_id = CODEC_ID_NONE;
  ic->flags |= AVFMT_FLAG_NONBLOCK;

  if ( av_open_input_file( &ic, name, NULL, 0, ap ) < 0 ) {
    die( "Can't open \"%s\"", name );
  }

  ic->loop_input = 0;

  ret = av_find_stream_info( ic );
  if ( ret < 0 ) {
    av_close_input_file( ic );
    die( "Can't find codec parameters for \"%s\"", name );
  }

  for ( i = 0; i < ic->nb_streams; i++ ) {
    AVStream *st = ic->streams[i];
    AVCodecContext *dec = st->codec;
    avcodec_thread_init( dec, THREAD_COUNT );
  }

  return ic;
}

static AVStream *
alloc_stream( AVFormatContext * s, int id ) {
  AVStream *ns = av_new_stream( s, id );
  if ( !ns ) {
    die( "Failed to allocate stream" );
  }
  return ns;
}

static void
new_stream( AVFormatContext * oc, int type ) {
  AVStream *st = alloc_stream( oc, oc->nb_streams < 0 );
  st->stream_copy = 1;
  avcodec_get_context_defaults3( st->codec, NULL );
  avcodec_thread_init( st->codec, THREAD_COUNT );
  st->codec->codec_type = type;
}

static AVFormatContext *
set_output_file( const char *name, tss_input * in ) {
  AVFormatContext *oc;
  enum AVMediaType codec_type;
  int i, err, nfound;
  AVFormatParameters params, *ap = &params;
  AVOutputFormat *file_oformat;

  if ( !strcmp( name, "-" ) )
    name = "pipe:";

  oc = alloc_context(  );
  file_oformat = av_guess_format( NULL, name, NULL );
  if ( !file_oformat ) {
    die( "Unable to find a suitable output format for \"%s\"", name );
  }

  oc->oformat = file_oformat;
  av_strlcpy( oc->filename, name, sizeof( oc->filename ) );

  nfound = 0;
  for ( i = 0; i < in->file->nb_streams; i++ ) {
    codec_type = in->st[i]->st->codec->codec_type;
    if ( codec_type == AVMEDIA_TYPE_VIDEO
         || codec_type == AVMEDIA_TYPE_AUDIO ) {
      new_stream( oc, codec_type );
      nfound++;
    }
  }
  if ( !nfound ) {
    die( "No streams found in input" );
  }

  oc->timestamp = 0;

  if ( ( err = url_fopen( &oc->pb, name, URL_WRONLY ) ) < 0 ) {
    die( "Can't write \"%s\"", name );
  }

  memset( ap, 0, sizeof( *ap ) );
  if ( av_set_parameters( oc, ap ) < 0 ) {
    die( "Invalid encoding parameters" );
  }

  oc->preload = ( int ) ( mux_preload * AV_TIME_BASE );
  oc->max_delay = ( int ) ( mux_max_delay * AV_TIME_BASE );

  return oc;
}

static void
set_input( tss_input * in, const char *name ) {
  int i, nb_istreams;
  input_stream *ist;

  in->file = set_input_file( name );
  nb_istreams = in->file->nb_streams;
  in->st = mallocz( nb_istreams * sizeof( input_stream * ) );
  for ( i = 0; i < nb_istreams; i++ ) {
    ist = in->st[i] = mallocz( sizeof( input_stream ) );
    ist->st = in->file->streams[i];
    ist->discard = 1;
  }
}

static void
set_output( tss_output * out, tss_input * in, const char *name ) {
  int i, j, nb_ostreams;
  output_stream *ost;
  input_stream *ist;
  AVCodecContext *codec, *icodec;

  out->file = set_output_file( name, in );

  for ( i = 0; i < in->file->nb_streams; i++ ) {
    in->st[i]->discard = 1;
  }

  nb_ostreams = out->file->nb_streams;
  out->st = mallocz( nb_ostreams * sizeof( output_stream * ) );
  for ( i = 0; i < nb_ostreams; i++ ) {
    ost = out->st[i] = mallocz( sizeof( output_stream ) );
    ost->st = out->file->streams[i];
    ost->ist = NULL;

    int best_nb_frames = -1;
    ost->ist = NULL;
    for ( j = 0; j < in->file->nb_streams; j++ ) {
      ist = in->st[j];
      if ( ist->discard && ist->st->discard != AVDISCARD_ALL &&
           ist->st->codec->codec_type == ost->st->codec->codec_type ) {
        if ( ost->ist == NULL
             || best_nb_frames < ist->st->codec_info_nb_frames ) {
          best_nb_frames = ist->st->codec_info_nb_frames;
          ost->ist = ist;
        }
      }
    }

    if ( !ost->ist ) {
      die( "Failed to map input %d to output", i );
    }

    ist = ost->ist;
    ist->discard = 0;

    codec = ost->st->codec;
    icodec = ist->st->codec;

    av_metadata_copy( &ost->st->metadata, ist->st->metadata,
                      AV_METADATA_DONT_OVERWRITE );

    ost->st->disposition = ist->st->disposition;
    codec->bits_per_raw_sample = icodec->bits_per_raw_sample;
    codec->chroma_sample_location = icodec->chroma_sample_location;

    codec->codec_id = icodec->codec_id;
    codec->codec_type = icodec->codec_type;
    codec->codec_tag = icodec->codec_tag;
    codec->bit_rate = icodec->bit_rate;
    codec->time_base = icodec->time_base;
    codec->rc_max_rate = icodec->rc_max_rate;
    codec->rc_buffer_size = icodec->rc_buffer_size;
    codec->extradata = NULL;

    switch ( codec->codec_type ) {
    case AVMEDIA_TYPE_AUDIO:
      codec->channel_layout = icodec->channel_layout;
      codec->sample_rate = icodec->sample_rate;
      codec->channels = icodec->channels;
      codec->frame_size = icodec->frame_size;
      codec->block_align = icodec->block_align;
      if ( codec->block_align == 1 && codec->codec_id == CODEC_ID_MP3 ) {
        codec->block_align = 0;
      }
      if ( codec->codec_id == CODEC_ID_AC3 ) {
        codec->block_align = 0;
      }
      break;

    case AVMEDIA_TYPE_VIDEO:
      codec->pix_fmt = icodec->pix_fmt;
      codec->width = icodec->width;
      codec->height = icodec->height;
      codec->has_b_frames = icodec->has_b_frames;
      ost->st->sample_aspect_ratio =
          codec->sample_aspect_ratio = icodec->sample_aspect_ratio;
      break;

    default:
      die( "Unhandled codec type" );
    }
  }
}

static void
free_input( tss_input * in ) {
  int i;
  for ( i = 0; i < in->file->nb_streams; i++ ) {
    av_free( in->st[i] );
  }

  av_free( in->st );
  av_close_input_file( in->file );
}

static void
free_output( tss_output * out ) {
  int i;
  output_stream *ost;

  for ( i = 0; i < out->file->nb_streams; i++ ) {
    ost = out->st[i];
    if ( ost ) {
      av_freep( &ost->st->codec->extradata );
      av_freep( &ost->st->codec->subtitle_header );
      av_free( ost );
    }
  }
  av_free( out->st );
  out->st = NULL;

  url_fclose( out->file->pb );
  for ( i = 0; i < out->file->nb_streams; i++ ) {
    av_metadata_free( &out->file->streams[i]->metadata );
    av_free( out->file->streams[i]->codec );
    av_free( out->file->streams[i]->info );
    av_free( out->file->streams[i] );
  }
  for ( i = 0; i < out->file->nb_programs; i++ ) {
    av_metadata_free( &out->file->programs[i]->metadata );
  }
  for ( i = 0; i < out->file->nb_chapters; i++ ) {
    av_metadata_free( &out->file->chapters[i]->metadata );
  }
  av_metadata_free( &out->file->metadata );
  av_free( out->file );
  out->file = NULL;
}

static void
start_output( tss_output * out, tss_input * in, const char *name, int seq ) {
  char *tmpn;

  if ( asprintf( &tmpn, name, seq ) < 0 ) {
    oom(  );
  }
  mention( "Writing %s", tmpn );

  set_output( out, in, tmpn );
  free( tmpn );

  av_metadata_copy( &out->file->metadata,
                    in->file->metadata, AV_METADATA_DONT_OVERWRITE );

  if ( av_write_header( out->file ) < 0 ) {
    die( "Failed to write header" );
  }
}

static void
end_output( tss_output * out ) {
  if ( av_write_trailer( out->file ) < 0 ) {
    die( "Failed to write trailer" );
  }

  free_output( out );
}

/* Verify that the output filename contains a single printf-style format
 * specifier and that the specifier is of the form %[0-9]*[diouxX].
 */
static int
valid_format_string( const char *s ) {
  int c, got_fmt = 0;
  static const char *int_spec = "diouxX";

  while ( ( c = *s++ ) ) {
    if ( c == '%' ) {
      if ( *s == '%' ) {        /* %% escape */
        s++;
        continue;
      }

      /* optional field width specifier */
      while ( isdigit( *s ) ) { /* %NNN */
        s++;
      }
      if ( got_fmt || !*s || !strchr( int_spec, *s ) ) {
        return 0;
      }
      got_fmt++;
    }
  }

  return got_fmt;
}

static void
tssplit( const char *input_name, const char *output_name ) {
  tss_input in;
  tss_output out;
  int seq = 0;
  int done_output = 0;
  int gop_count = 0;

  if ( !valid_format_string( output_name ) ) {
    die( "Invalid or missing conversion format in output filename" );
  }

  mention( "Splitting %s", input_name );

  set_input( &in, input_name );
  start_output( &out, &in, output_name, seq++ );

  for ( ;; ) {
    AVPacket pkt;
    int si;

    if ( av_read_frame( in.file, &pkt ) < 0 ) {
      break;
    }

    si = pkt.stream_index;
    if ( si < in.file->nb_streams && !in.st[si]->discard ) {

      if ( in.st[si]->st->codec->codec_type == AVMEDIA_TYPE_VIDEO
           && pkt.flags & AV_PKT_FLAG_KEY ) {
        ++gop_count;
        if ( gop_count >= chunk_size && done_output ) {
          end_output( &out );
          start_output( &out, &in, output_name, seq++ );
          done_output = 0;
          gop_count = 0;
        }
      }

      if ( av_interleaved_write_frame( out.file, &pkt ) < 0 ) {
        die( "Frame write failed" );
      }
      ++done_output;
    }

    av_free_packet( &pkt );
  }

  end_output( &out );
  free_input( &in );
}

static void
usage( void ) {
  fprintf( stderr, "Usage: " PROG " [options] <in.ts> <out%%03d.ts>\n\n"
           "Options:\n"
           "  -C<n>,  --chunk=<n>      Number of gops per chunk (1)\n"
           "  -V,     --version        See version number\n"
           "  -v,     --verbose        Verbose output\n"
           "  -h,     --help           See this text\n" );
  exit( 1 );
}

int
main( int argc, char **argv ) {
  int ch;

  static struct option opts[] = {
    {"chunk", required_argument, NULL, 'C'},
    {"help", no_argument, NULL, 'h'},
    {"verbose", no_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {NULL, 0, NULL, 0}
  };

  av_log_set_level( AV_LOG_ERROR );
  avcodec_register_all(  );
  av_register_all(  );

  while ( ch = getopt_long( argc, argv, "hvVC:", opts, NULL ), ch != -1 ) {
    switch ( ch ) {
    case 'v':
      verbose++;
      break;
    case 'V':
      printf( "%s %s\n", PROG, VERSION );
      return 0;
    case 'C':
      {
        char *ep;
        chunk_size = ( int ) strtol( optarg, &ep, 10 );
        if ( *ep ) {
          die( "Bad chunk size" );
        }
      }
    case 0:
      break;
    case 'h':
    default:
      usage(  );
    }
  }

  argc -= optind;
  argv += optind;

  if ( argc != 2 ) {
    usage(  );
  }

  tssplit( argv[0], argv[1] );

  return 0;
}
