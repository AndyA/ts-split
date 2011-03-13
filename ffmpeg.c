/*
 * FFmpeg main
 * Copyright (c) 2000-2003 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/* needed for usleep() */
#define _XOPEN_SOURCE 600

#include "config.h"
#include <ctype.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <unistd.h>
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavcodec/opt.h"
#include "libavcodec/audioconvert.h"
#include "libavcore/audioconvert.h"
#include "libavcore/parseutils.h"
#include "libavcore/samplefmt.h"
#include "libavutil/colorspace.h"
#include "libavutil/fifo.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avstring.h"
#include "libavutil/libm.h"
#include "libavformat/os_support.h"

#if CONFIG_AVFILTER
# include "libavfilter/avfilter.h"
# include "libavfilter/avfiltergraph.h"
# include "libavfilter/vsrc_buffer.h"
#endif

#if HAVE_SYS_RESOURCE_H
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#elif HAVE_GETPROCESSTIMES
#include <windows.h>
#endif
#if HAVE_GETPROCESSMEMORYINFO
#include <windows.h>
#include <psapi.h>
#endif

#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#if HAVE_TERMIOS_H
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#elif HAVE_CONIO_H
#include <conio.h>
#endif
#include <time.h>

#include "cmdutils.h"

#include "libavutil/avassert.h"

const char program_name[] = "FFmpeg";
const int program_birth_year = 2000;

/* select an input stream for an output stream */
typedef struct AVStreamMap {
  int file_index;
  int stream_index;
  int sync_file_index;
  int sync_stream_index;
} AVStreamMap;

/**
 * select an input file for an output file
 */
typedef struct AVMetaDataMap {
  int file;                     //< file index
  char type;                    //< type of metadata to copy -- (g)lobal, (s)tream, (c)hapter or (p)rogram
  int index;                    //< stream/chapter/program number
} AVMetaDataMap;

typedef struct AVChapterMap {
  int in_file;
  int out_file;
} AVChapterMap;

static const OptionDef options[];

#define MAX_FILES 100
#if !FF_API_MAX_STREAMS
#define MAX_STREAMS 1024        /* arbitrary sanity check value */
#endif

static const char *last_asked_format = NULL;
static AVFormatContext *input_files[MAX_FILES];
static int64_t input_files_ts_offset[MAX_FILES];
static double *input_files_ts_scale[MAX_FILES] = { NULL };

static AVCodec **input_codecs = NULL;
static int nb_input_files = 0;
static int nb_input_codecs = 0;
static int nb_input_files_ts_scale[MAX_FILES] = { 0 };

static AVFormatContext *output_files[MAX_FILES];
static AVCodec **output_codecs = NULL;
static int nb_output_files = 0;
static int nb_output_codecs = 0;

static AVStreamMap *stream_maps = NULL;
static int nb_stream_maps;

/* first item specifies output metadata, second is input */
static
AVMetaDataMap( *meta_data_maps )[2] = NULL;
static int nb_meta_data_maps;
static int metadata_global_autocopy = 1;
static int metadata_streams_autocopy = 1;
static int metadata_chapters_autocopy = 1;

static AVChapterMap *chapter_maps = NULL;
static int nb_chapter_maps;

/* indexed by output file stream index */
static int *streamid_map = NULL;
static int nb_streamid_map = 0;

static int frame_width = 0;
static int frame_height = 0;
static float frame_aspect_ratio = 0;
static enum PixelFormat frame_pix_fmt = PIX_FMT_NONE;
static enum AVSampleFormat audio_sample_fmt = AV_SAMPLE_FMT_NONE;
static int max_frames[4] = { INT_MAX, INT_MAX, INT_MAX, INT_MAX };

static AVRational frame_rate;
static float video_qscale = 0;
static uint16_t *intra_matrix = NULL;
static uint16_t *inter_matrix = NULL;
static const char *video_rc_override_string = NULL;
static int video_disable = 0;
static int video_discard = 0;
static char *video_codec_name = NULL;
static unsigned int video_codec_tag = 0;
static char *video_language = NULL;
static int same_quality = 0;
static int do_deinterlace = 0;
static int top_field_first = -1;
static int me_threshold = 0;
static int intra_dc_precision = 8;
static int loop_input = 0;
static int loop_output = AVFMT_NOOUTPUTLOOP;
#if CONFIG_AVFILTER
static char *vfilters = NULL;
AVFilterGraph *graph = NULL;
#endif

static int intra_only = 0;
static int audio_sample_rate = 44100;
static int64_t channel_layout = 0;
#define QSCALE_NONE -99999
static float audio_qscale = QSCALE_NONE;
static int audio_disable = 0;
static int audio_channels = 1;
static char *audio_codec_name = NULL;
static unsigned int audio_codec_tag = 0;
static char *audio_language = NULL;

static int subtitle_disable = 0;
static char *subtitle_codec_name = NULL;
static char *subtitle_language = NULL;
static unsigned int subtitle_codec_tag = 0;

static float mux_preload = 0.5;
static float mux_max_delay = 0.7;

static int64_t recording_time = INT64_MAX;
static int64_t start_time = 0;
static int64_t recording_timestamp = 0;
static int64_t input_ts_offset = 0;
static int file_overwrite = 0;
static AVMetadata *metadata;
static int do_hex_dump = 0;
static int do_pkt_dump = 0;
static int do_psnr = 0;
static int do_pass = 0;
static char *pass_logfilename_prefix = NULL;
static int audio_stream_copy = 0;
static int video_stream_copy = 0;
static int subtitle_stream_copy = 0;
static int video_sync_method = -1;
static int audio_sync_method = 0;
static float audio_drift_threshold = 0.1;
static int copy_ts = 0;
static int copy_tb;
static int opt_shortest = 0;
static int video_global_header = 0;
static char *vstats_filename;
static int opt_programid = 0;
static int copy_initial_nonkeyframes = 0;

static int video_channel = 0;
static char *video_standard;

static int exit_on_error = 0;
static int using_stdin = 0;
static int verbose = 1;
static int thread_count = 1;
static int q_pressed = 0;
static int64_t video_size = 0;
static int64_t audio_size = 0;
static int64_t extra_size = 0;
static int nb_frames_dup = 0;
static int nb_frames_drop = 0;
static int input_sync;
static uint64_t limit_filesize = 0;
static int force_fps = 0;
static char *forced_key_frames = NULL;

static float dts_delta_threshold = 10;

static unsigned int sws_flags = SWS_BICUBIC;

static int64_t timer_start;

static uint8_t *audio_buf;
static uint8_t *audio_out;
unsigned int allocated_audio_out_size, allocated_audio_buf_size;

static short *samples;

static AVBitStreamFilterContext *video_bitstream_filters = NULL;
static AVBitStreamFilterContext *audio_bitstream_filters = NULL;
static AVBitStreamFilterContext *subtitle_bitstream_filters = NULL;

#define DEFAULT_PASS_LOGFILENAME_PREFIX "ffmpeg2pass"

struct AVInputStream;

typedef struct AVOutputStream {
  int file_index;               /* file index */
  int index;                    /* stream index in the output file */
  int source_index;             /* AVInputStream index */
  AVStream *st;                 /* stream in the output file */
  int encoding_needed;          /* true if encoding needed for this stream */
  int frame_number;
  /* input pts and corresponding output pts
     for A/V sync */
  //double sync_ipts;        /* dts from the AVPacket of the demuxer in second units */
  struct AVInputStream *sync_ist;       /* input stream to sync against */
  int64_t sync_opts;            /* output frame counter, could be changed to some true timestamp *///FIXME look at frame_number
  AVBitStreamFilterContext *bitstream_filters;
  /* video only */
  int video_resample;
  AVFrame pict_tmp;             /* temporary image for resampling */
  struct SwsContext *img_resample_ctx;  /* for image resampling */
  int resample_height;
  int resample_width;
  int resample_pix_fmt;

  /* full frame size of first frame */
  int original_height;
  int original_width;

  /* forced key frames */
  int64_t *forced_kf_pts;
  int forced_kf_count;
  int forced_kf_index;

  /* audio only */
  int audio_resample;
  ReSampleContext *resample;    /* for audio resampling */
  int resample_sample_fmt;
  int resample_channels;
  int resample_sample_rate;
  int reformat_pair;
  AVAudioConvert *reformat_ctx;
  AVFifoBuffer *fifo;           /* for compression: one audio fifo per codec */
  FILE *logfile;
} AVOutputStream;

static AVOutputStream **output_streams_for_file[MAX_FILES] = { NULL };
static int nb_output_streams_for_file[MAX_FILES] = { 0 };

typedef struct AVInputStream {
  int file_index;
  int index;
  AVStream *st;
  int discard;                  /* true if stream data should be discarded */
  int decoding_needed;          /* true if the packets must be decoded in 'raw_fifo' */
  int64_t sample_index;         /* current sample */

  int64_t start;                /* time when read started */
  int64_t next_pts;             /* synthetic pts for cases where pkt.pts
                                   is not defined */
  int64_t pts;                  /* current pts */
  PtsCorrectionContext pts_ctx;
  int is_start;                 /* is 1 at the start and after a discontinuity */
  int showed_multi_packet_warning;
  int is_past_recording_time;
#if CONFIG_AVFILTER
  AVFilterContext *output_video_filter;
  AVFilterContext *input_video_filter;
  AVFrame *filter_frame;
  int has_filter_frame;
  AVFilterBufferRef *picref;
#endif
} AVInputStream;

typedef struct AVInputFile {
  int eof_reached;              /* true if eof reached */
  int ist_index;                /* index of first stream in ist_table */
  int buffer_size;              /* current total buffer size */
  int nb_streams;               /* nb streams we are aware of */
} AVInputFile;

#if HAVE_TERMIOS_H

/* init terminal so that we can grab keys */
static struct termios oldtty;
#endif

static void
term_exit( void ) {
  av_log( NULL, AV_LOG_QUIET, "" );
#if HAVE_TERMIOS_H
  tcsetattr( 0, TCSANOW, &oldtty );
#endif
}

static volatile int received_sigterm = 0;

static void
sigterm_handler( int sig ) {
  received_sigterm = sig;
  term_exit(  );
}

static void
term_init( void ) {
#if HAVE_TERMIOS_H
  struct termios tty;

  tcgetattr( 0, &tty );
  oldtty = tty;
  atexit( term_exit );

  tty.c_iflag &= ~( IGNBRK | BRKINT | PARMRK | ISTRIP
                    | INLCR | IGNCR | ICRNL | IXON );
  tty.c_oflag |= OPOST;
  tty.c_lflag &= ~( ECHO | ECHONL | ICANON | IEXTEN );
  tty.c_cflag &= ~( CSIZE | PARENB );
  tty.c_cflag |= CS8;
  tty.c_cc[VMIN] = 1;
  tty.c_cc[VTIME] = 0;

  tcsetattr( 0, TCSANOW, &tty );
  signal( SIGQUIT, sigterm_handler );   /* Quit (POSIX).  */
#endif

  signal( SIGINT, sigterm_handler );    /* Interrupt (ANSI).  */
  signal( SIGTERM, sigterm_handler );   /* Termination (ANSI).  */
#ifdef SIGXCPU
  signal( SIGXCPU, sigterm_handler );
#endif
}

/* read a key without blocking */
static int
read_key( void ) {
#if HAVE_TERMIOS_H
  int n = 1;
  unsigned char ch;
  struct timeval tv;
  fd_set rfds;

  FD_ZERO( &rfds );
  FD_SET( 0, &rfds );
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  n = select( 1, &rfds, NULL, NULL, &tv );
  if ( n > 0 ) {
    n = read( 0, &ch, 1 );
    if ( n == 1 )
      return ch;

    return n;
  }
#elif HAVE_CONIO_H
  if ( kbhit(  ) )
    return ( getch(  ) );
#endif
  return -1;
}

static int
decode_interrupt_cb( void ) {
  return q_pressed || ( q_pressed = read_key(  ) == 'q' );
}

static int
ffmpeg_exit( int ret ) {
  int i;

  /* close files */
  for ( i = 0; i < nb_output_files; i++ ) {
    /* maybe av_close_output_file ??? */
    AVFormatContext *s = output_files[i];
    int j;
    if ( !( s->oformat->flags & AVFMT_NOFILE ) && s->pb )
      url_fclose( s->pb );
    for ( j = 0; j < s->nb_streams; j++ ) {
      av_metadata_free( &s->streams[j]->metadata );
      av_free( s->streams[j]->codec );
      av_free( s->streams[j]->info );
      av_free( s->streams[j] );
    }
    for ( j = 0; j < s->nb_programs; j++ ) {
      av_metadata_free( &s->programs[j]->metadata );
    }
    for ( j = 0; j < s->nb_chapters; j++ ) {
      av_metadata_free( &s->chapters[j]->metadata );
    }
    av_metadata_free( &s->metadata );
    av_free( s );
    av_free( output_streams_for_file[i] );
  }
  for ( i = 0; i < nb_input_files; i++ ) {
    av_close_input_file( input_files[i] );
    av_free( input_files_ts_scale[i] );
  }

  av_free( intra_matrix );
  av_free( inter_matrix );

  av_free( vstats_filename );

  av_free( opt_names );
  av_free( streamid_map );
  av_free( input_codecs );
  av_free( output_codecs );
  av_free( stream_maps );
  av_free( meta_data_maps );

  av_free( video_codec_name );
  av_free( audio_codec_name );
  av_free( subtitle_codec_name );

  av_free( video_standard );

  uninit_opts(  );
  av_free( audio_buf );
  av_free( audio_out );
  allocated_audio_buf_size = allocated_audio_out_size = 0;
  av_free( samples );

#if CONFIG_AVFILTER
  avfilter_uninit(  );
#endif

  if ( received_sigterm ) {
    fprintf( stderr,
             "Received signal %d: terminating.\n",
             ( int ) received_sigterm );
    exit( 255 );
  }

  exit( ret );                  /* not all OS-es handle main() return value */
  return ret;
}

/* similar to ff_dynarray_add() and av_fast_realloc() */
static void *
grow_array( void *array, int elem_size, int *size, int new_size ) {
  if ( new_size >= INT_MAX / elem_size ) {
    fprintf( stderr, "Array too big.\n" );
    ffmpeg_exit( 1 );
  }
  if ( *size < new_size ) {
    uint8_t *tmp = av_realloc( array, new_size * elem_size );
    if ( !tmp ) {
      fprintf( stderr, "Could not alloc buffer.\n" );
      ffmpeg_exit( 1 );
    }
    memset( tmp + *size * elem_size, 0, ( new_size - *size ) * elem_size );
    *size = new_size;
    return tmp;
  }
  return array;
}

static AVOutputStream *
new_output_stream( AVFormatContext * oc, int file_idx ) {
  int idx = oc->nb_streams - 1;
  AVOutputStream *ost;

  output_streams_for_file[file_idx] =
      grow_array( output_streams_for_file[file_idx],
                  sizeof( *output_streams_for_file[file_idx] ),
                  &nb_output_streams_for_file[file_idx], oc->nb_streams );
  ost = output_streams_for_file[file_idx][idx] =
      av_mallocz( sizeof( AVOutputStream ) );
  if ( !ost ) {
    fprintf( stderr, "Could not alloc output stream\n" );
    ffmpeg_exit( 1 );
  }
  ost->file_index = file_idx;
  ost->index = idx;
  return ost;
}

static void
write_frame( AVFormatContext * s, AVPacket * pkt, AVCodecContext * avctx,
             AVBitStreamFilterContext * bsfc ) {
  int ret;

  while ( bsfc ) {
    AVPacket new_pkt = *pkt;
    int a = av_bitstream_filter_filter( bsfc, avctx, NULL,
                                        &new_pkt.data, &new_pkt.size,
                                        pkt->data, pkt->size,
                                        pkt->flags & AV_PKT_FLAG_KEY );
    if ( a > 0 ) {
      av_free_packet( pkt );
      new_pkt.destruct = av_destruct_packet;
    }
    else if ( a < 0 ) {
      fprintf( stderr, "%s failed for stream %d, codec %s",
               bsfc->filter->name, pkt->stream_index,
               avctx->codec ? avctx->codec->name : "copy" );
      print_error( "", a );
      if ( exit_on_error )
        ffmpeg_exit( 1 );
    }
    *pkt = new_pkt;

    bsfc = bsfc->next;
  }

  ret = av_interleaved_write_frame( s, pkt );
  if ( ret < 0 ) {
    print_error( "av_interleaved_write_frame()", ret );
    ffmpeg_exit( 1 );
  }
}

/* we begin to correct av delay at this threshold */
#define AV_DELAY_MAX 0.100

static int bit_buffer_size = 1024 * 256;
static uint8_t *bit_buffer = NULL;

static void
print_report( AVFormatContext ** output_files,
              AVOutputStream ** ost_table, int nb_ostreams,
              int is_last_report ) {
  char buf[1024];
  AVOutputStream *ost;
  AVFormatContext *oc;
  int64_t total_size;
  AVCodecContext *enc;
  int frame_number, vid, i;
  double bitrate, ti1, pts;
  static int64_t last_time = -1;
  static int qp_histogram[52];

  if ( !is_last_report ) {
    int64_t cur_time;
    /* display the report every 0.5 seconds */
    cur_time = av_gettime(  );
    if ( last_time == -1 ) {
      last_time = cur_time;
      return;
    }
    if ( ( cur_time - last_time ) < 500000 )
      return;
    last_time = cur_time;
  }

  oc = output_files[0];

  total_size = url_fsize( oc->pb );
  if ( total_size < 0 )         // FIXME improve url_fsize() so it works with non seekable output too
    total_size = url_ftell( oc->pb );

  buf[0] = '\0';
  ti1 = 1e10;
  vid = 0;
  for ( i = 0; i < nb_ostreams; i++ ) {
    ost = ost_table[i];
    enc = ost->st->codec;
    if ( !vid && enc->codec_type == AVMEDIA_TYPE_VIDEO ) {
      float t = ( av_gettime(  ) - timer_start ) / 1000000.0;

      frame_number = ost->frame_number;
      snprintf( buf + strlen( buf ), sizeof( buf ) - strlen( buf ),
                "frame=%5d fps=%3d q=%3.1f ", frame_number,
                ( t > 1 ) ? ( int ) ( frame_number / t + 0.5 ) : 0,
                !ost->st->stream_copy ? enc->coded_frame->quality /
                ( float ) FF_QP2LAMBDA : -1 );
      if ( is_last_report )
        snprintf( buf + strlen( buf ), sizeof( buf ) - strlen( buf ),
                  "L" );
      vid = 1;
    }
    /* compute min output value */
    pts = ( double ) ost->st->pts.val * av_q2d( ost->st->time_base );
    if ( ( pts < ti1 ) && ( pts > 0 ) )
      ti1 = pts;
  }
  if ( ti1 < 0.01 )
    ti1 = 0.01;

  if ( verbose || is_last_report ) {
    bitrate = ( double ) ( total_size * 8 ) / ti1 / 1000.0;

    snprintf( buf + strlen( buf ), sizeof( buf ) - strlen( buf ),
              "size=%8.0fkB time=%0.2f bitrate=%6.1fkbits/s",
              ( double ) total_size / 1024, ti1, bitrate );

    if ( nb_frames_dup || nb_frames_drop )
      snprintf( buf + strlen( buf ), sizeof( buf ) - strlen( buf ),
                " dup=%d drop=%d", nb_frames_dup, nb_frames_drop );

    if ( verbose >= 0 )
      fprintf( stderr, "%s    \r", buf );

    fflush( stderr );
  }

  if ( is_last_report && verbose >= 0 ) {
    int64_t raw = audio_size + video_size + extra_size;
    fprintf( stderr, "\n" );
    fprintf( stderr,
             "video:%1.0fkB audio:%1.0fkB global headers:%1.0fkB muxing overhead %f%%\n",
             video_size / 1024.0, audio_size / 1024.0, extra_size / 1024.0,
             100.0 * ( total_size - raw ) / raw );
  }
}

/* pkt = NULL means EOF (needed to flush decoder buffers) */
static int
output_packet( AVInputStream * ist, int ist_index,
               AVOutputStream ** ost_table, int nb_ostreams,
               const AVPacket * pkt ) {
  AVFormatContext *os;
  AVOutputStream *ost;
  int ret, i;
  int got_picture;
  AVFrame picture;
  void *buffer_to_free;
  static unsigned int samples_size = 0;
  AVSubtitle subtitle, *subtitle_to_free;
  int64_t pkt_pts = AV_NOPTS_VALUE;
#if CONFIG_AVFILTER
  int frame_available;
#endif

  AVPacket avpkt;
  int bps = av_get_bits_per_sample_fmt( ist->st->codec->sample_fmt ) >> 3;

  if ( ist->next_pts == AV_NOPTS_VALUE )
    ist->next_pts = ist->pts;

  if ( pkt == NULL ) {
    /* EOF handling */
    av_init_packet( &avpkt );
    avpkt.data = NULL;
    avpkt.size = 0;
    goto handle_eof;
  }
  else {
    avpkt = *pkt;
  }

  if ( pkt->dts != AV_NOPTS_VALUE )
    ist->next_pts = ist->pts =
        av_rescale_q( pkt->dts, ist->st->time_base, AV_TIME_BASE_Q );
  if ( pkt->pts != AV_NOPTS_VALUE )
    pkt_pts = av_rescale_q( pkt->pts, ist->st->time_base, AV_TIME_BASE_Q );

  //while we have more to decode or while the decoder did output something on EOF
  while ( avpkt.size > 0 || ( !pkt && ist->next_pts != ist->pts ) ) {
    uint8_t *data_buf, *decoded_data_buf;
    int data_size, decoded_data_size;
  handle_eof:
    ist->pts = ist->next_pts;

    if ( avpkt.size && avpkt.size != pkt->size &&
         ( ( !ist->showed_multi_packet_warning && verbose > 0 )
           || verbose > 1 ) ) {
      fprintf( stderr, "Multiple frames in a packet from stream %d\n",
               pkt->stream_index );
      ist->showed_multi_packet_warning = 1;
    }

    /* decode the packet if needed */
    decoded_data_buf = NULL;    /* fail safe */
    decoded_data_size = 0;
    data_buf = avpkt.data;
    data_size = avpkt.size;
    subtitle_to_free = NULL;
    if ( ist->decoding_needed ) {
    }
    else {
      switch ( ist->st->codec->codec_type ) {
      case AVMEDIA_TYPE_AUDIO:
        ist->next_pts +=
            ( ( int64_t ) AV_TIME_BASE * ist->st->codec->frame_size ) /
            ist->st->codec->sample_rate;
        break;
      case AVMEDIA_TYPE_VIDEO:
        if ( ist->st->codec->time_base.num != 0 ) {
          int ticks =
              ist->st->parser ? ist->st->parser->repeat_pict +
              1 : ist->st->codec->ticks_per_frame;
          ist->next_pts +=
              ( ( int64_t ) AV_TIME_BASE * ist->st->codec->time_base.num *
                ticks ) / ist->st->codec->time_base.den;
        }
        break;
      }
      ret = avpkt.size;
      avpkt.size = 0;
    }

    buffer_to_free = NULL;
    if ( ist->st->codec->codec_type == AVMEDIA_TYPE_VIDEO ) {
      pre_process_video_frame( ist, ( AVPicture * ) & picture,
                               &buffer_to_free );
    }

    // preprocess audio (volume)
    /* frame rate emulation */
    /* if output time reached then transcode raw format,
       encode packets and output them */
    if ( start_time == 0 || ist->pts >= start_time )
      for ( i = 0; i < nb_ostreams; i++ ) {
        int frame_size;

        ost = ost_table[i];
        if ( ost->source_index == ist_index ) {
          os = output_files[ost->file_index];

          /* set the input output pts pairs */
          //ost->sync_ipts = (double)(ist->pts + input_files_ts_offset[ist->file_index] - start_time)/ AV_TIME_BASE;

          if ( ost->encoding_needed ) {
          }
          else {
            AVFrame avframe;    //FIXME/XXX remove this
            AVPacket opkt;
            int64_t ost_tb_start_time =
                av_rescale_q( start_time, AV_TIME_BASE_Q,
                              ost->st->time_base );

            av_init_packet( &opkt );

            if ( ( !ost->frame_number
                   && !( pkt->flags & AV_PKT_FLAG_KEY ) )
                 && !copy_initial_nonkeyframes )
              continue;

            /* no reencoding needed : output the packet directly */
            /* force the input stream PTS */

            avcodec_get_frame_defaults( &avframe );
            ost->st->codec->coded_frame = &avframe;
            avframe.key_frame = pkt->flags & AV_PKT_FLAG_KEY;

            if ( ost->st->codec->codec_type == AVMEDIA_TYPE_AUDIO )
              audio_size += data_size;
            else if ( ost->st->codec->codec_type == AVMEDIA_TYPE_VIDEO ) {
              video_size += data_size;
              ost->sync_opts++;
            }

            opkt.stream_index = ost->index;
            if ( pkt->pts != AV_NOPTS_VALUE )
              opkt.pts =
                  av_rescale_q( pkt->pts, ist->st->time_base,
                                ost->st->time_base ) - ost_tb_start_time;
            else
              opkt.pts = AV_NOPTS_VALUE;

            if ( pkt->dts == AV_NOPTS_VALUE )
              opkt.dts =
                  av_rescale_q( ist->pts, AV_TIME_BASE_Q,
                                ost->st->time_base );
            else
              opkt.dts =
                  av_rescale_q( pkt->dts, ist->st->time_base,
                                ost->st->time_base );
            opkt.dts -= ost_tb_start_time;

            opkt.duration =
                av_rescale_q( pkt->duration, ist->st->time_base,
                              ost->st->time_base );
            opkt.flags = pkt->flags;

            //FIXME remove the following 2 lines they shall be replaced by the bitstream filters
            if ( ost->st->codec->codec_id != CODEC_ID_H264
                 && ost->st->codec->codec_id != CODEC_ID_MPEG1VIDEO
                 && ost->st->codec->codec_id != CODEC_ID_MPEG2VIDEO ) {
              if ( av_parser_change
                   ( ist->st->parser, ost->st->codec, &opkt.data,
                     &opkt.size, data_buf, data_size,
                     pkt->flags & AV_PKT_FLAG_KEY ) )
                opkt.destruct = av_destruct_packet;
            }
            else {
              opkt.data = data_buf;
              opkt.size = data_size;
            }

            write_frame( os, &opkt, ost->st->codec,
                         ost->bitstream_filters );
            ost->st->codec->frame_number++;
            ost->frame_number++;
            av_free_packet( &opkt );
          }
        }
      }

#if CONFIG_AVFILTER
    frame_available = ( ist->st->codec->codec_type == AVMEDIA_TYPE_VIDEO )
        && ist->output_video_filter
        && avfilter_poll_frame( ist->output_video_filter->inputs[0] );
    if ( ist->picref )
      avfilter_unref_buffer( ist->picref );
  }
#endif
  av_free( buffer_to_free );
  /* XXX: allocate the subtitles in the codec ? */
  if ( subtitle_to_free ) {
    avsubtitle_free( subtitle_to_free );
    subtitle_to_free = NULL;
  }
}

discard_packet:
if ( pkt == NULL ) {
  /* EOF handling */

  for ( i = 0; i < nb_ostreams; i++ ) {
    ost = ost_table[i];
    if ( ost->source_index == ist_index ) {
      AVCodecContext *enc = ost->st->codec;
      os = output_files[ost->file_index];

      if ( ost->st->codec->codec_type == AVMEDIA_TYPE_AUDIO
           && enc->frame_size <= 1 )
        continue;
      if ( ost->st->codec->codec_type == AVMEDIA_TYPE_VIDEO
           && ( os->oformat->flags & AVFMT_RAWPICTURE ) )
        continue;

      if ( ost->encoding_needed ) {
        for ( ;; ) {
          AVPacket pkt;
          int fifo_bytes;
          av_init_packet( &pkt );
          pkt.stream_index = ost->index;

          switch ( ost->st->codec->codec_type ) {
          case AVMEDIA_TYPE_AUDIO:
            fifo_bytes = av_fifo_size( ost->fifo );
            ret = 0;
            /* encode any samples remaining in fifo */
            if ( fifo_bytes > 0 ) {
              int osize =
                  av_get_bits_per_sample_fmt( enc->sample_fmt ) >> 3;
              int fs_tmp = enc->frame_size;

              av_fifo_generic_read( ost->fifo, audio_buf, fifo_bytes,
                                    NULL );
              if ( enc->codec->capabilities & CODEC_CAP_SMALL_LAST_FRAME ) {
                enc->frame_size = fifo_bytes / ( osize * enc->channels );
              }
              else {            /* pad */
                int frame_bytes = enc->frame_size * osize * enc->channels;
                if ( allocated_audio_buf_size < frame_bytes )
                  ffmpeg_exit( 1 );
                memset( audio_buf + fifo_bytes, 0,
                        frame_bytes - fifo_bytes );
              }

              ret =
                  avcodec_encode_audio( enc, bit_buffer, bit_buffer_size,
                                        ( short * ) audio_buf );
              pkt.duration =
                  av_rescale( ( int64_t ) enc->frame_size *
                              ost->st->time_base.den,
                              ost->st->time_base.num, enc->sample_rate );
              enc->frame_size = fs_tmp;
            }
            if ( ret <= 0 ) {
              ret =
                  avcodec_encode_audio( enc, bit_buffer, bit_buffer_size,
                                        NULL );
            }
            if ( ret < 0 ) {
              fprintf( stderr, "Audio encoding failed\n" );
              ffmpeg_exit( 1 );
            }
            audio_size += ret;
            pkt.flags |= AV_PKT_FLAG_KEY;
            break;
          case AVMEDIA_TYPE_VIDEO:
            ret =
                avcodec_encode_video( enc, bit_buffer, bit_buffer_size,
                                      NULL );
            if ( ret < 0 ) {
              fprintf( stderr, "Video encoding failed\n" );
              ffmpeg_exit( 1 );
            }
            video_size += ret;
            if ( enc->coded_frame && enc->coded_frame->key_frame )
              pkt.flags |= AV_PKT_FLAG_KEY;
            if ( ost->logfile && enc->stats_out ) {
              findent: Standard input:905: Warning:old style assignment ambiguity in "=-".  Assuming "= -"

indent: Standard input:924: Error:Stmt nesting error.
indent: Standard input:1014: Warning:old style assignment ambiguity in "=-".  Assuming "= -"

printf( ost->logfile, "%s", enc->stats_out );
            }
            break;
          default:
            ret = -1;
          }

          if ( ret <= 0 )
            break;
          pkt.data = bit_buffer;
          pkt.size = ret;
          if ( enc->coded_frame
               && enc->coded_frame->pts != AV_NOPTS_VALUE )
            pkt.pts =
                av_rescale_q( enc->coded_frame->pts, enc->time_base,
                              ost->st->time_base );
          write_frame( os, &pkt, ost->st->codec, ost->bitstream_filters );
        }
      }
    }
  }
}

return 0;
fail_decode:
return -1;
}

/*
 * The following code is the main loop of the file converter
 */
static int
transcode( AVFormatContext ** output_files,
           int nb_output_files,
           AVFormatContext ** input_files,
           int nb_input_files,
           AVStreamMap * stream_maps, int nb_stream_maps ) {
  int ret = 0, i, j, k, n, nb_istreams = 0, nb_ostreams = 0;
  AVFormatContext *is, *os;
  AVCodecContext *codec, *icodec;
  AVOutputStream *ost, **ost_table = NULL;
  AVInputStream *ist, **ist_table = NULL;
  AVInputFile *file_table;
  char error[1024];
  int key;
  int want_sdp = 1;
  uint8_t no_packet[MAX_FILES] = { 0 };
  int no_packet_count = 0;

  file_table = av_mallocz( nb_input_files * sizeof( AVInputFile ) );
  if ( !file_table )
    goto fail;

  /* input stream init */
  j = 0;
  for ( i = 0; i < nb_input_files; i++ ) {
    is = input_files[i];
    file_table[i].ist_index = j;
    file_table[i].nb_streams = is->nb_streams;
    j += is->nb_streams;
  }
  nb_istreams = j;

  ist_table = av_mallocz( nb_istreams * sizeof( AVInputStream * ) );
  if ( !ist_table )
    goto fail;

  for ( i = 0; i < nb_istreams; i++ ) {
    ist = av_mallocz( sizeof( AVInputStream ) );
    if ( !ist )
      goto fail;
    ist_table[i] = ist;
  }
  j = 0;
  for ( i = 0; i < nb_input_files; i++ ) {
    is = input_files[i];
    for ( k = 0; k < is->nb_streams; k++ ) {
      ist = ist_table[j++];
      ist->st = is->streams[k];
      ist->file_index = i;
      ist->index = k;
      ist->discard = 1;         /* the stream is discarded by default
                                   (changed later) */

    }
  }

  /* output stream init */
  nb_ostreams = 0;
  for ( i = 0; i < nb_output_files; i++ ) {
    os = output_files[i];
    if ( !os->nb_streams && !( os->oformat->flags & AVFMT_NOSTREAMS ) ) {
      dump_format( output_files[i], i, output_files[i]->filename, 1 );
      fprintf( stderr, "Output file #%d does not contain any stream\n",
               i );
      ret = AVERROR( EINVAL );
      goto fail;
    }
    nb_ostreams += os->nb_streams;
  }
  if ( nb_stream_maps > 0 && nb_stream_maps != nb_ostreams ) {
    fprintf( stderr,
             "Number of stream maps must match number of output streams\n" );
    ret = AVERROR( EINVAL );
    goto fail;
  }

  ost_table = av_mallocz( sizeof( AVOutputStream * ) * nb_ostreams );
  if ( !ost_table )
    goto fail;
  n = 0;
  for ( k = 0; k < nb_output_files; k++ ) {
    os = output_files[k];
    for ( i = 0; i < os->nb_streams; i++, n++ ) {
      int found;
      ost = ost_table[n] = output_streams_for_file[k][i];
      ost->st = os->streams[i];
      int best_nb_frames = -1;
      /* get corresponding input stream index : we select the first one with the right type */
      found = 0;
      for ( j = 0; j < nb_istreams; j++ ) {
        int skip = 0;
        ist = ist_table[j];
        if ( ist->discard && ist->st->discard != AVDISCARD_ALL && !skip &&
             ist->st->codec->codec_type == ost->st->codec->codec_type ) {
          if ( best_nb_frames < ist->st->codec_info_nb_frames ) {
            best_nb_frames = ist->st->codec_info_nb_frames;
            ost->source_index = j;
            found = 1;
          }
        }
      }

      if ( !found ) {
      }
      ist = ist_table[ost->source_index];
      ist->discard = 0;
      ost->sync_ist = ( nb_stream_maps > 0 ) ?
          ist_table[file_table[stream_maps[n].sync_file_index].ist_index +
                    stream_maps[n].sync_stream_index] : ist;
    }
  }

  /* for each output stream, we compute the right encoding parameters */
  for ( i = 0; i < nb_ostreams; i++ ) {
    ost = ost_table[i];
    os = output_files[ost->file_index];
    ist = ist_table[ost->source_index];

    codec = ost->st->codec;
    icodec = ist->st->codec;

    if ( metadata_streams_autocopy )
      av_metadata_copy( &ost->st->metadata, ist->st->metadata,
                        AV_METADATA_DONT_OVERWRITE );

    ost->st->disposition = ist->st->disposition;
    codec->bits_per_raw_sample = icodec->bits_per_raw_sample;
    codec->chroma_sample_location = icodec->chroma_sample_location;

    if ( ost->st->stream_copy ) {
      uint64_t extra_size =
          ( uint64_t ) icodec->extradata_size +
          FF_INPUT_BUFFER_PADDING_SIZE;

      if ( extra_size > INT_MAX )
        goto fail;

      /* if stream_copy is selected, no need to decode or encode */
      codec->codec_id = icodec->codec_id;
      codec->codec_type = icodec->codec_type;

      if ( !codec->codec_tag ) {
        if ( !os->oformat->codec_tag
             || av_codec_get_id( os->oformat->codec_tag,
                                 icodec->codec_tag ) == codec->codec_id
             || av_codec_get_tag( os->oformat->codec_tag,
                                  icodec->codec_id ) <= 0 )
          codec->codec_tag = icodec->codec_tag;
      }

      codec->bit_rate = icodec->bit_rate;
      codec->rc_max_rate = icodec->rc_max_rate;
      codec->rc_buffer_size = icodec->rc_buffer_size;
      codec->extradata = av_mallocz( extra_size );
      if ( !codec->extradata )
        goto fail;
      memcpy( codec->extradata, icodec->extradata,
              icodec->extradata_size );
      codec->extradata_size = icodec->extradata_size;
      if ( !copy_tb
           && av_q2d( icodec->time_base ) * icodec->ticks_per_frame >
           av_q2d( ist->st->time_base )
           && av_q2d( ist->st->time_base ) < 1.0 / 1000 ) {
        codec->time_base = icodec->time_base;
        codec->time_base.num *= icodec->ticks_per_frame;
        av_reduce( &codec->time_base.num, &codec->time_base.den,
                   codec->time_base.num, codec->time_base.den, INT_MAX );
      }
      else
        codec->time_base = ist->st->time_base;
      switch ( codec->codec_type ) {
      case AVMEDIA_TYPE_AUDIO:
        codec->channel_layout = icodec->channel_layout;
        codec->sample_rate = icodec->sample_rate;
        codec->channels = icodec->channels;
        codec->frame_size = icodec->frame_size;
        codec->block_align = icodec->block_align;
        if ( codec->block_align == 1 && codec->codec_id == CODEC_ID_MP3 )
          codec->block_align = 0;
        if ( codec->codec_id == CODEC_ID_AC3 )
          codec->block_align = 0;
        break;
      case AVMEDIA_TYPE_VIDEO:
        codec->pix_fmt = icodec->pix_fmt;
        codec->width = icodec->width;
        codec->height = icodec->height;
        codec->has_b_frames = icodec->has_b_frames;
        break;
      default:
        abort(  );
      }
    }
    else {
      switch ( codec->codec_type ) {
      case AVMEDIA_TYPE_AUDIO:
        ost->fifo = av_fifo_alloc( 1024 );
        if ( !ost->fifo )
          goto fail;
        ost->reformat_pair =
            MAKE_SFMT_PAIR( AV_SAMPLE_FMT_NONE, AV_SAMPLE_FMT_NONE );
        ost->audio_resample = codec->sample_rate != icodec->sample_rate
            || audio_sync_method > 1;
        icodec->request_channels = codec->channels;
        ist->decoding_needed = 1;
        ost->encoding_needed = 1;
        ost->resample_sample_fmt = icodec->sample_fmt;
        ost->resample_sample_rate = icodec->sample_rate;
        ost->resample_channels = icodec->channels;
        break;
      case AVMEDIA_TYPE_VIDEO:
        if ( ost->st->codec->pix_fmt == PIX_FMT_NONE ) {
          fprintf( stderr,
                   "Video pixel format is unknown, stream cannot be encoded\n" );
          ffmpeg_exit( 1 );
        }
        ost->video_resample = ( codec->width != icodec->width ||
                                codec->height != icodec->height ||
                                ( codec->pix_fmt != icodec->pix_fmt ) );
        if ( ost->video_resample ) {
#if !CONFIG_AVFILTER
          avcodec_get_frame_defaults( &ost->pict_tmp );
          if ( avpicture_alloc
               ( ( AVPicture * ) & ost->pict_tmp, codec->pix_fmt,
                 codec->width, codec->height ) ) {
            fprintf( stderr,
                     "Cannot allocate temp picture, check pix fmt\n" );
            ffmpeg_exit( 1 );
          }
          sws_flags = av_get_int( sws_opts, "sws_flags", NULL );
          ost->img_resample_ctx = sws_getContext( icodec->width,
                                                  icodec->height,
                                                  icodec->pix_fmt,
                                                  codec->width,
                                                  codec->height,
                                                  codec->pix_fmt,
                                                  sws_flags, NULL, NULL,
                                                  NULL );
          if ( ost->img_resample_ctx == NULL ) {
            fprintf( stderr, "Cannot get resampling context\n" );
            ffmpeg_exit( 1 );
          }

          ost->original_height = icodec->height;
          ost->original_width = icodec->width;
#endif
          codec->bits_per_raw_sample = 0;
        }
        ost->resample_height = icodec->height;
        ost->resample_width = icodec->width;
        ost->resample_pix_fmt = icodec->pix_fmt;
        ost->encoding_needed = 1;
        ist->decoding_needed = 1;

#if CONFIG_AVFILTER
        if ( configure_filters( ist, ost ) ) {
          fprintf( stderr, "Error opening filters!\n" );
          exit( 1 );
        }
#endif
        break;
      case AVMEDIA_TYPE_SUBTITLE:
        ost->encoding_needed = 1;
        ist->decoding_needed = 1;
        break;
      default:
        abort(  );
        break;
      }
      /* two pass mode */
      if ( ost->encoding_needed &&
           ( codec->flags & ( CODEC_FLAG_PASS1 | CODEC_FLAG_PASS2 ) ) ) {
        char logfilename[1024];
        FILE *f;

        snprintf( logfilename, sizeof( logfilename ), "%s-%d.log",
                  pass_logfilename_prefix ? pass_logfilename_prefix :
                  DEFAULT_PASS_LOGFILENAME_PREFIX, i );
        if ( codec->flags & CODEC_FLAG_PASS1 ) {
          f = fopen( logfilename, "wb" );
          if ( !f ) {
            fprintf( stderr,
                     "Cannot write log file '%s' for pass-1 encoding: %s\n",
                     logfilename, strerror( errno ) );
            ffmpeg_exit( 1 );
          }
          ost->logfile = f;
        }
        else {
          char *logbuffer;
          size_t logbuffer_size;
          if ( read_file( logfilename, &logbuffer, &logbuffer_size ) < 0 ) {
            fprintf( stderr,
                     "Error reading log file '%s' for pass-2 encoding\n",
                     logfilename );
            ffmpeg_exit( 1 );
          }
          codec->stats_in = logbuffer;
        }
      }
    }
    if ( codec->codec_type == AVMEDIA_TYPE_VIDEO ) {
      int size = codec->width * codec->height;
      bit_buffer_size = FFMAX( bit_buffer_size, 6 * size + 200 );
    }
  }

  if ( !bit_buffer )
    bit_buffer = av_malloc( bit_buffer_size );
  if ( !bit_buffer ) {
    fprintf( stderr, "Cannot allocate %d bytes output buffer\n",
             bit_buffer_size );
    ret = AVERROR( ENOMEM );
    goto fail;
  }

  /* open each encoder */
  for ( i = 0; i < nb_ostreams; i++ ) {
    ost = ost_table[i];
    if ( ost->encoding_needed ) {
      AVCodec *codec = i < nb_output_codecs ? output_codecs[i] : NULL;
      AVCodecContext *dec = ist_table[ost->source_index]->st->codec;
      if ( !codec )
        codec = avcodec_find_encoder( ost->st->codec->codec_id );
      if ( !codec ) {
        snprintf( error, sizeof( error ),
                  "Encoder (codec id %d) not found for output stream #%d.%d",
                  ost->st->codec->codec_id, ost->file_index, ost->index );
        ret = AVERROR( EINVAL );
        goto dump_format;
      }
      if ( dec->subtitle_header ) {
        ost->st->codec->subtitle_header =
            av_malloc( dec->subtitle_header_size );
        if ( !ost->st->codec->subtitle_header ) {
          ret = AVERROR( ENOMEM );
          goto dump_format;
        }
        memcpy( ost->st->codec->subtitle_header, dec->subtitle_header,
                dec->subtitle_header_size );
        ost->st->codec->subtitle_header_size = dec->subtitle_header_size;
      }
      if ( avcodec_open( ost->st->codec, codec ) < 0 ) {
        snprintf( error, sizeof( error ),
                  "Error while opening encoder for output stream #%d.%d - maybe incorrect parameters such as bit_rate, rate, width or height",
                  ost->file_index, ost->index );
        ret = AVERROR( EINVAL );
        goto dump_format;
      }
      extra_size += ost->st->codec->extradata_size;
    }
  }

  /* open each decoder */
  for ( i = 0; i < nb_istreams; i++ ) {
    ist = ist_table[i];
    if ( ist->decoding_needed ) {
      AVCodec *codec = i < nb_input_codecs ? input_codecs[i] : NULL;
      if ( !codec )
        codec = avcodec_find_decoder( ist->st->codec->codec_id );
      if ( !codec ) {
        snprintf( error, sizeof( error ),
                  "Decoder (codec id %d) not found for input stream #%d.%d",
                  ist->st->codec->codec_id, ist->file_index, ist->index );
        ret = AVERROR( EINVAL );
        goto dump_format;
      }
      if ( avcodec_open( ist->st->codec, codec ) < 0 ) {
        snprintf( error, sizeof( error ),
                  "Error while opening decoder for input stream #%d.%d",
                  ist->file_index, ist->index );
        ret = AVERROR( EINVAL );
        goto dump_format;
      }
      //if (ist->st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
      //    ist->st->codec->flags |= CODEC_FLAG_REPEAT_FIELD;
    }
  }

  /* init pts */
  for ( i = 0; i < nb_istreams; i++ ) {
    AVStream *st;
    ist = ist_table[i];
    st = ist->st;
    ist->pts =
        st->avg_frame_rate.num ? -st->codec->has_b_frames * AV_TIME_BASE /
        av_q2d( st->avg_frame_rate ) : 0;
    ist->next_pts = AV_NOPTS_VALUE;
    init_pts_correction( &ist->pts_ctx );
    ist->is_start = 1;
  }

  /* set meta data information from input file if required */
  for ( i = 0; i < nb_meta_data_maps; i++ ) {
    AVFormatContext *files[2];
    AVMetadata **meta[2];
    int j;

#define METADATA_CHECK_INDEX(index, nb_elems, desc)\
        if ((index) < 0 || (index) >= (nb_elems)) {\
            snprintf(error, sizeof(error), "Invalid %s index %d while processing metadata maps\n",\
                     (desc), (index));\
            ret = AVERROR(EINVAL);\
            goto dump_format;\
        }

    int out_file_index = meta_data_maps[i][0].file;
    int in_file_index = meta_data_maps[i][1].file;
    if ( in_file_index < 0 || out_file_index < 0 )
      continue;
    METADATA_CHECK_INDEX( out_file_index, nb_output_files, "output file" )
        METADATA_CHECK_INDEX( in_file_index, nb_input_files, "input file" )

        files[0] = output_files[out_file_index];
    files[1] = input_files[in_file_index];

    for ( j = 0; j < 2; j++ ) {
      AVMetaDataMap *map = &meta_data_maps[i][j];

      switch ( map->type ) {
      case 'g':
        meta[j] = &files[j]->metadata;
        break;
      case 's':
        METADATA_CHECK_INDEX( map->index, files[j]->nb_streams, "stream" )
            meta[j] = &files[j]->streams[map->index]->metadata;
        break;
      case 'c':
        METADATA_CHECK_INDEX( map->index, files[j]->nb_chapters,
                              "chapter" )
            meta[j] = &files[j]->chapters[map->index]->metadata;
        break;
      case 'p':
        METADATA_CHECK_INDEX( map->index, files[j]->nb_programs,
                              "program" )
            meta[j] = &files[j]->programs[map->index]->metadata;
        break;
      }
    }

    av_metadata_copy( meta[0], *meta[1], AV_METADATA_DONT_OVERWRITE );
  }

  /* copy global metadata by default */
  if ( metadata_global_autocopy ) {

    for ( i = 0; i < nb_output_files; i++ )
      av_metadata_copy( &output_files[i]->metadata,
                        input_files[0]->metadata,
                        AV_METADATA_DONT_OVERWRITE );
  }

  /* copy chapters according to chapter maps */
  for ( i = 0; i < nb_chapter_maps; i++ ) {
    int infile = chapter_maps[i].in_file;
    int outfile = chapter_maps[i].out_file;

    if ( infile < 0 || outfile < 0 )
      continue;
    if ( infile >= nb_input_files ) {
      snprintf( error, sizeof( error ),
                "Invalid input file index %d in chapter mapping.\n",
                infile );
      ret = AVERROR( EINVAL );
      goto dump_format;
    }
    if ( outfile >= nb_output_files ) {
      snprintf( error, sizeof( error ),
                "Invalid output file index %d in chapter mapping.\n",
                outfile );
      ret = AVERROR( EINVAL );
      goto dump_format;
    }
    copy_chapters( infile, outfile );
  }

  /* copy chapters from the first input file that has them */
  if ( !nb_chapter_maps )
    for ( i = 0; i < nb_input_files; i++ ) {
      if ( !input_files[i]->nb_chapters )
        continue;

      for ( j = 0; j < nb_output_files; j++ )
        if ( ( ret = copy_chapters( i, j ) ) < 0 )
          goto dump_format;
      break;
    }

  /* open files and write file headers */
  for ( i = 0; i < nb_output_files; i++ ) {
    os = output_files[i];
    if ( av_write_header( os ) < 0 ) {
      snprintf( error, sizeof( error ),
                "Could not write header for output file #%d (incorrect codec parameters ?)",
                i );
      ret = AVERROR( EINVAL );
      goto dump_format;
    }
    if ( strcmp( output_files[i]->oformat->name, "rtp" ) ) {
      want_sdp = 0;
    }
  }

dump_format:
  /* dump the file output parameters - cannot be done before in case
     of stream copy */
  for ( i = 0; i < nb_output_files; i++ ) {
    dump_format( output_files[i], i, output_files[i]->filename, 1 );
  }

  /* dump the stream mapping */
  if ( verbose >= 0 ) {
    fprintf( stderr, "Stream mapping:\n" );
    for ( i = 0; i < nb_ostreams; i++ ) {
      ost = ost_table[i];
      fprintf( stderr, "  Stream #%d.%d -> #%d.%d",
               ist_table[ost->source_index]->file_index,
               ist_table[ost->source_index]->index,
               ost->file_index, ost->index );
      if ( ost->sync_ist != ist_table[ost->source_index] )
        fprintf( stderr, " [sync #%d.%d]",
                 ost->sync_ist->file_index, ost->sync_ist->index );
      fprintf( stderr, "\n" );
    }
  }

  if ( ret ) {
    fprintf( stderr, "%s\n", error );
    goto fail;
  }

  if ( want_sdp ) {
    print_sdp( output_files, nb_output_files );
  }

  if ( !using_stdin && verbose >= 0 ) {
    fprintf( stderr, "Press [q] to stop encoding\n" );
    url_set_interrupt_cb( decode_interrupt_cb );
  }
  term_init(  );

  timer_start = av_gettime(  );

  for ( ; received_sigterm == 0; ) {
    int file_index, ist_index;
    AVPacket pkt;
    double ipts_min;
    double opts_min;

  redo:
    ipts_min = 1e100;
    opts_min = 1e100;
    /* if 'q' pressed, exits */
    if ( !using_stdin ) {
      if ( q_pressed )
        break;
      /* read_key() returns 0 on EOF */
      key = read_key(  );
      if ( key == 'q' )
        break;
    }

    /* select the stream that we must read now by looking at the
       smallest output pts */
    file_index = -1;
    for ( i = 0; i < nb_ostreams; i++ ) {
      double ipts, opts;
      ost = ost_table[i];
      os = output_files[ost->file_index];
      ist = ist_table[ost->source_index];
      if ( ist->is_past_recording_time || no_packet[ist->file_index] )
        continue;
      opts = ost->st->pts.val * av_q2d( ost->st->time_base );
      ipts = ( double ) ist->pts;
      if ( !file_table[ist->file_index].eof_reached ) {
        if ( ipts < ipts_min ) {
          ipts_min = ipts;
          if ( input_sync )
            file_index = ist->file_index;
        }
        if ( opts < opts_min ) {
          opts_min = opts;
          if ( !input_sync )
            file_index = ist->file_index;
        }
      }
      if ( ost->frame_number >= max_frames[ost->st->codec->codec_type] ) {
        file_index = -1;
        break;
      }
    }
    /* if none, if is finished */
    if ( file_index < 0 ) {
      if ( no_packet_count ) {
        no_packet_count = 0;
        memset( no_packet, 0, sizeof( no_packet ) );
        usleep( 10000 );
        continue;
      }
      break;
    }

    /* finish if limit size exhausted */
    if ( limit_filesize != 0
         && limit_filesize <= url_ftell( output_files[0]->pb ) )
      break;

    /* read a frame from it and output it in the fifo */
    is = input_files[file_index];
    ret = av_read_frame( is, &pkt );
    if ( ret == AVERROR( EAGAIN ) ) {
      no_packet[file_index] = 1;
      no_packet_count++;
      continue;
    }
    if ( ret < 0 ) {
      file_table[file_index].eof_reached = 1;
      if ( opt_shortest )
        break;
      else
        continue;
    }

    no_packet_count = 0;
    memset( no_packet, 0, sizeof( no_packet ) );

    if ( do_pkt_dump ) {
      av_pkt_dump_log( NULL, AV_LOG_DEBUG, &pkt, do_hex_dump );
    }
    /* the following test is needed in case new streams appear
       dynamically in stream : we ignore them */
    if ( pkt.stream_index >= file_table[file_index].nb_streams )
      goto discard_packet;
    ist_index = file_table[file_index].ist_index + pkt.stream_index;
    ist = ist_table[ist_index];
    if ( ist->discard )
      goto discard_packet;

    if ( pkt.dts != AV_NOPTS_VALUE )
      pkt.dts +=
          av_rescale_q( input_files_ts_offset[ist->file_index],
                        AV_TIME_BASE_Q, ist->st->time_base );
    if ( pkt.pts != AV_NOPTS_VALUE )
      pkt.pts +=
          av_rescale_q( input_files_ts_offset[ist->file_index],
                        AV_TIME_BASE_Q, ist->st->time_base );

    if ( pkt.stream_index < nb_input_files_ts_scale[file_index]
         && input_files_ts_scale[file_index][pkt.stream_index] ) {
      if ( pkt.pts != AV_NOPTS_VALUE )
        pkt.pts *= input_files_ts_scale[file_index][pkt.stream_index];
      if ( pkt.dts != AV_NOPTS_VALUE )
        pkt.dts *= input_files_ts_scale[file_index][pkt.stream_index];
    }

//        fprintf(stderr, "next:%"PRId64" dts:%"PRId64" off:%"PRId64" %d\n", ist->next_pts, pkt.dts, input_files_ts_offset[ist->file_index], ist->st->codec->codec_type);
    if ( pkt.dts != AV_NOPTS_VALUE && ist->next_pts != AV_NOPTS_VALUE
         && ( is->iformat->flags & AVFMT_TS_DISCONT ) ) {
      int64_t pkt_dts =
          av_rescale_q( pkt.dts, ist->st->time_base, AV_TIME_BASE_Q );
      int64_t delta = pkt_dts - ist->next_pts;
      if ( ( FFABS( delta ) > 1LL * dts_delta_threshold * AV_TIME_BASE
             || pkt_dts + 1 < ist->pts ) && !copy_ts ) {
        input_files_ts_offset[ist->file_index] -= delta;
        if ( verbose > 2 )
          fprintf( stderr,
                   "timestamp discontinuity %" PRId64 ", new offset= %"
                   PRId64 "\n", delta,
                   input_files_ts_offset[ist->file_index] );
        pkt.dts -=
            av_rescale_q( delta, AV_TIME_BASE_Q, ist->st->time_base );
        if ( pkt.pts != AV_NOPTS_VALUE )
          pkt.pts -=
              av_rescale_q( delta, AV_TIME_BASE_Q, ist->st->time_base );
      }
    }

    /* finish if recording time exhausted */
    if ( recording_time != INT64_MAX &&
         av_compare_ts( pkt.pts, ist->st->time_base,
                        recording_time + start_time, ( AVRational ) {
                        1, 1000000}
          ) >= 0 ) {
      ist->is_past_recording_time = 1;
      goto discard_packet;
    }

    //fprintf(stderr,"read #%d.%d size=%d\n", ist->file_index, ist->index, pkt.size);
    if ( output_packet( ist, ist_index, ost_table, nb_ostreams, &pkt ) <
         0 ) {

      if ( verbose >= 0 )
        fprintf( stderr, "Error while decoding stream #%d.%d\n",
                 ist->file_index, ist->index );
      if ( exit_on_error )
        ffmpeg_exit( 1 );
      av_free_packet( &pkt );
      goto redo;
    }

  discard_packet:
    av_free_packet( &pkt );

    /* dump report by using the output first video and audio streams */
    print_report( output_files, ost_table, nb_ostreams, 0 );
  }

  /* at the end of stream, we must flush the decoder buffers */
  for ( i = 0; i < nb_istreams; i++ ) {
    ist = ist_table[i];
    if ( ist->decoding_needed ) {
      output_packet( ist, i, ost_table, nb_ostreams, NULL );
    }
  }

  term_exit(  );

  /* write the trailer if needed and close file */
  for ( i = 0; i < nb_output_files; i++ ) {
    os = output_files[i];
    av_write_trailer( os );
  }

  /* dump report by using the first video and audio streams */
  print_report( output_files, ost_table, nb_ostreams, 1 );

  /* close each encoder */
  for ( i = 0; i < nb_ostreams; i++ ) {
    ost = ost_table[i];
    if ( ost->encoding_needed ) {
      av_freep( &ost->st->codec->stats_in );
      avcodec_close( ost->st->codec );
    }
  }

  /* close each decoder */
  for ( i = 0; i < nb_istreams; i++ ) {
    ist = ist_table[i];
    if ( ist->decoding_needed ) {
      avcodec_close( ist->st->codec );
    }
  }
#if CONFIG_AVFILTER
  if ( graph ) {
    avfilter_graph_free( graph );
    av_freep( &graph );
  }
#endif

  /* finished ! */
  ret = 0;

fail:
  av_freep( &bit_buffer );
  av_free( file_table );

  if ( ist_table ) {
    for ( i = 0; i < nb_istreams; i++ ) {
      ist = ist_table[i];
      av_free( ist );
    }
    av_free( ist_table );
  }
  if ( ost_table ) {
    for ( i = 0; i < nb_ostreams; i++ ) {
      ost = ost_table[i];
      if ( ost ) {
        if ( ost->st->stream_copy )
          av_freep( &ost->st->codec->extradata );
        if ( ost->logfile ) {
          fclose( ost->logfile );
          ost->logfile = NULL;
        }
        av_fifo_free( ost->fifo );      /* works even if fifo is not
                                           initialized but set to zero */
        av_freep( &ost->st->codec->subtitle_header );
        av_free( ost->pict_tmp.data[0] );
        av_free( ost->forced_kf_pts );
        if ( ost->video_resample )
          sws_freeContext( ost->img_resample_ctx );
        if ( ost->resample )
          audio_resample_close( ost->resample );
        if ( ost->reformat_ctx )
          av_audio_convert_free( ost->reformat_ctx );
        av_free( ost );
      }
    }
    av_free( ost_table );
  }
  return ret;
}

static void
opt_codec( int *pstream_copy, char **pcodec_name,
           int codec_type, const char *arg ) {
  av_freep( pcodec_name );
  if ( !strcmp( arg, "copy" ) ) {
    *pstream_copy = 1;
  }
  else {
    *pcodec_name = av_strdup( arg );
  }
}

static void
opt_audio_codec( const char *arg ) {
  opt_codec( &audio_stream_copy, &audio_codec_name, AVMEDIA_TYPE_AUDIO,
             arg );
}

static void
opt_video_codec( const char *arg ) {
  opt_codec( &video_stream_copy, &video_codec_name, AVMEDIA_TYPE_VIDEO,
             arg );
}

static enum CodecID
find_codec_or_die( const char *name, int type, int encoder, int strict ) {
  const char *codec_string = encoder ? "encoder" : "decoder";
  AVCodec *codec;

  if ( !name )
    return CODEC_ID_NONE;
  codec = encoder ?
      avcodec_find_encoder_by_name( name ) :
      avcodec_find_decoder_by_name( name );
  if ( !codec ) {
    fprintf( stderr, "Unknown %s '%s'\n", codec_string, name );
    ffmpeg_exit( 1 );
  }
  if ( codec->type != type ) {
    fprintf( stderr, "Invalid %s type '%s'\n", codec_string, name );
    ffmpeg_exit( 1 );
  }
  if ( codec->capabilities & CODEC_CAP_EXPERIMENTAL &&
       strict > FF_COMPLIANCE_EXPERIMENTAL ) {
    fprintf( stderr, "%s '%s' is experimental and might produce bad "
             "results.\nAdd '-strict experimental' if you want to use it.\n",
             codec_string, codec->name );
    codec = encoder ?
        avcodec_find_encoder( codec->id ) :
        avcodec_find_decoder( codec->id );
    if ( !( codec->capabilities & CODEC_CAP_EXPERIMENTAL ) )
      fprintf( stderr, "Or use the non experimental %s '%s'.\n",
               codec_string, codec->name );
    ffmpeg_exit( 1 );
  }
  return codec->id;
}

static void
opt_input_file( const char *filename ) {
  AVFormatContext *ic;
  AVFormatParameters params, *ap = &params;
  AVInputFormat *file_iformat = NULL;
  int err, i, ret, rfps, rfps_base;
  int64_t timestamp;

  if ( last_asked_format ) {
    if ( !( file_iformat = av_find_input_format( last_asked_format ) ) ) {
      fprintf( stderr, "Unknown input format: '%s'\n", last_asked_format );
      ffmpeg_exit( 1 );
    }
    last_asked_format = NULL;
  }

  if ( !strcmp( filename, "-" ) )
    filename = "pipe:";

  using_stdin |= !strncmp( filename, "pipe:", 5 ) ||
      !strcmp( filename, "/dev/stdin" );

  /* get default parameters from command line */
  ic = avformat_alloc_context(  );
  if ( !ic ) {
    print_error( filename, AVERROR( ENOMEM ) );
    ffmpeg_exit( 1 );
  }

  memset( ap, 0, sizeof( *ap ) );
  ap->prealloced_context = 1;
  ap->sample_rate = audio_sample_rate;
  ap->channels = audio_channels;
  ap->time_base.den = frame_rate.num;
  ap->time_base.num = frame_rate.den;
  ap->width = frame_width;
  ap->height = frame_height;
  ap->pix_fmt = frame_pix_fmt;
  // ap->sample_fmt = audio_sample_fmt; //FIXME:not implemented in libavformat
  ap->channel = video_channel;
  ap->standard = video_standard;

  set_context_opts( ic, avformat_opts, AV_OPT_FLAG_DECODING_PARAM, NULL );

  ic->video_codec_id =
      find_codec_or_die( video_codec_name, AVMEDIA_TYPE_VIDEO, 0,
                         avcodec_opts[AVMEDIA_TYPE_VIDEO]->
                         strict_std_compliance );
  ic->audio_codec_id =
      find_codec_or_die( audio_codec_name, AVMEDIA_TYPE_AUDIO, 0,
                         avcodec_opts[AVMEDIA_TYPE_AUDIO]->
                         strict_std_compliance );
  ic->subtitle_codec_id =
      find_codec_or_die( subtitle_codec_name, AVMEDIA_TYPE_SUBTITLE, 0,
                         avcodec_opts[AVMEDIA_TYPE_SUBTITLE]->
                         strict_std_compliance );
  ic->flags |= AVFMT_FLAG_NONBLOCK;

  /* open the input file with generic libav function */
  err = av_open_input_file( &ic, filename, file_iformat, 0, ap );
  if ( err < 0 ) {
    print_error( filename, err );
    ffmpeg_exit( 1 );
  }
  if ( opt_programid ) {
    int i, j;
    int found = 0;
    for ( i = 0; i < ic->nb_streams; i++ ) {
      ic->streams[i]->discard = AVDISCARD_ALL;
    }
    for ( i = 0; i < ic->nb_programs; i++ ) {
      AVProgram *p = ic->programs[i];
      if ( p->id != opt_programid ) {
        p->discard = AVDISCARD_ALL;
      }
      else {
        found = 1;
        for ( j = 0; j < p->nb_stream_indexes; j++ ) {
          ic->streams[p->stream_index[j]]->discard = AVDISCARD_DEFAULT;
        }
      }
    }
    if ( !found ) {
      fprintf( stderr, "Specified program id not found\n" );
      ffmpeg_exit( 1 );
    }
    opt_programid = 0;
  }

  ic->loop_input = loop_input;

  /* If not enough info to get the stream parameters, we decode the
     first frames to get it. (used in mpeg case for example) */
  ret = av_find_stream_info( ic );
  if ( ret < 0 && verbose >= 0 ) {
    fprintf( stderr, "%s: could not find codec parameters\n", filename );
    av_close_input_file( ic );
    ffmpeg_exit( 1 );
  }

  timestamp = start_time;
  /* add the stream start time */
  if ( ic->start_time != AV_NOPTS_VALUE )
    timestamp += ic->start_time;

  /* if seeking requested, we execute it */
  if ( start_time != 0 ) {
    ret = av_seek_frame( ic, -1, timestamp, AVSEEK_FLAG_BACKWARD );
    if ( ret < 0 ) {
      fprintf( stderr, "%s: could not seek to position %0.3f\n",
               filename, ( double ) timestamp / AV_TIME_BASE );
    }
    /* reset seek info */
    start_time = 0;
  }

  /* update the current parameters so that they match the one of the input stream */
  for ( i = 0; i < ic->nb_streams; i++ ) {
    AVStream *st = ic->streams[i];
    AVCodecContext *dec = st->codec;
    avcodec_thread_init( dec, thread_count );
    input_codecs =
        grow_array( input_codecs, sizeof( *input_codecs ),
                    &nb_input_codecs, nb_input_codecs + 1 );
    switch ( dec->codec_type ) {
    case AVMEDIA_TYPE_AUDIO:
      input_codecs[nb_input_codecs - 1] =
          avcodec_find_decoder_by_name( audio_codec_name );
      set_context_opts( dec, avcodec_opts[AVMEDIA_TYPE_AUDIO],
                        AV_OPT_FLAG_AUDIO_PARAM |
                        AV_OPT_FLAG_DECODING_PARAM,
                        input_codecs[nb_input_codecs - 1] );
      //fprintf(stderr, "\nInput Audio channels: %d", dec->channels);
      channel_layout = dec->channel_layout;
      audio_channels = dec->channels;
      audio_sample_rate = dec->sample_rate;
      audio_sample_fmt = dec->sample_fmt;
      if ( audio_disable )
        st->discard = AVDISCARD_ALL;
      /* Note that av_find_stream_info can add more streams, and we
       * currently have no chance of setting up lowres decoding
       * early enough for them. */
      if ( dec->lowres )
        audio_sample_rate >>= dec->lowres;
      break;
    case AVMEDIA_TYPE_VIDEO:
      input_codecs[nb_input_codecs - 1] =
          avcodec_find_decoder_by_name( video_codec_name );
      set_context_opts( dec, avcodec_opts[AVMEDIA_TYPE_VIDEO],
                        AV_OPT_FLAG_VIDEO_PARAM |
                        AV_OPT_FLAG_DECODING_PARAM,
                        input_codecs[nb_input_codecs - 1] );
      frame_height = dec->height;
      frame_width = dec->width;
      if ( ic->streams[i]->sample_aspect_ratio.num )
        frame_aspect_ratio = av_q2d( ic->streams[i]->sample_aspect_ratio );
      else
        frame_aspect_ratio = av_q2d( dec->sample_aspect_ratio );
      frame_aspect_ratio *= ( float ) dec->width / dec->height;
      frame_pix_fmt = dec->pix_fmt;
      rfps = ic->streams[i]->r_frame_rate.num;
      rfps_base = ic->streams[i]->r_frame_rate.den;
      if ( dec->lowres ) {
        dec->flags |= CODEC_FLAG_EMU_EDGE;
        frame_height >>= dec->lowres;
        frame_width >>= dec->lowres;
        dec->height = frame_height;
        dec->width = frame_width;
      }
      if ( me_threshold )
        dec->debug |= FF_DEBUG_MV;

      if ( dec->time_base.den != rfps * dec->ticks_per_frame
           || dec->time_base.num != rfps_base ) {

        if ( verbose >= 0 )
          fprintf( stderr,
                   "\nSeems stream %d codec frame rate differs from container frame rate: %2.2f (%d/%d) -> %2.2f (%d/%d)\n",
                   i, ( float ) dec->time_base.den / dec->time_base.num,
                   dec->time_base.den, dec->time_base.num,
                   ( float ) rfps / rfps_base, rfps, rfps_base );
      }
      /* update the current frame rate to match the stream frame rate */
      frame_rate.num = rfps;
      frame_rate.den = rfps_base;

      if ( video_disable )
        st->discard = AVDISCARD_ALL;
      else if ( video_discard )
        st->discard = video_discard;
      break;
    case AVMEDIA_TYPE_DATA:
      break;
    case AVMEDIA_TYPE_SUBTITLE:
      input_codecs[nb_input_codecs - 1] =
          avcodec_find_decoder_by_name( subtitle_codec_name );
      if ( subtitle_disable )
        st->discard = AVDISCARD_ALL;
      break;
    case AVMEDIA_TYPE_ATTACHMENT:
    case AVMEDIA_TYPE_UNKNOWN:
      break;
    default:
      abort(  );
    }
  }

  input_files[nb_input_files] = ic;
  input_files_ts_offset[nb_input_files] =
      input_ts_offset - ( copy_ts ? 0 : timestamp );
  /* dump the file content */
  if ( verbose >= 0 )
    dump_format( ic, nb_input_files, filename, 0 );

  nb_input_files++;

  video_channel = 0;

  av_freep( &video_codec_name );
  av_freep( &audio_codec_name );
  av_freep( &subtitle_codec_name );
}

static void
check_audio_video_sub_inputs( int *has_video_ptr, int *has_audio_ptr,
                              int *has_subtitle_ptr ) {
  int has_video, has_audio, has_subtitle, i, j;
  AVFormatContext *ic;

  has_video = 0;
  has_audio = 0;
  has_subtitle = 0;
  for ( j = 0; j < nb_input_files; j++ ) {
    ic = input_files[j];
    for ( i = 0; i < ic->nb_streams; i++ ) {
      AVCodecContext *enc = ic->streams[i]->codec;
      switch ( enc->codec_type ) {
      case AVMEDIA_TYPE_AUDIO:
        has_audio = 1;
        break;
      case AVMEDIA_TYPE_VIDEO:
        has_video = 1;
        break;
      case AVMEDIA_TYPE_SUBTITLE:
        has_subtitle = 1;
        break;
      case AVMEDIA_TYPE_DATA:
      case AVMEDIA_TYPE_ATTACHMENT:
      case AVMEDIA_TYPE_UNKNOWN:
        break;
      default:
        abort(  );
      }
    }
  }
  *has_video_ptr = has_video;
  *has_audio_ptr = has_audio;
  *has_subtitle_ptr = has_subtitle;
}

static void
new_video_stream( AVFormatContext * oc, int file_idx ) {
  AVStream *st;
  AVOutputStream *ost;
  AVCodecContext *video_enc;
  enum CodecID codec_id = CODEC_ID_NONE;
  AVCodec *codec = NULL;

  st = av_new_stream( oc,
                      oc->nb_streams <
                      nb_streamid_map ? streamid_map[oc->nb_streams] : 0 );
  if ( !st ) {
    fprintf( stderr, "Could not alloc stream\n" );
    ffmpeg_exit( 1 );
  }
  ost = new_output_stream( oc, file_idx );

  output_codecs =
      grow_array( output_codecs, sizeof( *output_codecs ),
                  &nb_output_codecs, nb_output_codecs + 1 );
  if ( !video_stream_copy ) {
    if ( video_codec_name ) {
      codec_id =
          find_codec_or_die( video_codec_name, AVMEDIA_TYPE_VIDEO, 1,
                             avcodec_opts[AVMEDIA_TYPE_VIDEO]->
                             strict_std_compliance );
      codec = avcodec_find_encoder_by_name( video_codec_name );
      output_codecs[nb_output_codecs - 1] = codec;
    }
    else {
      codec_id =
          av_guess_codec( oc->oformat, NULL, oc->filename, NULL,
                          AVMEDIA_TYPE_VIDEO );
      codec = avcodec_find_encoder( codec_id );
    }
  }

  avcodec_get_context_defaults3( st->codec, codec );
  ost->bitstream_filters = video_bitstream_filters;
  video_bitstream_filters = NULL;

  avcodec_thread_init( st->codec, thread_count );

  video_enc = st->codec;

  if ( video_codec_tag )
    video_enc->codec_tag = video_codec_tag;

  if ( ( video_global_header & 1 )
       || ( video_global_header == 0
            && ( oc->oformat->flags & AVFMT_GLOBALHEADER ) ) ) {
    video_enc->flags |= CODEC_FLAG_GLOBAL_HEADER;
    avcodec_opts[AVMEDIA_TYPE_VIDEO]->flags |= CODEC_FLAG_GLOBAL_HEADER;
  }
  if ( video_global_header & 2 ) {
    video_enc->flags2 |= CODEC_FLAG2_LOCAL_HEADER;
    avcodec_opts[AVMEDIA_TYPE_VIDEO]->flags2 |= CODEC_FLAG2_LOCAL_HEADER;
  }

  if ( video_stream_copy ) {
    st->stream_copy = 1;
    video_enc->codec_type = AVMEDIA_TYPE_VIDEO;
    video_enc->sample_aspect_ratio =
        st->sample_aspect_ratio =
        av_d2q( frame_aspect_ratio * frame_height / frame_width, 255 );
  }
  else {
    const char *p;
    int i;
    AVRational fps =
        frame_rate.num ? frame_rate : ( AVRational ) { 25, 1 };

    video_enc->codec_id = codec_id;
    set_context_opts( video_enc, avcodec_opts[AVMEDIA_TYPE_VIDEO],
                      AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM,
                      codec );

    if ( codec && codec->supported_framerates && !force_fps )
      fps =
          codec->
          supported_framerates[av_find_nearest_q_idx
                               ( fps, codec->supported_framerates )];
    video_enc->time_base.den = fps.num;
    video_enc->time_base.num = fps.den;

    video_enc->width = frame_width;
    video_enc->height = frame_height;
    video_enc->sample_aspect_ratio =
        av_d2q( frame_aspect_ratio * video_enc->height / video_enc->width,
                255 );
    video_enc->pix_fmt = frame_pix_fmt;
    st->sample_aspect_ratio = video_enc->sample_aspect_ratio;

    choose_pixel_fmt( st, codec );

    if ( intra_only )
      video_enc->gop_size = 0;
    if ( video_qscale || same_quality ) {
      video_enc->flags |= CODEC_FLAG_QSCALE;
      video_enc->global_quality =
          st->quality = FF_QP2LAMBDA * video_qscale;
    }

    if ( intra_matrix )
      video_enc->intra_matrix = intra_matrix;
    if ( inter_matrix )
      video_enc->inter_matrix = inter_matrix;

    p = video_rc_override_string;
    for ( i = 0; p; i++ ) {
      int start, end, q;
      int e = sscanf( p, "%d,%d,%d", &start, &end, &q );
      if ( e != 3 ) {
        fprintf( stderr, "error parsing rc_override\n" );
        ffmpeg_exit( 1 );
      }
      video_enc->rc_override =
          av_realloc( video_enc->rc_override,
                      sizeof( RcOverride ) * ( i + 1 ) );
      video_enc->rc_override[i].start_frame = start;
      video_enc->rc_override[i].end_frame = end;
      if ( q > 0 ) {
        video_enc->rc_override[i].qscale = q;
        video_enc->rc_override[i].quality_factor = 1.0;
      }
      else {
        video_enc->rc_override[i].qscale = 0;
        video_enc->rc_override[i].quality_factor = -q / 100.0;
      }
      p = strchr( p, '/' );
      if ( p )
        p++;
    }
    video_enc->rc_override_count = i;
    if ( !video_enc->rc_initial_buffer_occupancy )
      video_enc->rc_initial_buffer_occupancy =
          video_enc->rc_buffer_size * 3 / 4;
    video_enc->me_threshold = me_threshold;
    video_enc->intra_dc_precision = intra_dc_precision - 8;

    if ( do_psnr )
      video_enc->flags |= CODEC_FLAG_PSNR;

    /* two pass mode */
    if ( do_pass ) {
      if ( do_pass == 1 ) {
        video_enc->flags |= CODEC_FLAG_PASS1;
      }
      else {
        video_enc->flags |= CODEC_FLAG_PASS2;
      }
    }

    if ( forced_key_frames )
      parse_forced_key_frames( forced_key_frames, ost, video_enc );
  }
  if ( video_language ) {
    av_metadata_set2( &st->metadata, "language", video_language, 0 );
    av_freep( &video_language );
  }

  /* reset some key parameters */
  video_disable = 0;
  av_freep( &video_codec_name );
  av_freep( &forced_key_frames );
  video_stream_copy = 0;
  frame_pix_fmt = PIX_FMT_NONE;
}

static void
new_audio_stream( AVFormatContext * oc, int file_idx ) {
  AVStream *st;
  AVOutputStream *ost;
  AVCodec *codec = NULL;
  AVCodecContext *audio_enc;
  enum CodecID codec_id = CODEC_ID_NONE;

  st = av_new_stream( oc,
                      oc->nb_streams <
                      nb_streamid_map ? streamid_map[oc->nb_streams] : 0 );
  if ( !st ) {
    fprintf( stderr, "Could not alloc stream\n" );
    ffmpeg_exit( 1 );
  }
  ost = new_output_stream( oc, file_idx );

  output_codecs =
      grow_array( output_codecs, sizeof( *output_codecs ),
                  &nb_output_codecs, nb_output_codecs + 1 );

  avcodec_get_context_defaults3( st->codec, codec );

  ost->bitstream_filters = audio_bitstream_filters;
  audio_bitstream_filters = NULL;

  avcodec_thread_init( st->codec, thread_count );

  audio_enc = st->codec;
  audio_enc->codec_type = AVMEDIA_TYPE_AUDIO;

  if ( audio_codec_tag )
    audio_enc->codec_tag = audio_codec_tag;

  if ( oc->oformat->flags & AVFMT_GLOBALHEADER ) {
    audio_enc->flags |= CODEC_FLAG_GLOBAL_HEADER;
    avcodec_opts[AVMEDIA_TYPE_AUDIO]->flags |= CODEC_FLAG_GLOBAL_HEADER;
  }
  st->stream_copy = 1;
  audio_enc->channels = audio_channels;
  audio_enc->sample_rate = audio_sample_rate;
  audio_enc->time_base = ( AVRational ) {
  1, audio_sample_rate};

  /* reset some key parameters */
  audio_disable = 0;
  av_freep( &audio_codec_name );
  audio_stream_copy = 0;
}

static void
opt_output_file( const char *filename ) {
  AVFormatContext *oc;
  int err, use_video, use_audio, use_subtitle;
  int input_has_video, input_has_audio, input_has_subtitle;
  AVFormatParameters params, *ap = &params;
  AVOutputFormat *file_oformat;

  if ( !strcmp( filename, "-" ) )
    filename = "pipe:";

  oc = avformat_alloc_context(  );
  if ( !oc ) {
    print_error( filename, AVERROR( ENOMEM ) );
    ffmpeg_exit( 1 );
  }

  if ( last_asked_format ) {
    file_oformat = av_guess_format( last_asked_format, NULL, NULL );
    if ( !file_oformat ) {
      fprintf( stderr,
               "Requested output format '%s' is not a suitable output format\n",
               last_asked_format );
      ffmpeg_exit( 1 );
    }
    last_asked_format = NULL;
  }
  else {
    file_oformat = av_guess_format( NULL, filename, NULL );
    if ( !file_oformat ) {
      fprintf( stderr,
               "Unable to find a suitable output format for '%s'\n",
               filename );
      ffmpeg_exit( 1 );
    }
  }

  oc->oformat = file_oformat;
  av_strlcpy( oc->filename, filename, sizeof( oc->filename ) );

  if ( !strcmp( file_oformat->name, "ffm" ) &&
       av_strstart( filename, "http:", NULL ) ) {
    /* special case for files sent to ffserver: we get the stream
       parameters from ffserver */
    int err = read_ffserver_streams( oc, filename );
    if ( err < 0 ) {
      print_error( filename, err );
      ffmpeg_exit( 1 );
    }
  }
  else {
    use_video = file_oformat->video_codec != CODEC_ID_NONE
        || video_stream_copy || video_codec_name;
    use_audio = file_oformat->audio_codec != CODEC_ID_NONE
        || audio_stream_copy || audio_codec_name;
    use_subtitle = file_oformat->subtitle_codec != CODEC_ID_NONE
        || subtitle_stream_copy || subtitle_codec_name;

    /* disable if no corresponding type found and at least one
       input file */
    if ( nb_input_files > 0 ) {
      check_audio_video_sub_inputs( &input_has_video, &input_has_audio,
                                    &input_has_subtitle );
      if ( !input_has_video )
        use_video = 0;
      if ( !input_has_audio )
        use_audio = 0;
      if ( !input_has_subtitle )
        use_subtitle = 0;
    }

    /* manual disable */
    if ( audio_disable )
      use_audio = 0;
    if ( video_disable )
      use_video = 0;
    if ( subtitle_disable )
      use_subtitle = 0;

    if ( use_video )
      new_video_stream( oc, nb_output_files );
    if ( use_audio )
      new_audio_stream( oc, nb_output_files );
    if ( use_subtitle )
      new_subtitle_stream( oc, nb_output_files );

    oc->timestamp = recording_timestamp;

    av_metadata_copy( &oc->metadata, metadata, 0 );
    av_metadata_free( &metadata );
  }

  output_files[nb_output_files++] = oc;

  /* check filename in case of an image number is expected */
  if ( oc->oformat->flags & AVFMT_NEEDNUMBER ) {
    if ( !av_filename_number_test( oc->filename ) ) {
      print_error( oc->filename, AVERROR_NUMEXPECTED );
      ffmpeg_exit( 1 );
    }
  }

  if ( !( oc->oformat->flags & AVFMT_NOFILE ) ) {

    /* open the file */
    if ( ( err = url_fopen( &oc->pb, filename, URL_WRONLY ) ) < 0 ) {
      print_error( filename, err );
      ffmpeg_exit( 1 );
    }
  }

  memset( ap, 0, sizeof( *ap ) );
  if ( av_set_parameters( oc, ap ) < 0 ) {
    fprintf( stderr, "%s: Invalid encoding parameters\n", oc->filename );
    ffmpeg_exit( 1 );
  }

  oc->preload = ( int ) ( mux_preload * AV_TIME_BASE );
  oc->max_delay = ( int ) ( mux_max_delay * AV_TIME_BASE );
  oc->loop_output = loop_output;
  oc->flags |= AVFMT_FLAG_NONBLOCK;

  set_context_opts( oc, avformat_opts, AV_OPT_FLAG_ENCODING_PARAM, NULL );

  nb_streamid_map = 0;
  av_freep( &forced_key_frames );
}

static int64_t
getutime( void ) {
#if HAVE_GETRUSAGE
  struct rusage rusage;

  getrusage( RUSAGE_SELF, &rusage );
  return ( rusage.ru_utime.tv_sec * 1000000LL ) + rusage.ru_utime.tv_usec;
#elif HAVE_GETPROCESSTIMES
  HANDLE proc;
  FILETIME c, e, k, u;
  proc = GetCurrentProcess(  );
  GetProcessTimes( proc, &c, &e, &k, &u );
  return ( ( int64_t ) u.dwHighDateTime << 32 | u.dwLowDateTime ) / 10;
#else
  return av_gettime(  );
#endif
}

int
main( int argc, char **argv ) {
  int64_t ti;

  av_log_set_flags( AV_LOG_SKIP_REPEATED );

  avcodec_register_all(  );
#if CONFIG_AVDEVICE
  avdevice_register_all(  );
#endif
#if CONFIG_AVFILTER
  avfilter_register_all(  );
#endif
  av_register_all(  );

#if HAVE_ISATTY
  if ( isatty( STDIN_FILENO ) )
    url_set_interrupt_cb( decode_interrupt_cb );
#endif

  init_opts(  );

  show_banner(  );

  /* parse options */
  parse_options( argc, argv, options, opt_output_file );

  if ( nb_output_files <= 0 && nb_input_files == 0 ) {
    show_usage(  );
    fprintf( stderr,
             "Use -h to get full help or, even better, run 'man ffmpeg'\n" );
    ffmpeg_exit( 1 );
  }

  /* file converter / grab */
  if ( nb_output_files <= 0 ) {
    fprintf( stderr, "At least one output file must be specified\n" );
    ffmpeg_exit( 1 );
  }

  if ( nb_input_files == 0 ) {
    fprintf( stderr, "At least one input file must be specified\n" );
    ffmpeg_exit( 1 );
  }

  ti = getutime(  );
  if ( transcode
       ( output_files, nb_output_files, input_files, nb_input_files,
         stream_maps, nb_stream_maps ) < 0 )
    ffmpeg_exit( 1 );
  ti = getutime(  ) - ti;
  return ffmpeg_exit( 0 );
}
