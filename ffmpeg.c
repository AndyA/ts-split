        -:    0:Source:ffmpeg.c
        -:    0:Graph:ffmpeg.gcno
        -:    0:Data:ffmpeg.gcda
        -:    0:Runs:1
        -:    0:Programs:1
        -:    1:/*
        -:    2: * FFmpeg main
        -:    3: * Copyright (c) 2000-2003 Fabrice Bellard
        -:    4: *
        -:    5: * This file is part of FFmpeg.
        -:    6: *
        -:    7: * FFmpeg is free software; you can redistribute it and/or
        -:    8: * modify it under the terms of the GNU Lesser General Public
        -:    9: * License as published by the Free Software Foundation; either
        -:   10: * version 2.1 of the License, or (at your option) any later version.
        -:   11: *
        -:   12: * FFmpeg is distributed in the hope that it will be useful,
        -:   13: * but WITHOUT ANY WARRANTY; without even the implied warranty of
        -:   14: * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
        -:   15: * Lesser General Public License for more details.
        -:   16: *
        -:   17: * You should have received a copy of the GNU Lesser General Public
        -:   18: * License along with FFmpeg; if not, write to the Free Software
        -:   19: * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
        -:   20: */
        -:   21:
        -:   22:/* needed for usleep() */
        -:   23:#define _XOPEN_SOURCE 600
        -:   24:
        -:   25:#include "config.h"
        -:   26:#include <ctype.h>
        -:   27:#include <string.h>
        -:   28:#include <math.h>
        -:   29:#include <stdlib.h>
        -:   30:#include <errno.h>
        -:   31:#include <signal.h>
        -:   32:#include <limits.h>
        -:   33:#include <unistd.h>
        -:   34:#include "libavformat/avformat.h"
        -:   35:#include "libavdevice/avdevice.h"
        -:   36:#include "libswscale/swscale.h"
        -:   37:#include "libavcodec/opt.h"
        -:   38:#include "libavcodec/audioconvert.h"
        -:   39:#include "libavcore/audioconvert.h"
        -:   40:#include "libavcore/parseutils.h"
        -:   41:#include "libavcore/samplefmt.h"
        -:   42:#include "libavutil/colorspace.h"
        -:   43:#include "libavutil/fifo.h"
        -:   44:#include "libavutil/intreadwrite.h"
        -:   45:#include "libavutil/pixdesc.h"
        -:   46:#include "libavutil/avstring.h"
        -:   47:#include "libavutil/libm.h"
        -:   48:#include "libavformat/os_support.h"
        -:   49:
        -:   50:#if CONFIG_AVFILTER
        -:   51:# include "libavfilter/avfilter.h"
        -:   52:# include "libavfilter/avfiltergraph.h"
        -:   53:# include "libavfilter/vsrc_buffer.h"
        -:   54:#endif
        -:   55:
        -:   56:#if HAVE_SYS_RESOURCE_H
        -:   57:#include <sys/types.h>
        -:   58:#include <sys/time.h>
        -:   59:#include <sys/resource.h>
        -:   60:#elif HAVE_GETPROCESSTIMES
        -:   61:#include <windows.h>
        -:   62:#endif
        -:   63:#if HAVE_GETPROCESSMEMORYINFO
        -:   64:#include <windows.h>
        -:   65:#include <psapi.h>
        -:   66:#endif
        -:   67:
        -:   68:#if HAVE_SYS_SELECT_H
        -:   69:#include <sys/select.h>
        -:   70:#endif
        -:   71:
        -:   72:#if HAVE_TERMIOS_H
        -:   73:#include <fcntl.h>
        -:   74:#include <sys/ioctl.h>
        -:   75:#include <sys/time.h>
        -:   76:#include <termios.h>
        -:   77:#elif HAVE_CONIO_H
        -:   78:#include <conio.h>
        -:   79:#endif
        -:   80:#include <time.h>
        -:   81:
        -:   82:#include "cmdutils.h"
        -:   83:
        -:   84:#include "libavutil/avassert.h"
        -:   85:
        -:   86:const char program_name[] = "FFmpeg";
        -:   87:const int program_birth_year = 2000;
        -:   88:
        -:   89:/* select an input stream for an output stream */
        -:   90:typedef struct AVStreamMap {
        -:   91:    int file_index;
        -:   92:    int stream_index;
        -:   93:    int sync_file_index;
        -:   94:    int sync_stream_index;
        -:   95:} AVStreamMap;
        -:   96:
        -:   97:/**
        -:   98: * select an input file for an output file
        -:   99: */
        -:  100:typedef struct AVMetaDataMap {
        -:  101:    int  file;      //< file index
        -:  102:    char type;      //< type of metadata to copy -- (g)lobal, (s)tream, (c)hapter or (p)rogram
        -:  103:    int  index;     //< stream/chapter/program number
        -:  104:} AVMetaDataMap;
        -:  105:
        -:  106:typedef struct AVChapterMap {
        -:  107:    int in_file;
        -:  108:    int out_file;
        -:  109:} AVChapterMap;
        -:  110:
        -:  111:static const OptionDef options[];
        -:  112:
        -:  113:#define MAX_FILES 100
        -:  114:#if !FF_API_MAX_STREAMS
        -:  115:#define MAX_STREAMS 1024    /* arbitrary sanity check value */
        -:  116:#endif
        -:  117:
        -:  118:static const char *last_asked_format = NULL;
        -:  119:static AVFormatContext *input_files[MAX_FILES];
        -:  120:static int64_t input_files_ts_offset[MAX_FILES];
        -:  121:static double *input_files_ts_scale[MAX_FILES] = {NULL};
        -:  122:static AVCodec **input_codecs = NULL;
        -:  123:static int nb_input_files = 0;
        -:  124:static int nb_input_codecs = 0;
        -:  125:static int nb_input_files_ts_scale[MAX_FILES] = {0};
        -:  126:
        -:  127:static AVFormatContext *output_files[MAX_FILES];
        -:  128:static AVCodec **output_codecs = NULL;
        -:  129:static int nb_output_files = 0;
        -:  130:static int nb_output_codecs = 0;
        -:  131:
        -:  132:static AVStreamMap *stream_maps = NULL;
        -:  133:static int nb_stream_maps;
        -:  134:
        -:  135:/* first item specifies output metadata, second is input */
        -:  136:static AVMetaDataMap (*meta_data_maps)[2] = NULL;
        -:  137:static int nb_meta_data_maps;
        -:  138:static int metadata_global_autocopy   = 1;
        -:  139:static int metadata_streams_autocopy  = 1;
        -:  140:static int metadata_chapters_autocopy = 1;
        -:  141:
        -:  142:static AVChapterMap *chapter_maps = NULL;
        -:  143:static int nb_chapter_maps;
        -:  144:
        -:  145:/* indexed by output file stream index */
        -:  146:static int *streamid_map = NULL;
        -:  147:static int nb_streamid_map = 0;
        -:  148:
        -:  149:static int frame_width  = 0;
        -:  150:static int frame_height = 0;
        -:  151:static float frame_aspect_ratio = 0;
        -:  152:static enum PixelFormat frame_pix_fmt = PIX_FMT_NONE;
        -:  153:static enum AVSampleFormat audio_sample_fmt = AV_SAMPLE_FMT_NONE;
        -:  154:static int max_frames[4] = {INT_MAX, INT_MAX, INT_MAX, INT_MAX};
        -:  155:static AVRational frame_rate;
        -:  156:static float video_qscale = 0;
        -:  157:static uint16_t *intra_matrix = NULL;
        -:  158:static uint16_t *inter_matrix = NULL;
        -:  159:static const char *video_rc_override_string=NULL;
        -:  160:static int video_disable = 0;
        -:  161:static int video_discard = 0;
        -:  162:static char *video_codec_name = NULL;
        -:  163:static unsigned int video_codec_tag = 0;
        -:  164:static char *video_language = NULL;
        -:  165:static int same_quality = 0;
        -:  166:static int do_deinterlace = 0;
        -:  167:static int top_field_first = -1;
        -:  168:static int me_threshold = 0;
        -:  169:static int intra_dc_precision = 8;
        -:  170:static int loop_input = 0;
        -:  171:static int loop_output = AVFMT_NOOUTPUTLOOP;
        -:  172:static int qp_hist = 0;
        -:  173:#if CONFIG_AVFILTER
        -:  174:static char *vfilters = NULL;
        -:  175:AVFilterGraph *graph = NULL;
        -:  176:#endif
        -:  177:
        -:  178:static int intra_only = 0;
        -:  179:static int audio_sample_rate = 44100;
        -:  180:static int64_t channel_layout = 0;
        -:  181:#define QSCALE_NONE -99999
        -:  182:static float audio_qscale = QSCALE_NONE;
        -:  183:static int audio_disable = 0;
        -:  184:static int audio_channels = 1;
        -:  185:static char  *audio_codec_name = NULL;
        -:  186:static unsigned int audio_codec_tag = 0;
        -:  187:static char *audio_language = NULL;
        -:  188:
        -:  189:static int subtitle_disable = 0;
        -:  190:static char *subtitle_codec_name = NULL;
        -:  191:static char *subtitle_language = NULL;
        -:  192:static unsigned int subtitle_codec_tag = 0;
        -:  193:
        -:  194:static float mux_preload= 0.5;
        -:  195:static float mux_max_delay= 0.7;
        -:  196:
        -:  197:static int64_t recording_time = INT64_MAX;
        -:  198:static int64_t start_time = 0;
        -:  199:static int64_t recording_timestamp = 0;
        -:  200:static int64_t input_ts_offset = 0;
        -:  201:static int file_overwrite = 0;
        -:  202:static AVMetadata *metadata;
        -:  203:static int do_benchmark = 0;
        -:  204:static int do_hex_dump = 0;
        -:  205:static int do_pkt_dump = 0;
        -:  206:static int do_psnr = 0;
        -:  207:static int do_pass = 0;
        -:  208:static char *pass_logfilename_prefix = NULL;
        -:  209:static int audio_stream_copy = 0;
        -:  210:static int video_stream_copy = 0;
        -:  211:static int subtitle_stream_copy = 0;
        -:  212:static int video_sync_method= -1;
        -:  213:static int audio_sync_method= 0;
        -:  214:static float audio_drift_threshold= 0.1;
        -:  215:static int copy_ts= 0;
        -:  216:static int copy_tb;
        -:  217:static int opt_shortest = 0;
        -:  218:static int video_global_header = 0;
        -:  219:static char *vstats_filename;
        -:  220:static FILE *vstats_file;
        -:  221:static int opt_programid = 0;
        -:  222:static int copy_initial_nonkeyframes = 0;
        -:  223:
        -:  224:static int rate_emu = 0;
        -:  225:
        -:  226:static int  video_channel = 0;
        -:  227:static char *video_standard;
        -:  228:
        -:  229:static int audio_volume = 256;
        -:  230:
        -:  231:static int exit_on_error = 0;
        -:  232:static int using_stdin = 0;
        -:  233:static int verbose = 1;
        -:  234:static int thread_count= 1;
        -:  235:static int q_pressed = 0;
        -:  236:static int64_t video_size = 0;
        -:  237:static int64_t audio_size = 0;
        -:  238:static int64_t extra_size = 0;
        -:  239:static int nb_frames_dup = 0;
        -:  240:static int nb_frames_drop = 0;
        -:  241:static int input_sync;
        -:  242:static uint64_t limit_filesize = 0;
        -:  243:static int force_fps = 0;
        -:  244:static char *forced_key_frames = NULL;
        -:  245:
        -:  246:static float dts_delta_threshold = 10;
        -:  247:
        -:  248:static unsigned int sws_flags = SWS_BICUBIC;
        -:  249:
        -:  250:static int64_t timer_start;
        -:  251:
        -:  252:static uint8_t *audio_buf;
        -:  253:static uint8_t *audio_out;
        -:  254:unsigned int allocated_audio_out_size, allocated_audio_buf_size;
        -:  255:
        -:  256:static short *samples;
        -:  257:
        -:  258:static AVBitStreamFilterContext *video_bitstream_filters=NULL;
        -:  259:static AVBitStreamFilterContext *audio_bitstream_filters=NULL;
        -:  260:static AVBitStreamFilterContext *subtitle_bitstream_filters=NULL;
        -:  261:
        -:  262:#define DEFAULT_PASS_LOGFILENAME_PREFIX "ffmpeg2pass"
        -:  263:
        -:  264:struct AVInputStream;
        -:  265:
        -:  266:typedef struct AVOutputStream {
        -:  267:    int file_index;          /* file index */
        -:  268:    int index;               /* stream index in the output file */
        -:  269:    int source_index;        /* AVInputStream index */
        -:  270:    AVStream *st;            /* stream in the output file */
        -:  271:    int encoding_needed;     /* true if encoding needed for this stream */
        -:  272:    int frame_number;
        -:  273:    /* input pts and corresponding output pts
        -:  274:       for A/V sync */
        -:  275:    //double sync_ipts;        /* dts from the AVPacket of the demuxer in second units */
        -:  276:    struct AVInputStream *sync_ist; /* input stream to sync against */
        -:  277:    int64_t sync_opts;       /* output frame counter, could be changed to some true timestamp */ //FIXME look at frame_number
        -:  278:    AVBitStreamFilterContext *bitstream_filters;
        -:  279:    /* video only */
        -:  280:    int video_resample;
        -:  281:    AVFrame pict_tmp;      /* temporary image for resampling */
        -:  282:    struct SwsContext *img_resample_ctx; /* for image resampling */
        -:  283:    int resample_height;
        -:  284:    int resample_width;
        -:  285:    int resample_pix_fmt;
        -:  286:
        -:  287:    /* full frame size of first frame */
        -:  288:    int original_height;
        -:  289:    int original_width;
        -:  290:
        -:  291:    /* forced key frames */
        -:  292:    int64_t *forced_kf_pts;
        -:  293:    int forced_kf_count;
        -:  294:    int forced_kf_index;
        -:  295:
        -:  296:    /* audio only */
        -:  297:    int audio_resample;
        -:  298:    ReSampleContext *resample; /* for audio resampling */
        -:  299:    int resample_sample_fmt;
        -:  300:    int resample_channels;
        -:  301:    int resample_sample_rate;
        -:  302:    int reformat_pair;
        -:  303:    AVAudioConvert *reformat_ctx;
        -:  304:    AVFifoBuffer *fifo;     /* for compression: one audio fifo per codec */
        -:  305:    FILE *logfile;
        -:  306:} AVOutputStream;
        -:  307:
        -:  308:static AVOutputStream **output_streams_for_file[MAX_FILES] = { NULL };
        -:  309:static int nb_output_streams_for_file[MAX_FILES] = { 0 };
        -:  310:
        -:  311:typedef struct AVInputStream {
        -:  312:    int file_index;
        -:  313:    int index;
        -:  314:    AVStream *st;
        -:  315:    int discard;             /* true if stream data should be discarded */
        -:  316:    int decoding_needed;     /* true if the packets must be decoded in 'raw_fifo' */
        -:  317:    int64_t sample_index;      /* current sample */
        -:  318:
        -:  319:    int64_t       start;     /* time when read started */
        -:  320:    int64_t       next_pts;  /* synthetic pts for cases where pkt.pts
        -:  321:                                is not defined */
        -:  322:    int64_t       pts;       /* current pts */
        -:  323:    PtsCorrectionContext pts_ctx;
        -:  324:    int is_start;            /* is 1 at the start and after a discontinuity */
        -:  325:    int showed_multi_packet_warning;
        -:  326:    int is_past_recording_time;
        -:  327:#if CONFIG_AVFILTER
        -:  328:    AVFilterContext *output_video_filter;
        -:  329:    AVFilterContext *input_video_filter;
        -:  330:    AVFrame *filter_frame;
        -:  331:    int has_filter_frame;
        -:  332:    AVFilterBufferRef *picref;
        -:  333:#endif
        -:  334:} AVInputStream;
        -:  335:
        -:  336:typedef struct AVInputFile {
        -:  337:    int eof_reached;      /* true if eof reached */
        -:  338:    int ist_index;        /* index of first stream in ist_table */
        -:  339:    int buffer_size;      /* current total buffer size */
        -:  340:    int nb_streams;       /* nb streams we are aware of */
        -:  341:} AVInputFile;
        -:  342:
        -:  343:#if HAVE_TERMIOS_H
        -:  344:
        -:  345:/* init terminal so that we can grab keys */
        -:  346:static struct termios oldtty;
        -:  347:#endif
        -:  348:
        -:  349:#if CONFIG_AVFILTER
        -:  350:
        -:  351:static int configure_filters(AVInputStream *ist, AVOutputStream *ost)
    #####:  352:{
        -:  353:    AVFilterContext *last_filter, *filter;
        -:  354:    /** filter graph containing all filters including input & output */
    #####:  355:    AVCodecContext *codec = ost->st->codec;
    #####:  356:    AVCodecContext *icodec = ist->st->codec;
    #####:  357:    FFSinkContext ffsink_ctx = { .pix_fmt = codec->pix_fmt };
        -:  358:    char args[255];
        -:  359:    int ret;
        -:  360:
    #####:  361:    graph = avfilter_graph_alloc();
        -:  362:
    #####:  363:    snprintf(args, 255, "%d:%d:%d:%d:%d", ist->st->codec->width,
        -:  364:             ist->st->codec->height, ist->st->codec->pix_fmt, 1, AV_TIME_BASE);
    #####:  365:    ret = avfilter_graph_create_filter(&ist->input_video_filter, avfilter_get_by_name("buffer"),
        -:  366:                                       "src", args, NULL, graph);
    #####:  367:    if (ret < 0)
    #####:  368:        return ret;
    #####:  369:    ret = avfilter_graph_create_filter(&ist->output_video_filter, &ffsink,
        -:  370:                                       "out", NULL, &ffsink_ctx, graph);
    #####:  371:    if (ret < 0)
    #####:  372:        return ret;
    #####:  373:    last_filter = ist->input_video_filter;
        -:  374:
    #####:  375:    if (codec->width  != icodec->width || codec->height != icodec->height) {
    #####:  376:        snprintf(args, 255, "%d:%d:flags=0x%X",
        -:  377:                 codec->width,
        -:  378:                 codec->height,
        -:  379:                 (int)av_get_int(sws_opts, "sws_flags", NULL));
    #####:  380:        if ((ret = avfilter_graph_create_filter(&filter, avfilter_get_by_name("scale"),
        -:  381:                                                NULL, args, NULL, graph)) < 0)
    #####:  382:            return ret;
    #####:  383:        if ((ret = avfilter_link(last_filter, 0, filter, 0)) < 0)
    #####:  384:            return ret;
    #####:  385:        last_filter = filter;
        -:  386:    }
        -:  387:
    #####:  388:    snprintf(args, sizeof(args), "flags=0x%X", (int)av_get_int(sws_opts, "sws_flags", NULL));
    #####:  389:    graph->scale_sws_opts = av_strdup(args);
        -:  390:
    #####:  391:    if (vfilters) {
    #####:  392:        AVFilterInOut *outputs = av_malloc(sizeof(AVFilterInOut));
    #####:  393:        AVFilterInOut *inputs  = av_malloc(sizeof(AVFilterInOut));
        -:  394:
    #####:  395:        outputs->name    = av_strdup("in");
    #####:  396:        outputs->filter_ctx = last_filter;
    #####:  397:        outputs->pad_idx = 0;
    #####:  398:        outputs->next    = NULL;
        -:  399:
    #####:  400:        inputs->name    = av_strdup("out");
    #####:  401:        inputs->filter_ctx = ist->output_video_filter;
    #####:  402:        inputs->pad_idx = 0;
    #####:  403:        inputs->next    = NULL;
        -:  404:
    #####:  405:        if ((ret = avfilter_graph_parse(graph, vfilters, inputs, outputs, NULL)) < 0)
    #####:  406:            return ret;
    #####:  407:        av_freep(&vfilters);
        -:  408:    } else {
    #####:  409:        if ((ret = avfilter_link(last_filter, 0, ist->output_video_filter, 0)) < 0)
    #####:  410:            return ret;
        -:  411:    }
        -:  412:
    #####:  413:    if ((ret = avfilter_graph_config(graph, NULL)) < 0)
    #####:  414:        return ret;
        -:  415:
    #####:  416:    codec->width  = ist->output_video_filter->inputs[0]->w;
    #####:  417:    codec->height = ist->output_video_filter->inputs[0]->h;
        -:  418:
    #####:  419:    return 0;
        -:  420:}
        -:  421:#endif /* CONFIG_AVFILTER */
        -:  422:
        -:  423:static void term_exit(void)
        2:  424:{
        2:  425:    av_log(NULL, AV_LOG_QUIET, "");
        -:  426:#if HAVE_TERMIOS_H
        2:  427:    tcsetattr (0, TCSANOW, &oldtty);
        -:  428:#endif
        2:  429:}
        -:  430:
        -:  431:static volatile int received_sigterm = 0;
        -:  432:
        -:  433:static void
        -:  434:sigterm_handler(int sig)
    #####:  435:{
    #####:  436:    received_sigterm = sig;
    #####:  437:    term_exit();
    #####:  438:}
        -:  439:
        -:  440:static void term_init(void)
        1:  441:{
        -:  442:#if HAVE_TERMIOS_H
        -:  443:    struct termios tty;
        -:  444:
        1:  445:    tcgetattr (0, &tty);
        1:  446:    oldtty = tty;
        1:  447:    atexit(term_exit);
        -:  448:
        1:  449:    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
        -:  450:                          |INLCR|IGNCR|ICRNL|IXON);
        1:  451:    tty.c_oflag |= OPOST;
        1:  452:    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);
        1:  453:    tty.c_cflag &= ~(CSIZE|PARENB);
        1:  454:    tty.c_cflag |= CS8;
        1:  455:    tty.c_cc[VMIN] = 1;
        1:  456:    tty.c_cc[VTIME] = 0;
        -:  457:
        1:  458:    tcsetattr (0, TCSANOW, &tty);
        1:  459:    signal(SIGQUIT, sigterm_handler); /* Quit (POSIX).  */
        -:  460:#endif
        -:  461:
        1:  462:    signal(SIGINT , sigterm_handler); /* Interrupt (ANSI).  */
        1:  463:    signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */
        -:  464:#ifdef SIGXCPU
        1:  465:    signal(SIGXCPU, sigterm_handler);
        -:  466:#endif
        1:  467:}
        -:  468:
        -:  469:/* read a key without blocking */
        -:  470:static int read_key(void)
    21914:  471:{
        -:  472:#if HAVE_TERMIOS_H
    21914:  473:    int n = 1;
        -:  474:    unsigned char ch;
        -:  475:    struct timeval tv;
        -:  476:    fd_set rfds;
        -:  477:
    21914:  478:    FD_ZERO(&rfds);
    21914:  479:    FD_SET(0, &rfds);
    21914:  480:    tv.tv_sec = 0;
    21914:  481:    tv.tv_usec = 0;
    21914:  482:    n = select(1, &rfds, NULL, NULL, &tv);
    21914:  483:    if (n > 0) {
    #####:  484:        n = read(0, &ch, 1);
    #####:  485:        if (n == 1)
    #####:  486:            return ch;
        -:  487:
    #####:  488:        return n;
        -:  489:    }
        -:  490:#elif HAVE_CONIO_H
        -:  491:    if(kbhit())
        -:  492:        return(getch());
        -:  493:#endif
    21914:  494:    return -1;
        -:  495:}
        -:  496:
        -:  497:static int decode_interrupt_cb(void)
      362:  498:{
      362:  499:    return q_pressed || (q_pressed = read_key() == 'q');
        -:  500:}
        -:  501:
        -:  502:static int ffmpeg_exit(int ret)
        1:  503:{
        -:  504:    int i;
        -:  505:
        -:  506:    /* close files */
        2:  507:    for(i=0;i<nb_output_files;i++) {
        -:  508:        /* maybe av_close_output_file ??? */
        1:  509:        AVFormatContext *s = output_files[i];
        -:  510:        int j;
        1:  511:        if (!(s->oformat->flags & AVFMT_NOFILE) && s->pb)
        1:  512:            url_fclose(s->pb);
        3:  513:        for(j=0;j<s->nb_streams;j++) {
        2:  514:            av_metadata_free(&s->streams[j]->metadata);
        2:  515:            av_free(s->streams[j]->codec);
        2:  516:            av_free(s->streams[j]->info);
        2:  517:            av_free(s->streams[j]);
        -:  518:        }
        1:  519:        for(j=0;j<s->nb_programs;j++) {
    #####:  520:            av_metadata_free(&s->programs[j]->metadata);
        -:  521:        }
        1:  522:        for(j=0;j<s->nb_chapters;j++) {
    #####:  523:            av_metadata_free(&s->chapters[j]->metadata);
        -:  524:        }
        1:  525:        av_metadata_free(&s->metadata);
        1:  526:        av_free(s);
        1:  527:        av_free(output_streams_for_file[i]);
        -:  528:    }
        2:  529:    for(i=0;i<nb_input_files;i++) {
        1:  530:        av_close_input_file(input_files[i]);
        1:  531:        av_free(input_files_ts_scale[i]);
        -:  532:    }
        -:  533:
        1:  534:    av_free(intra_matrix);
        1:  535:    av_free(inter_matrix);
        -:  536:
        1:  537:    if (vstats_file)
    #####:  538:        fclose(vstats_file);
        1:  539:    av_free(vstats_filename);
        -:  540:
        1:  541:    av_free(opt_names);
        1:  542:    av_free(streamid_map);
        1:  543:    av_free(input_codecs);
        1:  544:    av_free(output_codecs);
        1:  545:    av_free(stream_maps);
        1:  546:    av_free(meta_data_maps);
        -:  547:
        1:  548:    av_free(video_codec_name);
        1:  549:    av_free(audio_codec_name);
        1:  550:    av_free(subtitle_codec_name);
        -:  551:
        1:  552:    av_free(video_standard);
        -:  553:
        1:  554:    uninit_opts();
        1:  555:    av_free(audio_buf);
        1:  556:    av_free(audio_out);
        1:  557:    allocated_audio_buf_size= allocated_audio_out_size= 0;
        1:  558:    av_free(samples);
        -:  559:
        -:  560:#if CONFIG_AVFILTER
        1:  561:    avfilter_uninit();
        -:  562:#endif
        -:  563:
        1:  564:    if (received_sigterm) {
    #####:  565:        fprintf(stderr,
        -:  566:            "Received signal %d: terminating.\n",
        -:  567:            (int) received_sigterm);
    #####:  568:        exit (255);
        -:  569:    }
        -:  570:
        1:  571:    exit(ret); /* not all OS-es handle main() return value */
        -:  572:    return ret;
        -:  573:}
        -:  574:
        -:  575:/* similar to ff_dynarray_add() and av_fast_realloc() */
        -:  576:static void *grow_array(void *array, int elem_size, int *size, int new_size)
        6:  577:{
        6:  578:    if (new_size >= INT_MAX / elem_size) {
    #####:  579:        fprintf(stderr, "Array too big.\n");
    #####:  580:        ffmpeg_exit(1);
        -:  581:    }
        6:  582:    if (*size < new_size) {
        6:  583:        uint8_t *tmp = av_realloc(array, new_size*elem_size);
        6:  584:        if (!tmp) {
    #####:  585:            fprintf(stderr, "Could not alloc buffer.\n");
    #####:  586:            ffmpeg_exit(1);
        -:  587:        }
        6:  588:        memset(tmp + *size*elem_size, 0, (new_size-*size) * elem_size);
        6:  589:        *size = new_size;
        6:  590:        return tmp;
        -:  591:    }
    #####:  592:    return array;
        -:  593:}
        -:  594:
        -:  595:static void choose_sample_fmt(AVStream *st, AVCodec *codec)
        -:  596:{
    #####:  597:    if(codec && codec->sample_fmts){
    #####:  598:        const enum AVSampleFormat *p= codec->sample_fmts;
    #####:  599:        for(; *p!=-1; p++){
    #####:  600:            if(*p == st->codec->sample_fmt)
        -:  601:                break;
        -:  602:        }
    #####:  603:        if(*p == -1)
    #####:  604:            st->codec->sample_fmt = codec->sample_fmts[0];
        -:  605:    }
        -:  606:}
        -:  607:
        -:  608:static void choose_sample_rate(AVStream *st, AVCodec *codec)
    #####:  609:{
    #####:  610:    if(codec && codec->supported_samplerates){
    #####:  611:        const int *p= codec->supported_samplerates;
    #####:  612:        int best=0;
    #####:  613:        int best_dist=INT_MAX;
    #####:  614:        for(; *p; p++){
    #####:  615:            int dist= abs(st->codec->sample_rate - *p);
    #####:  616:            if(dist < best_dist){
    #####:  617:                best_dist= dist;
    #####:  618:                best= *p;
        -:  619:            }
        -:  620:        }
    #####:  621:        if(best_dist){
    #####:  622:            av_log(st->codec, AV_LOG_WARNING, "Requested sampling rate unsupported using closest supported (%d)\n", best);
        -:  623:        }
    #####:  624:        st->codec->sample_rate= best;
        -:  625:    }
    #####:  626:}
        -:  627:
        -:  628:static void choose_pixel_fmt(AVStream *st, AVCodec *codec)
    #####:  629:{
    #####:  630:    if(codec && codec->pix_fmts){
    #####:  631:        const enum PixelFormat *p= codec->pix_fmts;
    #####:  632:        if(st->codec->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL){
    #####:  633:            if(st->codec->codec_id==CODEC_ID_MJPEG){
    #####:  634:                p= (const enum PixelFormat[]){PIX_FMT_YUVJ420P, PIX_FMT_YUVJ422P, PIX_FMT_YUV420P, PIX_FMT_YUV422P, PIX_FMT_NONE};
    #####:  635:            }else if(st->codec->codec_id==CODEC_ID_LJPEG){
    #####:  636:                p= (const enum PixelFormat[]){PIX_FMT_YUVJ420P, PIX_FMT_YUVJ422P, PIX_FMT_YUVJ444P, PIX_FMT_YUV420P, PIX_FMT_YUV422P, PIX_FMT_YUV444P, PIX_FMT_BGRA, PIX_FMT_NONE};
        -:  637:            }
        -:  638:        }
    #####:  639:        for(; *p!=-1; p++){
    #####:  640:            if(*p == st->codec->pix_fmt)
    #####:  641:                break;
        -:  642:        }
    #####:  643:        if(*p == -1)
    #####:  644:            st->codec->pix_fmt = codec->pix_fmts[0];
        -:  645:    }
    #####:  646:}
        -:  647:
        -:  648:static AVOutputStream *new_output_stream(AVFormatContext *oc, int file_idx)
        2:  649:{
        2:  650:    int idx = oc->nb_streams - 1;
        -:  651:    AVOutputStream *ost;
        -:  652:
        2:  653:    output_streams_for_file[file_idx] =
        -:  654:        grow_array(output_streams_for_file[file_idx],
        -:  655:                   sizeof(*output_streams_for_file[file_idx]),
        -:  656:                   &nb_output_streams_for_file[file_idx],
        -:  657:                   oc->nb_streams);
        2:  658:    ost = output_streams_for_file[file_idx][idx] =
        -:  659:        av_mallocz(sizeof(AVOutputStream));
        2:  660:    if (!ost) {
    #####:  661:        fprintf(stderr, "Could not alloc output stream\n");
    #####:  662:        ffmpeg_exit(1);
        -:  663:    }
        2:  664:    ost->file_index = file_idx;
        2:  665:    ost->index = idx;
        2:  666:    return ost;
        -:  667:}
        -:  668:
        -:  669:static int read_ffserver_streams(AVFormatContext *s, const char *filename)
    #####:  670:{
        -:  671:    int i, err;
        -:  672:    AVFormatContext *ic;
    #####:  673:    int nopts = 0;
        -:  674:
    #####:  675:    err = av_open_input_file(&ic, filename, NULL, FFM_PACKET_SIZE, NULL);
    #####:  676:    if (err < 0)
    #####:  677:        return err;
        -:  678:    /* copy stream format */
    #####:  679:    s->nb_streams = 0;
    #####:  680:    for(i=0;i<ic->nb_streams;i++) {
        -:  681:        AVStream *st;
        -:  682:        AVCodec *codec;
        -:  683:
    #####:  684:        s->nb_streams++;
        -:  685:
        -:  686:        // FIXME: a more elegant solution is needed
    #####:  687:        st = av_mallocz(sizeof(AVStream));
    #####:  688:        memcpy(st, ic->streams[i], sizeof(AVStream));
    #####:  689:        st->codec = avcodec_alloc_context();
    #####:  690:        if (!st->codec) {
    #####:  691:            print_error(filename, AVERROR(ENOMEM));
    #####:  692:            ffmpeg_exit(1);
        -:  693:        }
    #####:  694:        avcodec_copy_context(st->codec, ic->streams[i]->codec);
    #####:  695:        s->streams[i] = st;
        -:  696:
    #####:  697:        codec = avcodec_find_encoder(st->codec->codec_id);
    #####:  698:        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
    #####:  699:            if (audio_stream_copy) {
    #####:  700:                st->stream_copy = 1;
        -:  701:            } else
        -:  702:                choose_sample_fmt(st, codec);
    #####:  703:        } else if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
    #####:  704:            if (video_stream_copy) {
    #####:  705:                st->stream_copy = 1;
        -:  706:            } else
    #####:  707:                choose_pixel_fmt(st, codec);
        -:  708:        }
        -:  709:
    #####:  710:        if(!st->codec->thread_count)
    #####:  711:            st->codec->thread_count = 1;
    #####:  712:        if(st->codec->thread_count>1)
    #####:  713:            avcodec_thread_init(st->codec, st->codec->thread_count);
        -:  714:
    #####:  715:        if(st->codec->flags & CODEC_FLAG_BITEXACT)
    #####:  716:            nopts = 1;
        -:  717:
    #####:  718:        new_output_stream(s, nb_output_files);
        -:  719:    }
        -:  720:
    #####:  721:    if (!nopts)
    #####:  722:        s->timestamp = av_gettime();
        -:  723:
    #####:  724:    av_close_input_file(ic);
    #####:  725:    return 0;
        -:  726:}
        -:  727:
        -:  728:static double
        -:  729:get_sync_ipts(const AVOutputStream *ost)
        -:  730:{
    #####:  731:    const AVInputStream *ist = ost->sync_ist;
    #####:  732:    return (double)(ist->pts - start_time)/AV_TIME_BASE;
        -:  733:}
        -:  734:
    21550:  735:static void write_frame(AVFormatContext *s, AVPacket *pkt, AVCodecContext *avctx, AVBitStreamFilterContext *bsfc){
        -:  736:    int ret;
        -:  737:
    43100:  738:    while(bsfc){
    #####:  739:        AVPacket new_pkt= *pkt;
        -:  740:        int a= av_bitstream_filter_filter(bsfc, avctx, NULL,
        -:  741:                                          &new_pkt.data, &new_pkt.size,
        -:  742:                                          pkt->data, pkt->size,
    #####:  743:                                          pkt->flags & AV_PKT_FLAG_KEY);
    #####:  744:        if(a>0){
    #####:  745:            av_free_packet(pkt);
    #####:  746:            new_pkt.destruct= av_destruct_packet;
    #####:  747:        } else if(a<0){
    #####:  748:            fprintf(stderr, "%s failed for stream %d, codec %s",
        -:  749:                    bsfc->filter->name, pkt->stream_index,
        -:  750:                    avctx->codec ? avctx->codec->name : "copy");
    #####:  751:            print_error("", a);
    #####:  752:            if (exit_on_error)
    #####:  753:                ffmpeg_exit(1);
        -:  754:        }
    #####:  755:        *pkt= new_pkt;
        -:  756:
    #####:  757:        bsfc= bsfc->next;
        -:  758:    }
        -:  759:
    21550:  760:    ret= av_interleaved_write_frame(s, pkt);
    21550:  761:    if(ret < 0){
    #####:  762:        print_error("av_interleaved_write_frame()", ret);
    #####:  763:        ffmpeg_exit(1);
        -:  764:    }
    21550:  765:}
        -:  766:
        -:  767:#define MAX_AUDIO_PACKET_SIZE (128 * 1024)
        -:  768:
        -:  769:static void do_audio_out(AVFormatContext *s,
        -:  770:                         AVOutputStream *ost,
        -:  771:                         AVInputStream *ist,
        -:  772:                         unsigned char *buf, int size)
    #####:  773:{
        -:  774:    uint8_t *buftmp;
        -:  775:    int64_t audio_out_size, audio_buf_size;
    #####:  776:    int64_t allocated_for_size= size;
        -:  777:
        -:  778:    int size_out, frame_bytes, ret, resample_changed;
    #####:  779:    AVCodecContext *enc= ost->st->codec;
    #####:  780:    AVCodecContext *dec= ist->st->codec;
    #####:  781:    int osize= av_get_bits_per_sample_fmt(enc->sample_fmt)/8;
    #####:  782:    int isize= av_get_bits_per_sample_fmt(dec->sample_fmt)/8;
    #####:  783:    const int coded_bps = av_get_bits_per_sample(enc->codec->id);
        -:  784:
    #####:  785:need_realloc:
    #####:  786:    audio_buf_size= (allocated_for_size + isize*dec->channels - 1) / (isize*dec->channels);
    #####:  787:    audio_buf_size= (audio_buf_size*enc->sample_rate + dec->sample_rate) / dec->sample_rate;
    #####:  788:    audio_buf_size= audio_buf_size*2 + 10000; //safety factors for the deprecated resampling API
    #####:  789:    audio_buf_size= FFMAX(audio_buf_size, enc->frame_size);
    #####:  790:    audio_buf_size*= osize*enc->channels;
        -:  791:
    #####:  792:    audio_out_size= FFMAX(audio_buf_size, enc->frame_size * osize * enc->channels);
    #####:  793:    if(coded_bps > 8*osize)
    #####:  794:        audio_out_size= audio_out_size * coded_bps / (8*osize);
    #####:  795:    audio_out_size += FF_MIN_BUFFER_SIZE;
        -:  796:
    #####:  797:    if(audio_out_size > INT_MAX || audio_buf_size > INT_MAX){
    #####:  798:        fprintf(stderr, "Buffer sizes too large\n");
    #####:  799:        ffmpeg_exit(1);
        -:  800:    }
        -:  801:
    #####:  802:    av_fast_malloc(&audio_buf, &allocated_audio_buf_size, audio_buf_size);
    #####:  803:    av_fast_malloc(&audio_out, &allocated_audio_out_size, audio_out_size);
    #####:  804:    if (!audio_buf || !audio_out){
    #####:  805:        fprintf(stderr, "Out of memory in do_audio_out\n");
    #####:  806:        ffmpeg_exit(1);
        -:  807:    }
        -:  808:
    #####:  809:    if (enc->channels != dec->channels)
    #####:  810:        ost->audio_resample = 1;
        -:  811:
    #####:  812:    resample_changed = ost->resample_sample_fmt  != dec->sample_fmt ||
        -:  813:                       ost->resample_channels    != dec->channels   ||
        -:  814:                       ost->resample_sample_rate != dec->sample_rate;
        -:  815:
    #####:  816:    if ((ost->audio_resample && !ost->resample) || resample_changed) {
    #####:  817:        if (resample_changed) {
    #####:  818:            av_log(NULL, AV_LOG_INFO, "Input stream #%d.%d frame changed from rate:%d fmt:%s ch:%d to rate:%d fmt:%s ch:%d\n",
        -:  819:                   ist->file_index, ist->index,
        -:  820:                   ost->resample_sample_rate, av_get_sample_fmt_name(ost->resample_sample_fmt), ost->resample_channels,
        -:  821:                   dec->sample_rate, av_get_sample_fmt_name(dec->sample_fmt), dec->channels);
    #####:  822:            ost->resample_sample_fmt  = dec->sample_fmt;
    #####:  823:            ost->resample_channels    = dec->channels;
    #####:  824:            ost->resample_sample_rate = dec->sample_rate;
    #####:  825:            if (ost->resample)
    #####:  826:                audio_resample_close(ost->resample);
        -:  827:        }
    #####:  828:        if (ost->resample_sample_fmt  == enc->sample_fmt &&
        -:  829:            ost->resample_channels    == enc->channels   &&
        -:  830:            ost->resample_sample_rate == enc->sample_rate) {
    #####:  831:            ost->resample = NULL;
    #####:  832:            ost->audio_resample = 0;
        -:  833:        } else {
    #####:  834:            if (dec->sample_fmt != AV_SAMPLE_FMT_S16)
    #####:  835:                fprintf(stderr, "Warning, using s16 intermediate sample format for resampling\n");
    #####:  836:            ost->resample = av_audio_resample_init(enc->channels,    dec->channels,
        -:  837:                                                   enc->sample_rate, dec->sample_rate,
        -:  838:                                                   enc->sample_fmt,  dec->sample_fmt,
        -:  839:                                                   16, 10, 0, 0.8);
    #####:  840:            if (!ost->resample) {
    #####:  841:                fprintf(stderr, "Can not resample %d channels @ %d Hz to %d channels @ %d Hz\n",
        -:  842:                        dec->channels, dec->sample_rate,
        -:  843:                        enc->channels, enc->sample_rate);
    #####:  844:                ffmpeg_exit(1);
        -:  845:            }
        -:  846:        }
        -:  847:    }
        -:  848:
        -:  849:#define MAKE_SFMT_PAIR(a,b) ((a)+AV_SAMPLE_FMT_NB*(b))
    #####:  850:    if (!ost->audio_resample && dec->sample_fmt!=enc->sample_fmt &&
        -:  851:        MAKE_SFMT_PAIR(enc->sample_fmt,dec->sample_fmt)!=ost->reformat_pair) {
    #####:  852:        if (ost->reformat_ctx)
    #####:  853:            av_audio_convert_free(ost->reformat_ctx);
    #####:  854:        ost->reformat_ctx = av_audio_convert_alloc(enc->sample_fmt, 1,
        -:  855:                                                   dec->sample_fmt, 1, NULL, 0);
    #####:  856:        if (!ost->reformat_ctx) {
    #####:  857:            fprintf(stderr, "Cannot convert %s sample format to %s sample format\n",
        -:  858:                av_get_sample_fmt_name(dec->sample_fmt),
        -:  859:                av_get_sample_fmt_name(enc->sample_fmt));
    #####:  860:            ffmpeg_exit(1);
        -:  861:        }
    #####:  862:        ost->reformat_pair=MAKE_SFMT_PAIR(enc->sample_fmt,dec->sample_fmt);
        -:  863:    }
        -:  864:
    #####:  865:    if(audio_sync_method){
        -:  866:        double delta = get_sync_ipts(ost) * enc->sample_rate - ost->sync_opts
    #####:  867:                - av_fifo_size(ost->fifo)/(enc->channels * 2);
    #####:  868:        double idelta= delta*dec->sample_rate / enc->sample_rate;
    #####:  869:        int byte_delta= ((int)idelta)*2*dec->channels;
        -:  870:
        -:  871:        //FIXME resample delay
    #####:  872:        if(fabs(delta) > 50){
    #####:  873:            if(ist->is_start || fabs(delta) > audio_drift_threshold*enc->sample_rate){
    #####:  874:                if(byte_delta < 0){
    #####:  875:                    byte_delta= FFMAX(byte_delta, -size);
    #####:  876:                    size += byte_delta;
    #####:  877:                    buf  -= byte_delta;
    #####:  878:                    if(verbose > 2)
    #####:  879:                        fprintf(stderr, "discarding %d audio samples\n", (int)-delta);
    #####:  880:                    if(!size)
    #####:  881:                        return;
    #####:  882:                    ist->is_start=0;
        -:  883:                }else{
        -:  884:                    static uint8_t *input_tmp= NULL;
    #####:  885:                    input_tmp= av_realloc(input_tmp, byte_delta + size);
        -:  886:
    #####:  887:                    if(byte_delta > allocated_for_size - size){
    #####:  888:                        allocated_for_size= byte_delta + (int64_t)size;
    #####:  889:                        goto need_realloc;
        -:  890:                    }
    #####:  891:                    ist->is_start=0;
        -:  892:
    #####:  893:                    memset(input_tmp, 0, byte_delta);
    #####:  894:                    memcpy(input_tmp + byte_delta, buf, size);
    #####:  895:                    buf= input_tmp;
    #####:  896:                    size += byte_delta;
    #####:  897:                    if(verbose > 2)
    #####:  898:                        fprintf(stderr, "adding %d audio samples of silence\n", (int)delta);
        -:  899:                }
    #####:  900:            }else if(audio_sync_method>1){
    #####:  901:                int comp= av_clip(delta, -audio_sync_method, audio_sync_method);
    #####:  902:                av_assert0(ost->audio_resample);
    #####:  903:                if(verbose > 2)
    #####:  904:                    fprintf(stderr, "compensating audio timestamp drift:%f compensation:%d in:%d\n", delta, comp, enc->sample_rate);
        -:  905://                fprintf(stderr, "drift:%f len:%d opts:%"PRId64" ipts:%"PRId64" fifo:%d\n", delta, -1, ost->sync_opts, (int64_t)(get_sync_ipts(ost) * enc->sample_rate), av_fifo_size(ost->fifo)/(ost->st->codec->channels * 2));
    #####:  906:                av_resample_compensate(*(struct AVResampleContext**)ost->resample, comp, enc->sample_rate);
        -:  907:            }
        -:  908:        }
        -:  909:    }else
    #####:  910:        ost->sync_opts= lrintf(get_sync_ipts(ost) * enc->sample_rate)
        -:  911:                        - av_fifo_size(ost->fifo)/(enc->channels * 2); //FIXME wrong
        -:  912:
    #####:  913:    if (ost->audio_resample) {
    #####:  914:        buftmp = audio_buf;
    #####:  915:        size_out = audio_resample(ost->resample,
        -:  916:                                  (short *)buftmp, (short *)buf,
        -:  917:                                  size / (dec->channels * isize));
    #####:  918:        size_out = size_out * enc->channels * osize;
        -:  919:    } else {
    #####:  920:        buftmp = buf;
    #####:  921:        size_out = size;
        -:  922:    }
        -:  923:
    #####:  924:    if (!ost->audio_resample && dec->sample_fmt!=enc->sample_fmt) {
    #####:  925:        const void *ibuf[6]= {buftmp};
    #####:  926:        void *obuf[6]= {audio_buf};
    #####:  927:        int istride[6]= {isize};
    #####:  928:        int ostride[6]= {osize};
    #####:  929:        int len= size_out/istride[0];
    #####:  930:        if (av_audio_convert(ost->reformat_ctx, obuf, ostride, ibuf, istride, len)<0) {
    #####:  931:            printf("av_audio_convert() failed\n");
    #####:  932:            if (exit_on_error)
    #####:  933:                ffmpeg_exit(1);
        -:  934:            return;
        -:  935:        }
    #####:  936:        buftmp = audio_buf;
    #####:  937:        size_out = len*osize;
        -:  938:    }
        -:  939:
        -:  940:    /* now encode as many frames as possible */
    #####:  941:    if (enc->frame_size > 1) {
        -:  942:        /* output resampled raw samples */
    #####:  943:        if (av_fifo_realloc2(ost->fifo, av_fifo_size(ost->fifo) + size_out) < 0) {
    #####:  944:            fprintf(stderr, "av_fifo_realloc2() failed\n");
    #####:  945:            ffmpeg_exit(1);
        -:  946:        }
    #####:  947:        av_fifo_generic_write(ost->fifo, buftmp, size_out, NULL);
        -:  948:
    #####:  949:        frame_bytes = enc->frame_size * osize * enc->channels;
        -:  950:
    #####:  951:        while (av_fifo_size(ost->fifo) >= frame_bytes) {
        -:  952:            AVPacket pkt;
    #####:  953:            av_init_packet(&pkt);
        -:  954:
    #####:  955:            av_fifo_generic_read(ost->fifo, audio_buf, frame_bytes, NULL);
        -:  956:
        -:  957:            //FIXME pass ost->sync_opts as AVFrame.pts in avcodec_encode_audio()
        -:  958:
    #####:  959:            ret = avcodec_encode_audio(enc, audio_out, audio_out_size,
        -:  960:                                       (short *)audio_buf);
    #####:  961:            if (ret < 0) {
    #####:  962:                fprintf(stderr, "Audio encoding failed\n");
    #####:  963:                ffmpeg_exit(1);
        -:  964:            }
    #####:  965:            audio_size += ret;
    #####:  966:            pkt.stream_index= ost->index;
    #####:  967:            pkt.data= audio_out;
    #####:  968:            pkt.size= ret;
    #####:  969:            if(enc->coded_frame && enc->coded_frame->pts != AV_NOPTS_VALUE)
    #####:  970:                pkt.pts= av_rescale_q(enc->coded_frame->pts, enc->time_base, ost->st->time_base);
    #####:  971:            pkt.flags |= AV_PKT_FLAG_KEY;
    #####:  972:            write_frame(s, &pkt, enc, ost->bitstream_filters);
        -:  973:
    #####:  974:            ost->sync_opts += enc->frame_size;
        -:  975:        }
        -:  976:    } else {
        -:  977:        AVPacket pkt;
    #####:  978:        av_init_packet(&pkt);
        -:  979:
    #####:  980:        ost->sync_opts += size_out / (osize * enc->channels);
        -:  981:
        -:  982:        /* output a pcm frame */
        -:  983:        /* determine the size of the coded buffer */
    #####:  984:        size_out /= osize;
    #####:  985:        if (coded_bps)
    #####:  986:            size_out = size_out*coded_bps/8;
        -:  987:
    #####:  988:        if(size_out > audio_out_size){
    #####:  989:            fprintf(stderr, "Internal error, buffer size too small\n");
    #####:  990:            ffmpeg_exit(1);
        -:  991:        }
        -:  992:
        -:  993:        //FIXME pass ost->sync_opts as AVFrame.pts in avcodec_encode_audio()
    #####:  994:        ret = avcodec_encode_audio(enc, audio_out, size_out,
        -:  995:                                   (short *)buftmp);
    #####:  996:        if (ret < 0) {
    #####:  997:            fprintf(stderr, "Audio encoding failed\n");
    #####:  998:            ffmpeg_exit(1);
        -:  999:        }
    #####: 1000:        audio_size += ret;
    #####: 1001:        pkt.stream_index= ost->index;
    #####: 1002:        pkt.data= audio_out;
    #####: 1003:        pkt.size= ret;
    #####: 1004:        if(enc->coded_frame && enc->coded_frame->pts != AV_NOPTS_VALUE)
    #####: 1005:            pkt.pts= av_rescale_q(enc->coded_frame->pts, enc->time_base, ost->st->time_base);
    #####: 1006:        pkt.flags |= AV_PKT_FLAG_KEY;
    #####: 1007:        write_frame(s, &pkt, enc, ost->bitstream_filters);
        -: 1008:    }
        -: 1009:}
        -: 1010:
        -: 1011:static void pre_process_video_frame(AVInputStream *ist, AVPicture *picture, void **bufp)
     7501: 1012:{
        -: 1013:    AVCodecContext *dec;
        -: 1014:    AVPicture *picture2;
        -: 1015:    AVPicture picture_tmp;
     7501: 1016:    uint8_t *buf = 0;
        -: 1017:
     7501: 1018:    dec = ist->st->codec;
        -: 1019:
        -: 1020:    /* deinterlace : must be done before any resize */
     7501: 1021:    if (do_deinterlace) {
        -: 1022:        int size;
        -: 1023:
        -: 1024:        /* create temporary picture */
    #####: 1025:        size = avpicture_get_size(dec->pix_fmt, dec->width, dec->height);
    #####: 1026:        buf = av_malloc(size);
    #####: 1027:        if (!buf)
    #####: 1028:            return;
        -: 1029:
    #####: 1030:        picture2 = &picture_tmp;
    #####: 1031:        avpicture_fill(picture2, buf, dec->pix_fmt, dec->width, dec->height);
        -: 1032:
    #####: 1033:        if(avpicture_deinterlace(picture2, picture,
        -: 1034:                                 dec->pix_fmt, dec->width, dec->height) < 0) {
        -: 1035:            /* if error, do not deinterlace */
    #####: 1036:            fprintf(stderr, "Deinterlacing failed\n");
    #####: 1037:            av_free(buf);
    #####: 1038:            buf = NULL;
    #####: 1039:            picture2 = picture;
        -: 1040:        }
        -: 1041:    } else {
     7501: 1042:        picture2 = picture;
        -: 1043:    }
        -: 1044:
     7501: 1045:    if (picture != picture2)
    #####: 1046:        *picture = *picture2;
     7501: 1047:    *bufp = buf;
        -: 1048:}
        -: 1049:
        -: 1050:/* we begin to correct av delay at this threshold */
        -: 1051:#define AV_DELAY_MAX 0.100
        -: 1052:
        -: 1053:static void do_subtitle_out(AVFormatContext *s,
        -: 1054:                            AVOutputStream *ost,
        -: 1055:                            AVInputStream *ist,
        -: 1056:                            AVSubtitle *sub,
        -: 1057:                            int64_t pts)
    #####: 1058:{
        -: 1059:    static uint8_t *subtitle_out = NULL;
    #####: 1060:    int subtitle_out_max_size = 1024 * 1024;
        -: 1061:    int subtitle_out_size, nb, i;
        -: 1062:    AVCodecContext *enc;
        -: 1063:    AVPacket pkt;
        -: 1064:
    #####: 1065:    if (pts == AV_NOPTS_VALUE) {
    #####: 1066:        fprintf(stderr, "Subtitle packets must have a pts\n");
    #####: 1067:        if (exit_on_error)
    #####: 1068:            ffmpeg_exit(1);
        -: 1069:        return;
        -: 1070:    }
        -: 1071:
    #####: 1072:    enc = ost->st->codec;
        -: 1073:
    #####: 1074:    if (!subtitle_out) {
    #####: 1075:        subtitle_out = av_malloc(subtitle_out_max_size);
        -: 1076:    }
        -: 1077:
        -: 1078:    /* Note: DVB subtitle need one packet to draw them and one other
        -: 1079:       packet to clear them */
        -: 1080:    /* XXX: signal it in the codec context ? */
    #####: 1081:    if (enc->codec_id == CODEC_ID_DVB_SUBTITLE)
    #####: 1082:        nb = 2;
        -: 1083:    else
    #####: 1084:        nb = 1;
        -: 1085:
    #####: 1086:    for(i = 0; i < nb; i++) {
    #####: 1087:        sub->pts = av_rescale_q(pts, ist->st->time_base, AV_TIME_BASE_Q);
        -: 1088:        // start_display_time is required to be 0
    #####: 1089:        sub->pts              += av_rescale_q(sub->start_display_time, (AVRational){1, 1000}, AV_TIME_BASE_Q);
    #####: 1090:        sub->end_display_time -= sub->start_display_time;
    #####: 1091:        sub->start_display_time = 0;
    #####: 1092:        subtitle_out_size = avcodec_encode_subtitle(enc, subtitle_out,
        -: 1093:                                                    subtitle_out_max_size, sub);
    #####: 1094:        if (subtitle_out_size < 0) {
    #####: 1095:            fprintf(stderr, "Subtitle encoding failed\n");
    #####: 1096:            ffmpeg_exit(1);
        -: 1097:        }
        -: 1098:
    #####: 1099:        av_init_packet(&pkt);
    #####: 1100:        pkt.stream_index = ost->index;
    #####: 1101:        pkt.data = subtitle_out;
    #####: 1102:        pkt.size = subtitle_out_size;
    #####: 1103:        pkt.pts = av_rescale_q(sub->pts, AV_TIME_BASE_Q, ost->st->time_base);
    #####: 1104:        if (enc->codec_id == CODEC_ID_DVB_SUBTITLE) {
        -: 1105:            /* XXX: the pts correction is handled here. Maybe handling
        -: 1106:               it in the codec would be better */
    #####: 1107:            if (i == 0)
    #####: 1108:                pkt.pts += 90 * sub->start_display_time;
        -: 1109:            else
    #####: 1110:                pkt.pts += 90 * sub->end_display_time;
        -: 1111:        }
    #####: 1112:        write_frame(s, &pkt, ost->st->codec, ost->bitstream_filters);
        -: 1113:    }
        -: 1114:}
        -: 1115:
        -: 1116:static int bit_buffer_size= 1024*256;
        -: 1117:static uint8_t *bit_buffer= NULL;
        -: 1118:
        -: 1119:static void do_video_out(AVFormatContext *s,
        -: 1120:                         AVOutputStream *ost,
        -: 1121:                         AVInputStream *ist,
        -: 1122:                         AVFrame *in_picture,
        -: 1123:                         int *frame_size)
    #####: 1124:{
        -: 1125:    int nb_frames, i, ret;
        -: 1126:    AVFrame *final_picture, *formatted_picture, *resampling_dst, *padding_src;
        -: 1127:    AVCodecContext *enc, *dec;
        -: 1128:    double sync_ipts;
        -: 1129:
    #####: 1130:    enc = ost->st->codec;
    #####: 1131:    dec = ist->st->codec;
        -: 1132:
    #####: 1133:    sync_ipts = get_sync_ipts(ost) / av_q2d(enc->time_base);
        -: 1134:
        -: 1135:    /* by default, we output a single frame */
    #####: 1136:    nb_frames = 1;
        -: 1137:
    #####: 1138:    *frame_size = 0;
        -: 1139:
    #####: 1140:    if(video_sync_method){
    #####: 1141:        double vdelta = sync_ipts - ost->sync_opts;
        -: 1142:        //FIXME set to 0.5 after we fix some dts/pts bugs like in avidec.c
    #####: 1143:        if (vdelta < -1.1)
    #####: 1144:            nb_frames = 0;
    #####: 1145:        else if (video_sync_method == 2 || (video_sync_method<0 && (s->oformat->flags & AVFMT_VARIABLE_FPS))){
    #####: 1146:            if(vdelta<=-0.6){
    #####: 1147:                nb_frames=0;
    #####: 1148:            }else if(vdelta>0.6)
    #####: 1149:                ost->sync_opts= lrintf(sync_ipts);
    #####: 1150:        }else if (vdelta > 1.1)
    #####: 1151:            nb_frames = lrintf(vdelta);
        -: 1152://fprintf(stderr, "vdelta:%f, ost->sync_opts:%"PRId64", ost->sync_ipts:%f nb_frames:%d\n", vdelta, ost->sync_opts, get_sync_ipts(ost), nb_frames);
    #####: 1153:        if (nb_frames == 0){
    #####: 1154:            ++nb_frames_drop;
    #####: 1155:            if (verbose>2)
    #####: 1156:                fprintf(stderr, "*** drop!\n");
    #####: 1157:        }else if (nb_frames > 1) {
    #####: 1158:            nb_frames_dup += nb_frames - 1;
    #####: 1159:            if (verbose>2)
    #####: 1160:                fprintf(stderr, "*** %d dup!\n", nb_frames-1);
        -: 1161:        }
        -: 1162:    }else
    #####: 1163:        ost->sync_opts= lrintf(sync_ipts);
        -: 1164:
    #####: 1165:    nb_frames= FFMIN(nb_frames, max_frames[AVMEDIA_TYPE_VIDEO] - ost->frame_number);
    #####: 1166:    if (nb_frames <= 0)
    #####: 1167:        return;
        -: 1168:
    #####: 1169:    formatted_picture = in_picture;
    #####: 1170:    final_picture = formatted_picture;
    #####: 1171:    padding_src = formatted_picture;
    #####: 1172:    resampling_dst = &ost->pict_tmp;
        -: 1173:
    #####: 1174:    if (   ost->resample_height != ist->st->codec->height
        -: 1175:        || ost->resample_width  != ist->st->codec->width
        -: 1176:        || (ost->resample_pix_fmt!= ist->st->codec->pix_fmt) ) {
        -: 1177:
    #####: 1178:        fprintf(stderr,"Input Stream #%d.%d frame size changed to %dx%d, %s\n", ist->file_index, ist->index, ist->st->codec->width,     ist->st->codec->height,avcodec_get_pix_fmt_name(ist->st->codec->pix_fmt));
    #####: 1179:        if(!ost->video_resample)
    #####: 1180:            ffmpeg_exit(1);
        -: 1181:    }
        -: 1182:
        -: 1183:#if !CONFIG_AVFILTER
        -: 1184:    if (ost->video_resample) {
        -: 1185:        padding_src = NULL;
        -: 1186:        final_picture = &ost->pict_tmp;
        -: 1187:        if(  ost->resample_height != ist->st->codec->height
        -: 1188:          || ost->resample_width  != ist->st->codec->width
        -: 1189:          || (ost->resample_pix_fmt!= ist->st->codec->pix_fmt) ) {
        -: 1190:
        -: 1191:            /* initialize a new scaler context */
        -: 1192:            sws_freeContext(ost->img_resample_ctx);
        -: 1193:            sws_flags = av_get_int(sws_opts, "sws_flags", NULL);
        -: 1194:            ost->img_resample_ctx = sws_getContext(
        -: 1195:                ist->st->codec->width,
        -: 1196:                ist->st->codec->height,
        -: 1197:                ist->st->codec->pix_fmt,
        -: 1198:                ost->st->codec->width,
        -: 1199:                ost->st->codec->height,
        -: 1200:                ost->st->codec->pix_fmt,
        -: 1201:                sws_flags, NULL, NULL, NULL);
        -: 1202:            if (ost->img_resample_ctx == NULL) {
        -: 1203:                fprintf(stderr, "Cannot get resampling context\n");
        -: 1204:                ffmpeg_exit(1);
        -: 1205:            }
        -: 1206:        }
        -: 1207:        sws_scale(ost->img_resample_ctx, formatted_picture->data, formatted_picture->linesize,
        -: 1208:              0, ost->resample_height, resampling_dst->data, resampling_dst->linesize);
        -: 1209:    }
        -: 1210:#endif
        -: 1211:
        -: 1212:    /* duplicates frame if needed */
    #####: 1213:    for(i=0;i<nb_frames;i++) {
        -: 1214:        AVPacket pkt;
    #####: 1215:        av_init_packet(&pkt);
    #####: 1216:        pkt.stream_index= ost->index;
        -: 1217:
    #####: 1218:        if (s->oformat->flags & AVFMT_RAWPICTURE) {
        -: 1219:            /* raw pictures are written as AVPicture structure to
        -: 1220:               avoid any copies. We support temorarily the older
        -: 1221:               method. */
    #####: 1222:            AVFrame* old_frame = enc->coded_frame;
    #####: 1223:            enc->coded_frame = dec->coded_frame; //FIXME/XXX remove this hack
    #####: 1224:            pkt.data= (uint8_t *)final_picture;
    #####: 1225:            pkt.size=  sizeof(AVPicture);
    #####: 1226:            pkt.pts= av_rescale_q(ost->sync_opts, enc->time_base, ost->st->time_base);
    #####: 1227:            pkt.flags |= AV_PKT_FLAG_KEY;
        -: 1228:
    #####: 1229:            write_frame(s, &pkt, ost->st->codec, ost->bitstream_filters);
    #####: 1230:            enc->coded_frame = old_frame;
        -: 1231:        } else {
        -: 1232:            AVFrame big_picture;
        -: 1233:
    #####: 1234:            big_picture= *final_picture;
        -: 1235:            /* better than nothing: use input picture interlaced
        -: 1236:               settings */
    #####: 1237:            big_picture.interlaced_frame = in_picture->interlaced_frame;
    #####: 1238:            if(avcodec_opts[AVMEDIA_TYPE_VIDEO]->flags & (CODEC_FLAG_INTERLACED_DCT|CODEC_FLAG_INTERLACED_ME)){
    #####: 1239:                if(top_field_first == -1)
    #####: 1240:                    big_picture.top_field_first = in_picture->top_field_first;
        -: 1241:                else
    #####: 1242:                    big_picture.top_field_first = top_field_first;
        -: 1243:            }
        -: 1244:
        -: 1245:            /* handles sameq here. This is not correct because it may
        -: 1246:               not be a global option */
    #####: 1247:            big_picture.quality = same_quality ? ist->st->quality : ost->st->quality;
    #####: 1248:            if(!me_threshold)
    #####: 1249:                big_picture.pict_type = 0;
        -: 1250://            big_picture.pts = AV_NOPTS_VALUE;
    #####: 1251:            big_picture.pts= ost->sync_opts;
        -: 1252://            big_picture.pts= av_rescale(ost->sync_opts, AV_TIME_BASE*(int64_t)enc->time_base.num, enc->time_base.den);
        -: 1253://av_log(NULL, AV_LOG_DEBUG, "%"PRId64" -> encoder\n", ost->sync_opts);
    #####: 1254:            if (ost->forced_kf_index < ost->forced_kf_count &&
        -: 1255:                big_picture.pts >= ost->forced_kf_pts[ost->forced_kf_index]) {
    #####: 1256:                big_picture.pict_type = FF_I_TYPE;
    #####: 1257:                ost->forced_kf_index++;
        -: 1258:            }
    #####: 1259:            ret = avcodec_encode_video(enc,
        -: 1260:                                       bit_buffer, bit_buffer_size,
        -: 1261:                                       &big_picture);
    #####: 1262:            if (ret < 0) {
    #####: 1263:                fprintf(stderr, "Video encoding failed\n");
    #####: 1264:                ffmpeg_exit(1);
        -: 1265:            }
        -: 1266:
    #####: 1267:            if(ret>0){
    #####: 1268:                pkt.data= bit_buffer;
    #####: 1269:                pkt.size= ret;
    #####: 1270:                if(enc->coded_frame->pts != AV_NOPTS_VALUE)
    #####: 1271:                    pkt.pts= av_rescale_q(enc->coded_frame->pts, enc->time_base, ost->st->time_base);
        -: 1272:/*av_log(NULL, AV_LOG_DEBUG, "encoder -> %"PRId64"/%"PRId64"\n",
        -: 1273:   pkt.pts != AV_NOPTS_VALUE ? av_rescale(pkt.pts, enc->time_base.den, AV_TIME_BASE*(int64_t)enc->time_base.num) : -1,
        -: 1274:   pkt.dts != AV_NOPTS_VALUE ? av_rescale(pkt.dts, enc->time_base.den, AV_TIME_BASE*(int64_t)enc->time_base.num) : -1);*/
        -: 1275:
    #####: 1276:                if(enc->coded_frame->key_frame)
    #####: 1277:                    pkt.flags |= AV_PKT_FLAG_KEY;
    #####: 1278:                write_frame(s, &pkt, ost->st->codec, ost->bitstream_filters);
    #####: 1279:                *frame_size = ret;
    #####: 1280:                video_size += ret;
        -: 1281:                //fprintf(stderr,"\nFrame: %3d size: %5d type: %d",
        -: 1282:                //        enc->frame_number-1, ret, enc->pict_type);
        -: 1283:                /* if two pass, output log */
    #####: 1284:                if (ost->logfile && enc->stats_out) {
    #####: 1285:                    fprintf(ost->logfile, "%s", enc->stats_out);
        -: 1286:                }
        -: 1287:            }
        -: 1288:        }
    #####: 1289:        ost->sync_opts++;
    #####: 1290:        ost->frame_number++;
        -: 1291:    }
        -: 1292:}
        -: 1293:
    #####: 1294:static double psnr(double d){
    #####: 1295:    return -10.0*log(d)/log(10.0);
        -: 1296:}
        -: 1297:
        -: 1298:static void do_video_stats(AVFormatContext *os, AVOutputStream *ost,
        -: 1299:                           int frame_size)
    #####: 1300:{
        -: 1301:    AVCodecContext *enc;
        -: 1302:    int frame_number;
        -: 1303:    double ti1, bitrate, avg_bitrate;
        -: 1304:
        -: 1305:    /* this is executed just the first time do_video_stats is called */
    #####: 1306:    if (!vstats_file) {
    #####: 1307:        vstats_file = fopen(vstats_filename, "w");
    #####: 1308:        if (!vstats_file) {
    #####: 1309:            perror("fopen");
    #####: 1310:            ffmpeg_exit(1);
        -: 1311:        }
        -: 1312:    }
        -: 1313:
    #####: 1314:    enc = ost->st->codec;
    #####: 1315:    if (enc->codec_type == AVMEDIA_TYPE_VIDEO) {
    #####: 1316:        frame_number = ost->frame_number;
    #####: 1317:        fprintf(vstats_file, "frame= %5d q= %2.1f ", frame_number, enc->coded_frame->quality/(float)FF_QP2LAMBDA);
    #####: 1318:        if (enc->flags&CODEC_FLAG_PSNR)
    #####: 1319:            fprintf(vstats_file, "PSNR= %6.2f ", psnr(enc->coded_frame->error[0]/(enc->width*enc->height*255.0*255.0)));
        -: 1320:
    #####: 1321:        fprintf(vstats_file,"f_size= %6d ", frame_size);
        -: 1322:        /* compute pts value */
    #####: 1323:        ti1 = ost->sync_opts * av_q2d(enc->time_base);
    #####: 1324:        if (ti1 < 0.01)
    #####: 1325:            ti1 = 0.01;
        -: 1326:
    #####: 1327:        bitrate = (frame_size * 8) / av_q2d(enc->time_base) / 1000.0;
    #####: 1328:        avg_bitrate = (double)(video_size * 8) / ti1 / 1000.0;
    #####: 1329:        fprintf(vstats_file, "s_size= %8.0fkB time= %0.3f br= %7.1fkbits/s avg_br= %7.1fkbits/s ",
        -: 1330:            (double)video_size / 1024, ti1, bitrate, avg_bitrate);
    #####: 1331:        fprintf(vstats_file,"type= %c\n", av_get_pict_type_char(enc->coded_frame->pict_type));
        -: 1332:    }
    #####: 1333:}
        -: 1334:
        -: 1335:static void print_report(AVFormatContext **output_files,
        -: 1336:                         AVOutputStream **ost_table, int nb_ostreams,
        -: 1337:                         int is_last_report)
    21551: 1338:{
        -: 1339:    char buf[1024];
        -: 1340:    AVOutputStream *ost;
        -: 1341:    AVFormatContext *oc;
        -: 1342:    int64_t total_size;
        -: 1343:    AVCodecContext *enc;
        -: 1344:    int frame_number, vid, i;
        -: 1345:    double bitrate, ti1, pts;
        -: 1346:    static int64_t last_time = -1;
        -: 1347:    static int qp_histogram[52];
        -: 1348:
    21551: 1349:    if (!is_last_report) {
        -: 1350:        int64_t cur_time;
        -: 1351:        /* display the report every 0.5 seconds */
    21550: 1352:        cur_time = av_gettime();
    21550: 1353:        if (last_time == -1) {
        1: 1354:            last_time = cur_time;
        1: 1355:            return;
        -: 1356:        }
    21549: 1357:        if ((cur_time - last_time) < 500000)
    21548: 1358:            return;
        1: 1359:        last_time = cur_time;
        -: 1360:    }
        -: 1361:
        -: 1362:
        2: 1363:    oc = output_files[0];
        -: 1364:
        2: 1365:    total_size = url_fsize(oc->pb);
        2: 1366:    if(total_size<0) // FIXME improve url_fsize() so it works with non seekable output too
    #####: 1367:        total_size= url_ftell(oc->pb);
        -: 1368:
        2: 1369:    buf[0] = '\0';
        2: 1370:    ti1 = 1e10;
        2: 1371:    vid = 0;
        6: 1372:    for(i=0;i<nb_ostreams;i++) {
        4: 1373:        ost = ost_table[i];
        4: 1374:        enc = ost->st->codec;
        4: 1375:        if (vid && enc->codec_type == AVMEDIA_TYPE_VIDEO) {
    #####: 1376:            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "q=%2.1f ",
        -: 1377:                     !ost->st->stream_copy ?
        -: 1378:                     enc->coded_frame->quality/(float)FF_QP2LAMBDA : -1);
        -: 1379:        }
        4: 1380:        if (!vid && enc->codec_type == AVMEDIA_TYPE_VIDEO) {
        2: 1381:            float t = (av_gettime()-timer_start) / 1000000.0;
        -: 1382:
        2: 1383:            frame_number = ost->frame_number;
        2: 1384:            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "frame=%5d fps=%3d q=%3.1f ",
        -: 1385:                     frame_number, (t>1)?(int)(frame_number/t+0.5) : 0,
        -: 1386:                     !ost->st->stream_copy ?
        -: 1387:                     enc->coded_frame->quality/(float)FF_QP2LAMBDA : -1);
        2: 1388:            if(is_last_report)
        1: 1389:                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "L");
        2: 1390:            if(qp_hist){
        -: 1391:                int j;
    #####: 1392:                int qp= lrintf(enc->coded_frame->quality/(float)FF_QP2LAMBDA);
    #####: 1393:                if(qp>=0 && qp<FF_ARRAY_ELEMS(qp_histogram))
    #####: 1394:                    qp_histogram[qp]++;
    #####: 1395:                for(j=0; j<32; j++)
    #####: 1396:                    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%X", (int)lrintf(log(qp_histogram[j]+1)/log(2)));
        -: 1397:            }
        2: 1398:            if (enc->flags&CODEC_FLAG_PSNR){
        -: 1399:                int j;
    #####: 1400:                double error, error_sum=0;
    #####: 1401:                double scale, scale_sum=0;
    #####: 1402:                char type[3]= {'Y','U','V'};
    #####: 1403:                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "PSNR=");
    #####: 1404:                for(j=0; j<3; j++){
    #####: 1405:                    if(is_last_report){
    #####: 1406:                        error= enc->error[j];
    #####: 1407:                        scale= enc->width*enc->height*255.0*255.0*frame_number;
        -: 1408:                    }else{
    #####: 1409:                        error= enc->coded_frame->error[j];
    #####: 1410:                        scale= enc->width*enc->height*255.0*255.0;
        -: 1411:                    }
    #####: 1412:                    if(j) scale/=4;
    #####: 1413:                    error_sum += error;
    #####: 1414:                    scale_sum += scale;
    #####: 1415:                    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%c:%2.2f ", type[j], psnr(error/scale));
        -: 1416:                }
    #####: 1417:                snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "*:%2.2f ", psnr(error_sum/scale_sum));
        -: 1418:            }
        2: 1419:            vid = 1;
        -: 1420:        }
        -: 1421:        /* compute min output value */
        8: 1422:        pts = (double)ost->st->pts.val * av_q2d(ost->st->time_base);
        4: 1423:        if ((pts < ti1) && (pts > 0))
        4: 1424:            ti1 = pts;
        -: 1425:    }
        2: 1426:    if (ti1 < 0.01)
    #####: 1427:        ti1 = 0.01;
        -: 1428:
        2: 1429:    if (verbose || is_last_report) {
        2: 1430:        bitrate = (double)(total_size * 8) / ti1 / 1000.0;
        -: 1431:
        2: 1432:        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
        -: 1433:            "size=%8.0fkB time=%0.2f bitrate=%6.1fkbits/s",
        -: 1434:            (double)total_size / 1024, ti1, bitrate);
        -: 1435:
        2: 1436:        if (nb_frames_dup || nb_frames_drop)
    #####: 1437:          snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " dup=%d drop=%d",
        -: 1438:                  nb_frames_dup, nb_frames_drop);
        -: 1439:
        2: 1440:        if (verbose >= 0)
        2: 1441:            fprintf(stderr, "%s    \r", buf);
        -: 1442:
        2: 1443:        fflush(stderr);
        -: 1444:    }
        -: 1445:
        2: 1446:    if (is_last_report && verbose >= 0){
        1: 1447:        int64_t raw= audio_size + video_size + extra_size;
        1: 1448:        fprintf(stderr, "\n");
        1: 1449:        fprintf(stderr, "video:%1.0fkB audio:%1.0fkB global headers:%1.0fkB muxing overhead %f%%\n",
        -: 1450:                video_size/1024.0,
        -: 1451:                audio_size/1024.0,
        -: 1452:                extra_size/1024.0,
        -: 1453:                100.0*(total_size - raw)/raw
        -: 1454:        );
        -: 1455:    }
        -: 1456:}
        -: 1457:
        -: 1458:/* pkt = NULL means EOF (needed to flush decoder buffers) */
        -: 1459:static int output_packet(AVInputStream *ist, int ist_index,
        -: 1460:                         AVOutputStream **ost_table, int nb_ostreams,
        -: 1461:                         const AVPacket *pkt)
    21550: 1462:{
        -: 1463:    AVFormatContext *os;
        -: 1464:    AVOutputStream *ost;
        -: 1465:    int ret, i;
        -: 1466:    int got_picture;
        -: 1467:    AVFrame picture;
        -: 1468:    void *buffer_to_free;
        -: 1469:    static unsigned int samples_size= 0;
        -: 1470:    AVSubtitle subtitle, *subtitle_to_free;
    21550: 1471:    int64_t pkt_pts = AV_NOPTS_VALUE;
        -: 1472:#if CONFIG_AVFILTER
        -: 1473:    int frame_available;
        -: 1474:#endif
        -: 1475:
        -: 1476:    AVPacket avpkt;
    21550: 1477:    int bps = av_get_bits_per_sample_fmt(ist->st->codec->sample_fmt)>>3;
        -: 1478:
    21550: 1479:    if(ist->next_pts == AV_NOPTS_VALUE)
        2: 1480:        ist->next_pts= ist->pts;
        -: 1481:
    21550: 1482:    if (pkt == NULL) {
        -: 1483:        /* EOF handling */
    #####: 1484:        av_init_packet(&avpkt);
    #####: 1485:        avpkt.data = NULL;
    #####: 1486:        avpkt.size = 0;
    #####: 1487:        goto handle_eof;
        -: 1488:    } else {
    21550: 1489:        avpkt = *pkt;
        -: 1490:    }
        -: 1491:
    21550: 1492:    if(pkt->dts != AV_NOPTS_VALUE)
    21550: 1493:        ist->next_pts = ist->pts = av_rescale_q(pkt->dts, ist->st->time_base, AV_TIME_BASE_Q);
    21550: 1494:    if(pkt->pts != AV_NOPTS_VALUE)
    21550: 1495:        pkt_pts = av_rescale_q(pkt->pts, ist->st->time_base, AV_TIME_BASE_Q);
        -: 1496:
        -: 1497:    //while we have more to decode or while the decoder did output something on EOF
    43100: 1498:    while (avpkt.size > 0 || (!pkt && ist->next_pts != ist->pts)) {
        -: 1499:        uint8_t *data_buf, *decoded_data_buf;
        -: 1500:        int data_size, decoded_data_size;
    21550: 1501:    handle_eof:
    21550: 1502:        ist->pts= ist->next_pts;
        -: 1503:
    21550: 1504:        if(avpkt.size && avpkt.size != pkt->size &&
        -: 1505:           ((!ist->showed_multi_packet_warning && verbose>0) || verbose>1)){
    #####: 1506:            fprintf(stderr, "Multiple frames in a packet from stream %d\n", pkt->stream_index);
    #####: 1507:            ist->showed_multi_packet_warning=1;
        -: 1508:        }
        -: 1509:
        -: 1510:        /* decode the packet if needed */
    21550: 1511:        decoded_data_buf = NULL; /* fail safe */
    21550: 1512:        decoded_data_size= 0;
    21550: 1513:        data_buf  = avpkt.data;
    21550: 1514:        data_size = avpkt.size;
    21550: 1515:        subtitle_to_free = NULL;
    21550: 1516:        if (ist->decoding_needed) {
    #####: 1517:            switch(ist->st->codec->codec_type) {
        -: 1518:            case AVMEDIA_TYPE_AUDIO:{
    #####: 1519:                if(pkt && samples_size < FFMAX(pkt->size*sizeof(*samples), AVCODEC_MAX_AUDIO_FRAME_SIZE)) {
    #####: 1520:                    samples_size = FFMAX(pkt->size*sizeof(*samples), AVCODEC_MAX_AUDIO_FRAME_SIZE);
    #####: 1521:                    av_free(samples);
    #####: 1522:                    samples= av_malloc(samples_size);
        -: 1523:                }
    #####: 1524:                decoded_data_size= samples_size;
        -: 1525:                    /* XXX: could avoid copy if PCM 16 bits with same
        -: 1526:                       endianness as CPU */
    #####: 1527:                ret = avcodec_decode_audio3(ist->st->codec, samples, &decoded_data_size,
        -: 1528:                                            &avpkt);
    #####: 1529:                if (ret < 0)
    #####: 1530:                    goto fail_decode;
    #####: 1531:                avpkt.data += ret;
    #####: 1532:                avpkt.size -= ret;
    #####: 1533:                data_size   = ret;
        -: 1534:                /* Some bug in mpeg audio decoder gives */
        -: 1535:                /* decoded_data_size < 0, it seems they are overflows */
    #####: 1536:                if (decoded_data_size <= 0) {
        -: 1537:                    /* no audio frame */
    #####: 1538:                    continue;
        -: 1539:                }
    #####: 1540:                decoded_data_buf = (uint8_t *)samples;
    #####: 1541:                ist->next_pts += ((int64_t)AV_TIME_BASE/bps * decoded_data_size) /
        -: 1542:                    (ist->st->codec->sample_rate * ist->st->codec->channels);
    #####: 1543:                break;}
        -: 1544:            case AVMEDIA_TYPE_VIDEO:
    #####: 1545:                    decoded_data_size = (ist->st->codec->width * ist->st->codec->height * 3) / 2;
        -: 1546:                    /* XXX: allocate picture correctly */
    #####: 1547:                    avcodec_get_frame_defaults(&picture);
    #####: 1548:                    ist->st->codec->reordered_opaque = pkt_pts;
    #####: 1549:                    pkt_pts = AV_NOPTS_VALUE;
        -: 1550:
    #####: 1551:                    ret = avcodec_decode_video2(ist->st->codec,
        -: 1552:                                                &picture, &got_picture, &avpkt);
    #####: 1553:                    ist->st->quality= picture.quality;
    #####: 1554:                    if (ret < 0)
    #####: 1555:                        goto fail_decode;
    #####: 1556:                    if (!got_picture) {
        -: 1557:                        /* no picture yet */
    #####: 1558:                        goto discard_packet;
        -: 1559:                    }
    #####: 1560:                    ist->next_pts = ist->pts = guess_correct_pts(&ist->pts_ctx, picture.reordered_opaque, ist->pts);
    #####: 1561:                    if (ist->st->codec->time_base.num != 0) {
    #####: 1562:                        int ticks= ist->st->parser ? ist->st->parser->repeat_pict+1 : ist->st->codec->ticks_per_frame;
    #####: 1563:                        ist->next_pts += ((int64_t)AV_TIME_BASE *
        -: 1564:                                          ist->st->codec->time_base.num * ticks) /
        -: 1565:                            ist->st->codec->time_base.den;
        -: 1566:                    }
    #####: 1567:                    avpkt.size = 0;
    #####: 1568:                    break;
        -: 1569:            case AVMEDIA_TYPE_SUBTITLE:
    #####: 1570:                ret = avcodec_decode_subtitle2(ist->st->codec,
        -: 1571:                                               &subtitle, &got_picture, &avpkt);
    #####: 1572:                if (ret < 0)
    #####: 1573:                    goto fail_decode;
    #####: 1574:                if (!got_picture) {
    #####: 1575:                    goto discard_packet;
        -: 1576:                }
    #####: 1577:                subtitle_to_free = &subtitle;
    #####: 1578:                avpkt.size = 0;
    #####: 1579:                break;
        -: 1580:            default:
        -: 1581:                goto fail_decode;
        -: 1582:            }
        -: 1583:        } else {
    21550: 1584:            switch(ist->st->codec->codec_type) {
        -: 1585:            case AVMEDIA_TYPE_AUDIO:
    14049: 1586:                ist->next_pts += ((int64_t)AV_TIME_BASE * ist->st->codec->frame_size) /
        -: 1587:                    ist->st->codec->sample_rate;
    14049: 1588:                break;
        -: 1589:            case AVMEDIA_TYPE_VIDEO:
     7501: 1590:                if (ist->st->codec->time_base.num != 0) {
     7501: 1591:                    int ticks= ist->st->parser ? ist->st->parser->repeat_pict+1 : ist->st->codec->ticks_per_frame;
     7501: 1592:                    ist->next_pts += ((int64_t)AV_TIME_BASE *
        -: 1593:                                      ist->st->codec->time_base.num * ticks) /
        -: 1594:                        ist->st->codec->time_base.den;
        -: 1595:                }
        -: 1596:                break;
        -: 1597:            }
    21550: 1598:            ret = avpkt.size;
    21550: 1599:            avpkt.size = 0;
        -: 1600:        }
        -: 1601:
    21550: 1602:        buffer_to_free = NULL;
    21550: 1603:        if (ist->st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
     7501: 1604:            pre_process_video_frame(ist, (AVPicture *)&picture,
        -: 1605:                                    &buffer_to_free);
        -: 1606:        }
        -: 1607:
        -: 1608:#if CONFIG_AVFILTER
    21550: 1609:        if (ist->st->codec->codec_type == AVMEDIA_TYPE_VIDEO && ist->input_video_filter) {
        -: 1610:            // add it to be filtered
    #####: 1611:            av_vsrc_buffer_add_frame(ist->input_video_filter, &picture,
        -: 1612:                                     ist->pts,
        -: 1613:                                     ist->st->codec->sample_aspect_ratio);
        -: 1614:        }
        -: 1615:#endif
        -: 1616:
        -: 1617:        // preprocess audio (volume)
    21550: 1618:        if (ist->st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
    14049: 1619:            if (audio_volume != 256) {
        -: 1620:                short *volp;
    #####: 1621:                volp = samples;
    #####: 1622:                for(i=0;i<(decoded_data_size / sizeof(short));i++) {
    #####: 1623:                    int v = ((*volp) * audio_volume + 128) >> 8;
    #####: 1624:                    if (v < -32768) v = -32768;
    #####: 1625:                    if (v >  32767) v = 32767;
    #####: 1626:                    *volp++ = v;
        -: 1627:                }
        -: 1628:            }
        -: 1629:        }
        -: 1630:
        -: 1631:        /* frame rate emulation */
    21550: 1632:        if (rate_emu) {
    #####: 1633:            int64_t pts = av_rescale(ist->pts, 1000000, AV_TIME_BASE);
    #####: 1634:            int64_t now = av_gettime() - ist->start;
    #####: 1635:            if (pts > now)
    #####: 1636:                usleep(pts - now);
        -: 1637:        }
        -: 1638:#if CONFIG_AVFILTER
    21550: 1639:        frame_available = ist->st->codec->codec_type != AVMEDIA_TYPE_VIDEO ||
        -: 1640:            !ist->output_video_filter || avfilter_poll_frame(ist->output_video_filter->inputs[0]);
        -: 1641:#endif
        -: 1642:        /* if output time reached then transcode raw format,
        -: 1643:           encode packets and output them */
    21550: 1644:        if (start_time == 0 || ist->pts >= start_time)
        -: 1645:#if CONFIG_AVFILTER
    43100: 1646:        while (frame_available) {
        -: 1647:            AVRational ist_pts_tb;
    21550: 1648:            if (ist->st->codec->codec_type == AVMEDIA_TYPE_VIDEO && ist->output_video_filter)
    #####: 1649:                get_filtered_video_frame(ist->output_video_filter, &picture, &ist->picref, &ist_pts_tb);
    21550: 1650:            if (ist->picref)
    #####: 1651:                ist->pts = av_rescale_q(ist->picref->pts, ist_pts_tb, AV_TIME_BASE_Q);
        -: 1652:#endif
    64650: 1653:            for(i=0;i<nb_ostreams;i++) {
        -: 1654:                int frame_size;
        -: 1655:
    43100: 1656:                ost = ost_table[i];
    43100: 1657:                if (ost->source_index == ist_index) {
    21550: 1658:                    os = output_files[ost->file_index];
        -: 1659:
        -: 1660:                    /* set the input output pts pairs */
        -: 1661:                    //ost->sync_ipts = (double)(ist->pts + input_files_ts_offset[ist->file_index] - start_time)/ AV_TIME_BASE;
        -: 1662:
    21550: 1663:                    if (ost->encoding_needed) {
    #####: 1664:                        av_assert0(ist->decoding_needed);
    #####: 1665:                        switch(ost->st->codec->codec_type) {
        -: 1666:                        case AVMEDIA_TYPE_AUDIO:
    #####: 1667:                            do_audio_out(os, ost, ist, decoded_data_buf, decoded_data_size);
    #####: 1668:                            break;
        -: 1669:                        case AVMEDIA_TYPE_VIDEO:
        -: 1670:#if CONFIG_AVFILTER
    #####: 1671:                            if (ist->picref->video)
    #####: 1672:                                ost->st->codec->sample_aspect_ratio = ist->picref->video->pixel_aspect;
        -: 1673:#endif
    #####: 1674:                            do_video_out(os, ost, ist, &picture, &frame_size);
    #####: 1675:                            if (vstats_filename && frame_size)
    #####: 1676:                                do_video_stats(os, ost, frame_size);
        -: 1677:                            break;
        -: 1678:                        case AVMEDIA_TYPE_SUBTITLE:
    #####: 1679:                            do_subtitle_out(os, ost, ist, &subtitle,
        -: 1680:                                            pkt->pts);
    #####: 1681:                            break;
        -: 1682:                        default:
    #####: 1683:                            abort();
        -: 1684:                        }
        -: 1685:                    } else {
        -: 1686:                        AVFrame avframe; //FIXME/XXX remove this
        -: 1687:                        AVPacket opkt;
    21550: 1688:                        int64_t ost_tb_start_time= av_rescale_q(start_time, AV_TIME_BASE_Q, ost->st->time_base);
        -: 1689:
    21550: 1690:                        av_init_packet(&opkt);
        -: 1691:
    21550: 1692:                        if ((!ost->frame_number && !(pkt->flags & AV_PKT_FLAG_KEY)) && !copy_initial_nonkeyframes)
    #####: 1693:                            continue;
        -: 1694:
        -: 1695:                        /* no reencoding needed : output the packet directly */
        -: 1696:                        /* force the input stream PTS */
        -: 1697:
    21550: 1698:                        avcodec_get_frame_defaults(&avframe);
    21550: 1699:                        ost->st->codec->coded_frame= &avframe;
    21550: 1700:                        avframe.key_frame = pkt->flags & AV_PKT_FLAG_KEY;
        -: 1701:
    21550: 1702:                        if(ost->st->codec->codec_type == AVMEDIA_TYPE_AUDIO)
    14049: 1703:                            audio_size += data_size;
     7501: 1704:                        else if (ost->st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
     7501: 1705:                            video_size += data_size;
     7501: 1706:                            ost->sync_opts++;
        -: 1707:                        }
        -: 1708:
    21550: 1709:                        opkt.stream_index= ost->index;
    21550: 1710:                        if(pkt->pts != AV_NOPTS_VALUE)
    21550: 1711:                            opkt.pts= av_rescale_q(pkt->pts, ist->st->time_base, ost->st->time_base) - ost_tb_start_time;
        -: 1712:                        else
    #####: 1713:                            opkt.pts= AV_NOPTS_VALUE;
        -: 1714:
    21550: 1715:                        if (pkt->dts == AV_NOPTS_VALUE)
    #####: 1716:                            opkt.dts = av_rescale_q(ist->pts, AV_TIME_BASE_Q, ost->st->time_base);
        -: 1717:                        else
    21550: 1718:                            opkt.dts = av_rescale_q(pkt->dts, ist->st->time_base, ost->st->time_base);
    21550: 1719:                        opkt.dts -= ost_tb_start_time;
        -: 1720:
    21550: 1721:                        opkt.duration = av_rescale_q(pkt->duration, ist->st->time_base, ost->st->time_base);
    21550: 1722:                        opkt.flags= pkt->flags;
        -: 1723:
        -: 1724:                        //FIXME remove the following 2 lines they shall be replaced by the bitstream filters
    21550: 1725:                        if(   ost->st->codec->codec_id != CODEC_ID_H264
        -: 1726:                           && ost->st->codec->codec_id != CODEC_ID_MPEG1VIDEO
        -: 1727:                           && ost->st->codec->codec_id != CODEC_ID_MPEG2VIDEO
        -: 1728:                           ) {
    14049: 1729:                            if(av_parser_change(ist->st->parser, ost->st->codec, &opkt.data, &opkt.size, data_buf, data_size, pkt->flags & AV_PKT_FLAG_KEY))
    #####: 1730:                                opkt.destruct= av_destruct_packet;
        -: 1731:                        } else {
     7501: 1732:                            opkt.data = data_buf;
     7501: 1733:                            opkt.size = data_size;
        -: 1734:                        }
        -: 1735:
    21550: 1736:                        write_frame(os, &opkt, ost->st->codec, ost->bitstream_filters);
    21550: 1737:                        ost->st->codec->frame_number++;
    21550: 1738:                        ost->frame_number++;
    21550: 1739:                        av_free_packet(&opkt);
        -: 1740:                    }
        -: 1741:                }
        -: 1742:            }
        -: 1743:
        -: 1744:#if CONFIG_AVFILTER
    21550: 1745:            frame_available = (ist->st->codec->codec_type == AVMEDIA_TYPE_VIDEO) &&
        -: 1746:                              ist->output_video_filter && avfilter_poll_frame(ist->output_video_filter->inputs[0]);
    21550: 1747:            if(ist->picref)
    #####: 1748:                avfilter_unref_buffer(ist->picref);
        -: 1749:        }
        -: 1750:#endif
    21550: 1751:        av_free(buffer_to_free);
        -: 1752:        /* XXX: allocate the subtitles in the codec ? */
    21550: 1753:        if (subtitle_to_free) {
    #####: 1754:            avsubtitle_free(subtitle_to_free);
    #####: 1755:            subtitle_to_free = NULL;
        -: 1756:        }
        -: 1757:    }
    21550: 1758: discard_packet:
    21550: 1759:    if (pkt == NULL) {
        -: 1760:        /* EOF handling */
        -: 1761:
    #####: 1762:        for(i=0;i<nb_ostreams;i++) {
    #####: 1763:            ost = ost_table[i];
    #####: 1764:            if (ost->source_index == ist_index) {
    #####: 1765:                AVCodecContext *enc= ost->st->codec;
    #####: 1766:                os = output_files[ost->file_index];
        -: 1767:
    #####: 1768:                if(ost->st->codec->codec_type == AVMEDIA_TYPE_AUDIO && enc->frame_size <=1)
    #####: 1769:                    continue;
    #####: 1770:                if(ost->st->codec->codec_type == AVMEDIA_TYPE_VIDEO && (os->oformat->flags & AVFMT_RAWPICTURE))
    #####: 1771:                    continue;
        -: 1772:
    #####: 1773:                if (ost->encoding_needed) {
        -: 1774:                    for(;;) {
        -: 1775:                        AVPacket pkt;
        -: 1776:                        int fifo_bytes;
    #####: 1777:                        av_init_packet(&pkt);
    #####: 1778:                        pkt.stream_index= ost->index;
        -: 1779:
    #####: 1780:                        switch(ost->st->codec->codec_type) {
        -: 1781:                        case AVMEDIA_TYPE_AUDIO:
    #####: 1782:                            fifo_bytes = av_fifo_size(ost->fifo);
    #####: 1783:                            ret = 0;
        -: 1784:                            /* encode any samples remaining in fifo */
    #####: 1785:                            if (fifo_bytes > 0) {
    #####: 1786:                                int osize = av_get_bits_per_sample_fmt(enc->sample_fmt) >> 3;
    #####: 1787:                                int fs_tmp = enc->frame_size;
        -: 1788:
    #####: 1789:                                av_fifo_generic_read(ost->fifo, audio_buf, fifo_bytes, NULL);
    #####: 1790:                                if (enc->codec->capabilities & CODEC_CAP_SMALL_LAST_FRAME) {
    #####: 1791:                                    enc->frame_size = fifo_bytes / (osize * enc->channels);
        -: 1792:                                } else { /* pad */
    #####: 1793:                                    int frame_bytes = enc->frame_size*osize*enc->channels;
    #####: 1794:                                    if (allocated_audio_buf_size < frame_bytes)
    #####: 1795:                                        ffmpeg_exit(1);
    #####: 1796:                                    memset(audio_buf+fifo_bytes, 0, frame_bytes - fifo_bytes);
        -: 1797:                                }
        -: 1798:
    #####: 1799:                                ret = avcodec_encode_audio(enc, bit_buffer, bit_buffer_size, (short *)audio_buf);
    #####: 1800:                                pkt.duration = av_rescale((int64_t)enc->frame_size*ost->st->time_base.den,
        -: 1801:                                                          ost->st->time_base.num, enc->sample_rate);
    #####: 1802:                                enc->frame_size = fs_tmp;
        -: 1803:                            }
    #####: 1804:                            if(ret <= 0) {
    #####: 1805:                                ret = avcodec_encode_audio(enc, bit_buffer, bit_buffer_size, NULL);
        -: 1806:                            }
    #####: 1807:                            if (ret < 0) {
    #####: 1808:                                fprintf(stderr, "Audio encoding failed\n");
    #####: 1809:                                ffmpeg_exit(1);
        -: 1810:                            }
    #####: 1811:                            audio_size += ret;
    #####: 1812:                            pkt.flags |= AV_PKT_FLAG_KEY;
    #####: 1813:                            break;
        -: 1814:                        case AVMEDIA_TYPE_VIDEO:
    #####: 1815:                            ret = avcodec_encode_video(enc, bit_buffer, bit_buffer_size, NULL);
    #####: 1816:                            if (ret < 0) {
    #####: 1817:                                fprintf(stderr, "Video encoding failed\n");
    #####: 1818:                                ffmpeg_exit(1);
        -: 1819:                            }
    #####: 1820:                            video_size += ret;
    #####: 1821:                            if(enc->coded_frame && enc->coded_frame->key_frame)
    #####: 1822:                                pkt.flags |= AV_PKT_FLAG_KEY;
    #####: 1823:                            if (ost->logfile && enc->stats_out) {
    #####: 1824:                                fprintf(ost->logfile, "%s", enc->stats_out);
        -: 1825:                            }
        -: 1826:                            break;
        -: 1827:                        default:
    #####: 1828:                            ret=-1;
        -: 1829:                        }
        -: 1830:
    #####: 1831:                        if(ret<=0)
    #####: 1832:                            break;
    #####: 1833:                        pkt.data= bit_buffer;
    #####: 1834:                        pkt.size= ret;
    #####: 1835:                        if(enc->coded_frame && enc->coded_frame->pts != AV_NOPTS_VALUE)
    #####: 1836:                            pkt.pts= av_rescale_q(enc->coded_frame->pts, enc->time_base, ost->st->time_base);
    #####: 1837:                        write_frame(os, &pkt, ost->st->codec, ost->bitstream_filters);
    #####: 1838:                    }
        -: 1839:                }
        -: 1840:            }
        -: 1841:        }
        -: 1842:    }
        -: 1843:
    21550: 1844:    return 0;
    #####: 1845: fail_decode:
    #####: 1846:    return -1;
        -: 1847:}
        -: 1848:
        -: 1849:static void print_sdp(AVFormatContext **avc, int n)
    #####: 1850:{
        -: 1851:    char sdp[2048];
        -: 1852:
    #####: 1853:    avf_sdp_create(avc, n, sdp, sizeof(sdp));
    #####: 1854:    printf("SDP:\n%s\n", sdp);
    #####: 1855:    fflush(stdout);
    #####: 1856:}
        -: 1857:
        -: 1858:static int copy_chapters(int infile, int outfile)
    #####: 1859:{
    #####: 1860:    AVFormatContext *is = input_files[infile];
    #####: 1861:    AVFormatContext *os = output_files[outfile];
        -: 1862:    int i;
        -: 1863:
    #####: 1864:    for (i = 0; i < is->nb_chapters; i++) {
    #####: 1865:        AVChapter *in_ch = is->chapters[i], *out_ch;
        -: 1866:        int64_t ts_off   = av_rescale_q(start_time - input_files_ts_offset[infile],
    #####: 1867:                                      AV_TIME_BASE_Q, in_ch->time_base);
        -: 1868:        int64_t rt       = (recording_time == INT64_MAX) ? INT64_MAX :
    #####: 1869:                           av_rescale_q(recording_time, AV_TIME_BASE_Q, in_ch->time_base);
        -: 1870:
        -: 1871:
    #####: 1872:        if (in_ch->end < ts_off)
    #####: 1873:            continue;
    #####: 1874:        if (rt != INT64_MAX && in_ch->start > rt + ts_off)
    #####: 1875:            break;
        -: 1876:
    #####: 1877:        out_ch = av_mallocz(sizeof(AVChapter));
    #####: 1878:        if (!out_ch)
    #####: 1879:            return AVERROR(ENOMEM);
        -: 1880:
    #####: 1881:        out_ch->id        = in_ch->id;
    #####: 1882:        out_ch->time_base = in_ch->time_base;
    #####: 1883:        out_ch->start     = FFMAX(0,  in_ch->start - ts_off);
    #####: 1884:        out_ch->end       = FFMIN(rt, in_ch->end   - ts_off);
        -: 1885:
    #####: 1886:        if (metadata_chapters_autocopy)
    #####: 1887:            av_metadata_copy(&out_ch->metadata, in_ch->metadata, 0);
        -: 1888:
    #####: 1889:        os->nb_chapters++;
    #####: 1890:        os->chapters = av_realloc(os->chapters, sizeof(AVChapter)*os->nb_chapters);
    #####: 1891:        if (!os->chapters)
    #####: 1892:            return AVERROR(ENOMEM);
    #####: 1893:        os->chapters[os->nb_chapters - 1] = out_ch;
        -: 1894:    }
    #####: 1895:    return 0;
        -: 1896:}
        -: 1897:
        -: 1898:static void parse_forced_key_frames(char *kf, AVOutputStream *ost,
        -: 1899:                                    AVCodecContext *avctx)
    #####: 1900:{
        -: 1901:    char *p;
    #####: 1902:    int n = 1, i;
        -: 1903:    int64_t t;
        -: 1904:
    #####: 1905:    for (p = kf; *p; p++)
    #####: 1906:        if (*p == ',')
    #####: 1907:            n++;
    #####: 1908:    ost->forced_kf_count = n;
    #####: 1909:    ost->forced_kf_pts = av_malloc(sizeof(*ost->forced_kf_pts) * n);
    #####: 1910:    if (!ost->forced_kf_pts) {
    #####: 1911:        av_log(NULL, AV_LOG_FATAL, "Could not allocate forced key frames array.\n");
    #####: 1912:        ffmpeg_exit(1);
        -: 1913:    }
    #####: 1914:    for (i = 0; i < n; i++) {
    #####: 1915:        p = i ? strchr(p, ',') + 1 : kf;
    #####: 1916:        t = parse_time_or_die("force_key_frames", p, 1);
    #####: 1917:        ost->forced_kf_pts[i] = av_rescale_q(t, AV_TIME_BASE_Q, avctx->time_base);
        -: 1918:    }
    #####: 1919:}
        -: 1920:
        -: 1921:/*
        -: 1922: * The following code is the main loop of the file converter
        -: 1923: */
        -: 1924:static int transcode(AVFormatContext **output_files,
        -: 1925:                     int nb_output_files,
        -: 1926:                     AVFormatContext **input_files,
        -: 1927:                     int nb_input_files,
        -: 1928:                     AVStreamMap *stream_maps, int nb_stream_maps)
        1: 1929:{
        1: 1930:    int ret = 0, i, j, k, n, nb_istreams = 0, nb_ostreams = 0;
        -: 1931:    AVFormatContext *is, *os;
        -: 1932:    AVCodecContext *codec, *icodec;
        1: 1933:    AVOutputStream *ost, **ost_table = NULL;
        1: 1934:    AVInputStream *ist, **ist_table = NULL;
        -: 1935:    AVInputFile *file_table;
        -: 1936:    char error[1024];
        -: 1937:    int key;
        1: 1938:    int want_sdp = 1;
        1: 1939:    uint8_t no_packet[MAX_FILES]={0};
        1: 1940:    int no_packet_count=0;
        -: 1941:
        1: 1942:    file_table= av_mallocz(nb_input_files * sizeof(AVInputFile));
        1: 1943:    if (!file_table)
    #####: 1944:        goto fail;
        -: 1945:
        -: 1946:    /* input stream init */
        1: 1947:    j = 0;
        2: 1948:    for(i=0;i<nb_input_files;i++) {
        1: 1949:        is = input_files[i];
        1: 1950:        file_table[i].ist_index = j;
        1: 1951:        file_table[i].nb_streams = is->nb_streams;
        1: 1952:        j += is->nb_streams;
        -: 1953:    }
        1: 1954:    nb_istreams = j;
        -: 1955:
        1: 1956:    ist_table = av_mallocz(nb_istreams * sizeof(AVInputStream *));
        1: 1957:    if (!ist_table)
    #####: 1958:        goto fail;
        -: 1959:
        3: 1960:    for(i=0;i<nb_istreams;i++) {
        2: 1961:        ist = av_mallocz(sizeof(AVInputStream));
        2: 1962:        if (!ist)
    #####: 1963:            goto fail;
        2: 1964:        ist_table[i] = ist;
        -: 1965:    }
        1: 1966:    j = 0;
        2: 1967:    for(i=0;i<nb_input_files;i++) {
        1: 1968:        is = input_files[i];
        3: 1969:        for(k=0;k<is->nb_streams;k++) {
        2: 1970:            ist = ist_table[j++];
        2: 1971:            ist->st = is->streams[k];
        2: 1972:            ist->file_index = i;
        2: 1973:            ist->index = k;
        2: 1974:            ist->discard = 1; /* the stream is discarded by default
        -: 1975:                                 (changed later) */
        -: 1976:
        2: 1977:            if (rate_emu) {
    #####: 1978:                ist->start = av_gettime();
        -: 1979:            }
        -: 1980:        }
        -: 1981:    }
        -: 1982:
        -: 1983:    /* output stream init */
        1: 1984:    nb_ostreams = 0;
        2: 1985:    for(i=0;i<nb_output_files;i++) {
        1: 1986:        os = output_files[i];
        1: 1987:        if (!os->nb_streams && !(os->oformat->flags & AVFMT_NOSTREAMS)) {
    #####: 1988:            dump_format(output_files[i], i, output_files[i]->filename, 1);
    #####: 1989:            fprintf(stderr, "Output file #%d does not contain any stream\n", i);
    #####: 1990:            ret = AVERROR(EINVAL);
    #####: 1991:            goto fail;
        -: 1992:        }
        1: 1993:        nb_ostreams += os->nb_streams;
        -: 1994:    }
        1: 1995:    if (nb_stream_maps > 0 && nb_stream_maps != nb_ostreams) {
    #####: 1996:        fprintf(stderr, "Number of stream maps must match number of output streams\n");
    #####: 1997:        ret = AVERROR(EINVAL);
    #####: 1998:        goto fail;
        -: 1999:    }
        -: 2000:
        -: 2001:    /* Sanity check the mapping args -- do the input files & streams exist? */
        1: 2002:    for(i=0;i<nb_stream_maps;i++) {
    #####: 2003:        int fi = stream_maps[i].file_index;
    #####: 2004:        int si = stream_maps[i].stream_index;
        -: 2005:
    #####: 2006:        if (fi < 0 || fi > nb_input_files - 1 ||
        -: 2007:            si < 0 || si > file_table[fi].nb_streams - 1) {
    #####: 2008:            fprintf(stderr,"Could not find input stream #%d.%d\n", fi, si);
    #####: 2009:            ret = AVERROR(EINVAL);
    #####: 2010:            goto fail;
        -: 2011:        }
    #####: 2012:        fi = stream_maps[i].sync_file_index;
    #####: 2013:        si = stream_maps[i].sync_stream_index;
    #####: 2014:        if (fi < 0 || fi > nb_input_files - 1 ||
        -: 2015:            si < 0 || si > file_table[fi].nb_streams - 1) {
    #####: 2016:            fprintf(stderr,"Could not find sync stream #%d.%d\n", fi, si);
    #####: 2017:            ret = AVERROR(EINVAL);
    #####: 2018:            goto fail;
        -: 2019:        }
        -: 2020:    }
        -: 2021:
        1: 2022:    ost_table = av_mallocz(sizeof(AVOutputStream *) * nb_ostreams);
        1: 2023:    if (!ost_table)
    #####: 2024:        goto fail;
        1: 2025:    n = 0;
        2: 2026:    for(k=0;k<nb_output_files;k++) {
        1: 2027:        os = output_files[k];
        3: 2028:        for(i=0;i<os->nb_streams;i++,n++) {
        -: 2029:            int found;
        2: 2030:            ost = ost_table[n] = output_streams_for_file[k][i];
        2: 2031:            ost->st = os->streams[i];
        2: 2032:            if (nb_stream_maps > 0) {
    #####: 2033:                ost->source_index = file_table[stream_maps[n].file_index].ist_index +
        -: 2034:                    stream_maps[n].stream_index;
        -: 2035:
        -: 2036:                /* Sanity check that the stream types match */
    #####: 2037:                if (ist_table[ost->source_index]->st->codec->codec_type != ost->st->codec->codec_type) {
    #####: 2038:                    int i= ost->file_index;
    #####: 2039:                    dump_format(output_files[i], i, output_files[i]->filename, 1);
    #####: 2040:                    fprintf(stderr, "Codec type mismatch for mapping #%d.%d -> #%d.%d\n",
        -: 2041:                        stream_maps[n].file_index, stream_maps[n].stream_index,
        -: 2042:                        ost->file_index, ost->index);
    #####: 2043:                    ffmpeg_exit(1);
        -: 2044:                }
        -: 2045:
        -: 2046:            } else {
        2: 2047:                int best_nb_frames=-1;
        -: 2048:                /* get corresponding input stream index : we select the first one with the right type */
        2: 2049:                found = 0;
        6: 2050:                for(j=0;j<nb_istreams;j++) {
        4: 2051:                    int skip=0;
        4: 2052:                    ist = ist_table[j];
        4: 2053:                    if(opt_programid){
        -: 2054:                        int pi,si;
    #####: 2055:                        AVFormatContext *f= input_files[ ist->file_index ];
    #####: 2056:                        skip=1;
    #####: 2057:                        for(pi=0; pi<f->nb_programs; pi++){
    #####: 2058:                            AVProgram *p= f->programs[pi];
    #####: 2059:                            if(p->id == opt_programid)
    #####: 2060:                                for(si=0; si<p->nb_stream_indexes; si++){
    #####: 2061:                                    if(f->streams[ p->stream_index[si] ] == ist->st)
    #####: 2062:                                        skip=0;
        -: 2063:                                }
        -: 2064:                        }
        -: 2065:                    }
        4: 2066:                    if (ist->discard && ist->st->discard != AVDISCARD_ALL && !skip &&
        -: 2067:                        ist->st->codec->codec_type == ost->st->codec->codec_type) {
        2: 2068:                        if(best_nb_frames < ist->st->codec_info_nb_frames){
        2: 2069:                            best_nb_frames= ist->st->codec_info_nb_frames;
        2: 2070:                            ost->source_index = j;
        2: 2071:                            found = 1;
        -: 2072:                        }
        -: 2073:                    }
        -: 2074:                }
        -: 2075:
        2: 2076:                if (!found) {
    #####: 2077:                    if(! opt_programid) {
        -: 2078:                        /* try again and reuse existing stream */
    #####: 2079:                        for(j=0;j<nb_istreams;j++) {
    #####: 2080:                            ist = ist_table[j];
    #####: 2081:                            if (   ist->st->codec->codec_type == ost->st->codec->codec_type
        -: 2082:                                && ist->st->discard != AVDISCARD_ALL) {
    #####: 2083:                                ost->source_index = j;
    #####: 2084:                                found = 1;
        -: 2085:                            }
        -: 2086:                        }
        -: 2087:                    }
    #####: 2088:                    if (!found) {
    #####: 2089:                        int i= ost->file_index;
    #####: 2090:                        dump_format(output_files[i], i, output_files[i]->filename, 1);
    #####: 2091:                        fprintf(stderr, "Could not find input stream matching output stream #%d.%d\n",
        -: 2092:                                ost->file_index, ost->index);
    #####: 2093:                        ffmpeg_exit(1);
        -: 2094:                    }
        -: 2095:                }
        -: 2096:            }
        2: 2097:            ist = ist_table[ost->source_index];
        2: 2098:            ist->discard = 0;
        2: 2099:            ost->sync_ist = (nb_stream_maps > 0) ?
        -: 2100:                ist_table[file_table[stream_maps[n].sync_file_index].ist_index +
        -: 2101:                         stream_maps[n].sync_stream_index] : ist;
        -: 2102:        }
        -: 2103:    }
        -: 2104:
        -: 2105:    /* for each output stream, we compute the right encoding parameters */
        3: 2106:    for(i=0;i<nb_ostreams;i++) {
        2: 2107:        ost = ost_table[i];
        2: 2108:        os = output_files[ost->file_index];
        2: 2109:        ist = ist_table[ost->source_index];
        -: 2110:
        2: 2111:        codec = ost->st->codec;
        2: 2112:        icodec = ist->st->codec;
        -: 2113:
        2: 2114:        if (metadata_streams_autocopy)
        2: 2115:            av_metadata_copy(&ost->st->metadata, ist->st->metadata,
        -: 2116:                             AV_METADATA_DONT_OVERWRITE);
        -: 2117:
        2: 2118:        ost->st->disposition = ist->st->disposition;
        2: 2119:        codec->bits_per_raw_sample= icodec->bits_per_raw_sample;
        2: 2120:        codec->chroma_sample_location = icodec->chroma_sample_location;
        -: 2121:
        2: 2122:        if (ost->st->stream_copy) {
        2: 2123:            uint64_t extra_size = (uint64_t)icodec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE;
        -: 2124:
        2: 2125:            if (extra_size > INT_MAX)
    #####: 2126:                goto fail;
        -: 2127:
        -: 2128:            /* if stream_copy is selected, no need to decode or encode */
        2: 2129:            codec->codec_id = icodec->codec_id;
        2: 2130:            codec->codec_type = icodec->codec_type;
        -: 2131:
        2: 2132:            if(!codec->codec_tag){
        2: 2133:                if(   !os->oformat->codec_tag
        -: 2134:                   || av_codec_get_id (os->oformat->codec_tag, icodec->codec_tag) == codec->codec_id
        -: 2135:                   || av_codec_get_tag(os->oformat->codec_tag, icodec->codec_id) <= 0)
        2: 2136:                    codec->codec_tag = icodec->codec_tag;
        -: 2137:            }
        -: 2138:
        2: 2139:            codec->bit_rate = icodec->bit_rate;
        2: 2140:            codec->rc_max_rate    = icodec->rc_max_rate;
        2: 2141:            codec->rc_buffer_size = icodec->rc_buffer_size;
        2: 2142:            codec->extradata= av_mallocz(extra_size);
        2: 2143:            if (!codec->extradata)
    #####: 2144:                goto fail;
        2: 2145:            memcpy(codec->extradata, icodec->extradata, icodec->extradata_size);
        2: 2146:            codec->extradata_size= icodec->extradata_size;
        8: 2147:            if(!copy_tb && av_q2d(icodec->time_base)*icodec->ticks_per_frame > av_q2d(ist->st->time_base) && av_q2d(ist->st->time_base) < 1.0/1000){
        1: 2148:                codec->time_base = icodec->time_base;
        1: 2149:                codec->time_base.num *= icodec->ticks_per_frame;
        1: 2150:                av_reduce(&codec->time_base.num, &codec->time_base.den,
        -: 2151:                          codec->time_base.num, codec->time_base.den, INT_MAX);
        -: 2152:            }else
        1: 2153:                codec->time_base = ist->st->time_base;
        2: 2154:            switch(codec->codec_type) {
        -: 2155:            case AVMEDIA_TYPE_AUDIO:
        1: 2156:                if(audio_volume != 256) {
    #####: 2157:                    fprintf(stderr,"-acodec copy and -vol are incompatible (frames are not decoded)\n");
    #####: 2158:                    ffmpeg_exit(1);
        -: 2159:                }
        1: 2160:                codec->channel_layout = icodec->channel_layout;
        1: 2161:                codec->sample_rate = icodec->sample_rate;
        1: 2162:                codec->channels = icodec->channels;
        1: 2163:                codec->frame_size = icodec->frame_size;
        1: 2164:                codec->block_align= icodec->block_align;
        1: 2165:                if(codec->block_align == 1 && codec->codec_id == CODEC_ID_MP3)
    #####: 2166:                    codec->block_align= 0;
        1: 2167:                if(codec->codec_id == CODEC_ID_AC3)
    #####: 2168:                    codec->block_align= 0;
        -: 2169:                break;
        -: 2170:            case AVMEDIA_TYPE_VIDEO:
        1: 2171:                codec->pix_fmt = icodec->pix_fmt;
        1: 2172:                codec->width = icodec->width;
        1: 2173:                codec->height = icodec->height;
        1: 2174:                codec->has_b_frames = icodec->has_b_frames;
        1: 2175:                break;
        -: 2176:            case AVMEDIA_TYPE_SUBTITLE:
    #####: 2177:                codec->width = icodec->width;
    #####: 2178:                codec->height = icodec->height;
    #####: 2179:                break;
        -: 2180:            default:
    #####: 2181:                abort();
        -: 2182:            }
        -: 2183:        } else {
    #####: 2184:            switch(codec->codec_type) {
        -: 2185:            case AVMEDIA_TYPE_AUDIO:
    #####: 2186:                ost->fifo= av_fifo_alloc(1024);
    #####: 2187:                if(!ost->fifo)
    #####: 2188:                    goto fail;
    #####: 2189:                ost->reformat_pair = MAKE_SFMT_PAIR(AV_SAMPLE_FMT_NONE,AV_SAMPLE_FMT_NONE);
    #####: 2190:                ost->audio_resample = codec->sample_rate != icodec->sample_rate || audio_sync_method > 1;
    #####: 2191:                icodec->request_channels = codec->channels;
    #####: 2192:                ist->decoding_needed = 1;
    #####: 2193:                ost->encoding_needed = 1;
    #####: 2194:                ost->resample_sample_fmt  = icodec->sample_fmt;
    #####: 2195:                ost->resample_sample_rate = icodec->sample_rate;
    #####: 2196:                ost->resample_channels    = icodec->channels;
    #####: 2197:                break;
        -: 2198:            case AVMEDIA_TYPE_VIDEO:
    #####: 2199:                if (ost->st->codec->pix_fmt == PIX_FMT_NONE) {
    #####: 2200:                    fprintf(stderr, "Video pixel format is unknown, stream cannot be encoded\n");
    #####: 2201:                    ffmpeg_exit(1);
        -: 2202:                }
    #####: 2203:                ost->video_resample = (codec->width != icodec->width   ||
        -: 2204:                                       codec->height != icodec->height ||
        -: 2205:                        (codec->pix_fmt != icodec->pix_fmt));
    #####: 2206:                if (ost->video_resample) {
        -: 2207:#if !CONFIG_AVFILTER
        -: 2208:                    avcodec_get_frame_defaults(&ost->pict_tmp);
        -: 2209:                    if(avpicture_alloc((AVPicture*)&ost->pict_tmp, codec->pix_fmt,
        -: 2210:                                         codec->width, codec->height)) {
        -: 2211:                        fprintf(stderr, "Cannot allocate temp picture, check pix fmt\n");
        -: 2212:                        ffmpeg_exit(1);
        -: 2213:                    }
        -: 2214:                    sws_flags = av_get_int(sws_opts, "sws_flags", NULL);
        -: 2215:                    ost->img_resample_ctx = sws_getContext(
        -: 2216:                        icodec->width,
        -: 2217:                        icodec->height,
        -: 2218:                            icodec->pix_fmt,
        -: 2219:                            codec->width,
        -: 2220:                            codec->height,
        -: 2221:                            codec->pix_fmt,
        -: 2222:                            sws_flags, NULL, NULL, NULL);
        -: 2223:                    if (ost->img_resample_ctx == NULL) {
        -: 2224:                        fprintf(stderr, "Cannot get resampling context\n");
        -: 2225:                        ffmpeg_exit(1);
        -: 2226:                    }
        -: 2227:
        -: 2228:                    ost->original_height = icodec->height;
        -: 2229:                    ost->original_width  = icodec->width;
        -: 2230:#endif
    #####: 2231:                    codec->bits_per_raw_sample= 0;
        -: 2232:                }
    #####: 2233:                ost->resample_height = icodec->height;
    #####: 2234:                ost->resample_width  = icodec->width;
    #####: 2235:                ost->resample_pix_fmt= icodec->pix_fmt;
    #####: 2236:                ost->encoding_needed = 1;
    #####: 2237:                ist->decoding_needed = 1;
        -: 2238:
        -: 2239:#if CONFIG_AVFILTER
    #####: 2240:                if (configure_filters(ist, ost)) {
    #####: 2241:                    fprintf(stderr, "Error opening filters!\n");
    #####: 2242:                    exit(1);
        -: 2243:                }
        -: 2244:#endif
        -: 2245:                break;
        -: 2246:            case AVMEDIA_TYPE_SUBTITLE:
    #####: 2247:                ost->encoding_needed = 1;
    #####: 2248:                ist->decoding_needed = 1;
    #####: 2249:                break;
        -: 2250:            default:
    #####: 2251:                abort();
        -: 2252:                break;
        -: 2253:            }
        -: 2254:            /* two pass mode */
    #####: 2255:            if (ost->encoding_needed &&
        -: 2256:                (codec->flags & (CODEC_FLAG_PASS1 | CODEC_FLAG_PASS2))) {
        -: 2257:                char logfilename[1024];
        -: 2258:                FILE *f;
        -: 2259:
    #####: 2260:                snprintf(logfilename, sizeof(logfilename), "%s-%d.log",
        -: 2261:                         pass_logfilename_prefix ? pass_logfilename_prefix : DEFAULT_PASS_LOGFILENAME_PREFIX,
        -: 2262:                         i);
    #####: 2263:                if (codec->flags & CODEC_FLAG_PASS1) {
    #####: 2264:                    f = fopen(logfilename, "wb");
    #####: 2265:                    if (!f) {
    #####: 2266:                        fprintf(stderr, "Cannot write log file '%s' for pass-1 encoding: %s\n", logfilename, strerror(errno));
    #####: 2267:                        ffmpeg_exit(1);
        -: 2268:                    }
    #####: 2269:                    ost->logfile = f;
        -: 2270:                } else {
        -: 2271:                    char  *logbuffer;
        -: 2272:                    size_t logbuffer_size;
    #####: 2273:                    if (read_file(logfilename, &logbuffer, &logbuffer_size) < 0) {
    #####: 2274:                        fprintf(stderr, "Error reading log file '%s' for pass-2 encoding\n", logfilename);
    #####: 2275:                        ffmpeg_exit(1);
        -: 2276:                    }
    #####: 2277:                    codec->stats_in = logbuffer;
        -: 2278:                }
        -: 2279:            }
        -: 2280:        }
        2: 2281:        if(codec->codec_type == AVMEDIA_TYPE_VIDEO){
        1: 2282:            int size= codec->width * codec->height;
        1: 2283:            bit_buffer_size= FFMAX(bit_buffer_size, 6*size + 200);
        -: 2284:        }
        -: 2285:    }
        -: 2286:
        1: 2287:    if (!bit_buffer)
        1: 2288:        bit_buffer = av_malloc(bit_buffer_size);
        1: 2289:    if (!bit_buffer) {
    #####: 2290:        fprintf(stderr, "Cannot allocate %d bytes output buffer\n",
        -: 2291:                bit_buffer_size);
    #####: 2292:        ret = AVERROR(ENOMEM);
    #####: 2293:        goto fail;
        -: 2294:    }
        -: 2295:
        -: 2296:    /* open each encoder */
        3: 2297:    for(i=0;i<nb_ostreams;i++) {
        2: 2298:        ost = ost_table[i];
        2: 2299:        if (ost->encoding_needed) {
    #####: 2300:            AVCodec *codec = i < nb_output_codecs ? output_codecs[i] : NULL;
    #####: 2301:            AVCodecContext *dec = ist_table[ost->source_index]->st->codec;
    #####: 2302:            if (!codec)
    #####: 2303:                codec = avcodec_find_encoder(ost->st->codec->codec_id);
    #####: 2304:            if (!codec) {
    #####: 2305:                snprintf(error, sizeof(error), "Encoder (codec id %d) not found for output stream #%d.%d",
        -: 2306:                         ost->st->codec->codec_id, ost->file_index, ost->index);
    #####: 2307:                ret = AVERROR(EINVAL);
    #####: 2308:                goto dump_format;
        -: 2309:            }
    #####: 2310:            if (dec->subtitle_header) {
    #####: 2311:                ost->st->codec->subtitle_header = av_malloc(dec->subtitle_header_size);
    #####: 2312:                if (!ost->st->codec->subtitle_header) {
    #####: 2313:                    ret = AVERROR(ENOMEM);
    #####: 2314:                    goto dump_format;
        -: 2315:                }
    #####: 2316:                memcpy(ost->st->codec->subtitle_header, dec->subtitle_header, dec->subtitle_header_size);
    #####: 2317:                ost->st->codec->subtitle_header_size = dec->subtitle_header_size;
        -: 2318:            }
    #####: 2319:            if (avcodec_open(ost->st->codec, codec) < 0) {
    #####: 2320:                snprintf(error, sizeof(error), "Error while opening encoder for output stream #%d.%d - maybe incorrect parameters such as bit_rate, rate, width or height",
        -: 2321:                        ost->file_index, ost->index);
    #####: 2322:                ret = AVERROR(EINVAL);
    #####: 2323:                goto dump_format;
        -: 2324:            }
    #####: 2325:            extra_size += ost->st->codec->extradata_size;
        -: 2326:        }
        -: 2327:    }
        -: 2328:
        -: 2329:    /* open each decoder */
        3: 2330:    for(i=0;i<nb_istreams;i++) {
        2: 2331:        ist = ist_table[i];
        2: 2332:        if (ist->decoding_needed) {
    #####: 2333:            AVCodec *codec = i < nb_input_codecs ? input_codecs[i] : NULL;
    #####: 2334:            if (!codec)
    #####: 2335:                codec = avcodec_find_decoder(ist->st->codec->codec_id);
    #####: 2336:            if (!codec) {
    #####: 2337:                snprintf(error, sizeof(error), "Decoder (codec id %d) not found for input stream #%d.%d",
        -: 2338:                        ist->st->codec->codec_id, ist->file_index, ist->index);
    #####: 2339:                ret = AVERROR(EINVAL);
    #####: 2340:                goto dump_format;
        -: 2341:            }
    #####: 2342:            if (avcodec_open(ist->st->codec, codec) < 0) {
    #####: 2343:                snprintf(error, sizeof(error), "Error while opening decoder for input stream #%d.%d",
        -: 2344:                        ist->file_index, ist->index);
    #####: 2345:                ret = AVERROR(EINVAL);
    #####: 2346:                goto dump_format;
        -: 2347:            }
        -: 2348:            //if (ist->st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        -: 2349:            //    ist->st->codec->flags |= CODEC_FLAG_REPEAT_FIELD;
        -: 2350:        }
        -: 2351:    }
        -: 2352:
        -: 2353:    /* init pts */
        3: 2354:    for(i=0;i<nb_istreams;i++) {
        -: 2355:        AVStream *st;
        2: 2356:        ist = ist_table[i];
        2: 2357:        st= ist->st;
        4: 2358:        ist->pts = st->avg_frame_rate.num ? - st->codec->has_b_frames*AV_TIME_BASE / av_q2d(st->avg_frame_rate) : 0;
        2: 2359:        ist->next_pts = AV_NOPTS_VALUE;
        2: 2360:        init_pts_correction(&ist->pts_ctx);
        2: 2361:        ist->is_start = 1;
        -: 2362:    }
        -: 2363:
        -: 2364:    /* set meta data information from input file if required */
        1: 2365:    for (i=0;i<nb_meta_data_maps;i++) {
        -: 2366:        AVFormatContext *files[2];
        -: 2367:        AVMetadata      **meta[2];
        -: 2368:        int j;
        -: 2369:
        -: 2370:#define METADATA_CHECK_INDEX(index, nb_elems, desc)\
        -: 2371:        if ((index) < 0 || (index) >= (nb_elems)) {\
        -: 2372:            snprintf(error, sizeof(error), "Invalid %s index %d while processing metadata maps\n",\
        -: 2373:                     (desc), (index));\
        -: 2374:            ret = AVERROR(EINVAL);\
        -: 2375:            goto dump_format;\
        -: 2376:        }
        -: 2377:
    #####: 2378:        int out_file_index = meta_data_maps[i][0].file;
    #####: 2379:        int in_file_index = meta_data_maps[i][1].file;
    #####: 2380:        if (in_file_index < 0 || out_file_index < 0)
    #####: 2381:            continue;
    #####: 2382:        METADATA_CHECK_INDEX(out_file_index, nb_output_files, "output file")
    #####: 2383:        METADATA_CHECK_INDEX(in_file_index, nb_input_files, "input file")
        -: 2384:
    #####: 2385:        files[0] = output_files[out_file_index];
    #####: 2386:        files[1] = input_files[in_file_index];
        -: 2387:
    #####: 2388:        for (j = 0; j < 2; j++) {
    #####: 2389:            AVMetaDataMap *map = &meta_data_maps[i][j];
        -: 2390:
    #####: 2391:            switch (map->type) {
        -: 2392:            case 'g':
    #####: 2393:                meta[j] = &files[j]->metadata;
    #####: 2394:                break;
        -: 2395:            case 's':
    #####: 2396:                METADATA_CHECK_INDEX(map->index, files[j]->nb_streams, "stream")
    #####: 2397:                meta[j] = &files[j]->streams[map->index]->metadata;
    #####: 2398:                break;
        -: 2399:            case 'c':
    #####: 2400:                METADATA_CHECK_INDEX(map->index, files[j]->nb_chapters, "chapter")
    #####: 2401:                meta[j] = &files[j]->chapters[map->index]->metadata;
    #####: 2402:                break;
        -: 2403:            case 'p':
    #####: 2404:                METADATA_CHECK_INDEX(map->index, files[j]->nb_programs, "program")
    #####: 2405:                meta[j] = &files[j]->programs[map->index]->metadata;
        -: 2406:                break;
        -: 2407:            }
        -: 2408:        }
        -: 2409:
    #####: 2410:        av_metadata_copy(meta[0], *meta[1], AV_METADATA_DONT_OVERWRITE);
        -: 2411:    }
        -: 2412:
        -: 2413:    /* copy global metadata by default */
        1: 2414:    if (metadata_global_autocopy) {
        -: 2415:
        2: 2416:        for (i = 0; i < nb_output_files; i++)
        1: 2417:            av_metadata_copy(&output_files[i]->metadata, input_files[0]->metadata,
        -: 2418:                             AV_METADATA_DONT_OVERWRITE);
        -: 2419:    }
        -: 2420:
        -: 2421:    /* copy chapters according to chapter maps */
        1: 2422:    for (i = 0; i < nb_chapter_maps; i++) {
    #####: 2423:        int infile  = chapter_maps[i].in_file;
    #####: 2424:        int outfile = chapter_maps[i].out_file;
        -: 2425:
    #####: 2426:        if (infile < 0 || outfile < 0)
    #####: 2427:            continue;
    #####: 2428:        if (infile >= nb_input_files) {
    #####: 2429:            snprintf(error, sizeof(error), "Invalid input file index %d in chapter mapping.\n", infile);
    #####: 2430:            ret = AVERROR(EINVAL);
    #####: 2431:            goto dump_format;
        -: 2432:        }
    #####: 2433:        if (outfile >= nb_output_files) {
    #####: 2434:            snprintf(error, sizeof(error), "Invalid output file index %d in chapter mapping.\n",outfile);
    #####: 2435:            ret = AVERROR(EINVAL);
    #####: 2436:            goto dump_format;
        -: 2437:        }
    #####: 2438:        copy_chapters(infile, outfile);
        -: 2439:    }
        -: 2440:
        -: 2441:    /* copy chapters from the first input file that has them*/
        1: 2442:    if (!nb_chapter_maps)
        2: 2443:        for (i = 0; i < nb_input_files; i++) {
        1: 2444:            if (!input_files[i]->nb_chapters)
        1: 2445:                continue;
        -: 2446:
    #####: 2447:            for (j = 0; j < nb_output_files; j++)
    #####: 2448:                if ((ret = copy_chapters(i, j)) < 0)
    #####: 2449:                    goto dump_format;
        -: 2450:            break;
        -: 2451:        }
        -: 2452:
        -: 2453:    /* open files and write file headers */
        2: 2454:    for(i=0;i<nb_output_files;i++) {
        1: 2455:        os = output_files[i];
        1: 2456:        if (av_write_header(os) < 0) {
    #####: 2457:            snprintf(error, sizeof(error), "Could not write header for output file #%d (incorrect codec parameters ?)", i);
    #####: 2458:            ret = AVERROR(EINVAL);
    #####: 2459:            goto dump_format;
        -: 2460:        }
        1: 2461:        if (strcmp(output_files[i]->oformat->name, "rtp")) {
        1: 2462:            want_sdp = 0;
        -: 2463:        }
        -: 2464:    }
        -: 2465:
        1: 2466: dump_format:
        -: 2467:    /* dump the file output parameters - cannot be done before in case
        -: 2468:       of stream copy */
        2: 2469:    for(i=0;i<nb_output_files;i++) {
        1: 2470:        dump_format(output_files[i], i, output_files[i]->filename, 1);
        -: 2471:    }
        -: 2472:
        -: 2473:    /* dump the stream mapping */
        1: 2474:    if (verbose >= 0) {
        1: 2475:        fprintf(stderr, "Stream mapping:\n");
        3: 2476:        for(i=0;i<nb_ostreams;i++) {
        2: 2477:            ost = ost_table[i];
        2: 2478:            fprintf(stderr, "  Stream #%d.%d -> #%d.%d",
        -: 2479:                    ist_table[ost->source_index]->file_index,
        -: 2480:                    ist_table[ost->source_index]->index,
        -: 2481:                    ost->file_index,
        -: 2482:                    ost->index);
        2: 2483:            if (ost->sync_ist != ist_table[ost->source_index])
    #####: 2484:                fprintf(stderr, " [sync #%d.%d]",
        -: 2485:                        ost->sync_ist->file_index,
        -: 2486:                        ost->sync_ist->index);
        2: 2487:            fprintf(stderr, "\n");
        -: 2488:        }
        -: 2489:    }
        -: 2490:
        1: 2491:    if (ret) {
    #####: 2492:        fprintf(stderr, "%s\n", error);
    #####: 2493:        goto fail;
        -: 2494:    }
        -: 2495:
        1: 2496:    if (want_sdp) {
    #####: 2497:        print_sdp(output_files, nb_output_files);
        -: 2498:    }
        -: 2499:
        1: 2500:    if (!using_stdin && verbose >= 0) {
        1: 2501:        fprintf(stderr, "Press [q] to stop encoding\n");
        1: 2502:        url_set_interrupt_cb(decode_interrupt_cb);
        -: 2503:    }
        1: 2504:    term_init();
        -: 2505:
        1: 2506:    timer_start = av_gettime();
        -: 2507:
    21553: 2508:    for(; received_sigterm == 0;) {
        -: 2509:        int file_index, ist_index;
        -: 2510:        AVPacket pkt;
        -: 2511:        double ipts_min;
        -: 2512:        double opts_min;
        -: 2513:
    21552: 2514:    redo:
    21552: 2515:        ipts_min= 1e100;
    21552: 2516:        opts_min= 1e100;
        -: 2517:        /* if 'q' pressed, exits */
    21552: 2518:        if (!using_stdin) {
    21552: 2519:            if (q_pressed)
    #####: 2520:                break;
        -: 2521:            /* read_key() returns 0 on EOF */
    21552: 2522:            key = read_key();
    21552: 2523:            if (key == 'q')
    #####: 2524:                break;
        -: 2525:        }
        -: 2526:
        -: 2527:        /* select the stream that we must read now by looking at the
        -: 2528:           smallest output pts */
    21552: 2529:        file_index = -1;
    64656: 2530:        for(i=0;i<nb_ostreams;i++) {
        -: 2531:            double ipts, opts;
    43104: 2532:            ost = ost_table[i];
    43104: 2533:            os = output_files[ost->file_index];
    43104: 2534:            ist = ist_table[ost->source_index];
    43104: 2535:            if(ist->is_past_recording_time || no_packet[ist->file_index])
        -: 2536:                continue;
    86208: 2537:                opts = ost->st->pts.val * av_q2d(ost->st->time_base);
    43104: 2538:            ipts = (double)ist->pts;
    43104: 2539:            if (!file_table[ist->file_index].eof_reached){
    43102: 2540:                if(ipts < ipts_min) {
    39352: 2541:                    ipts_min = ipts;
    39352: 2542:                    if(input_sync ) file_index = ist->file_index;
        -: 2543:                }
    43102: 2544:                if(opts < opts_min) {
    41112: 2545:                    opts_min = opts;
    41112: 2546:                    if(!input_sync) file_index = ist->file_index;
        -: 2547:                }
        -: 2548:            }
    43104: 2549:            if(ost->frame_number >= max_frames[ost->st->codec->codec_type]){
    #####: 2550:                file_index= -1;
    #####: 2551:                break;
        -: 2552:            }
        -: 2553:        }
        -: 2554:        /* if none, if is finished */
    21552: 2555:        if (file_index < 0) {
        1: 2556:            if(no_packet_count){
    #####: 2557:                no_packet_count=0;
    #####: 2558:                memset(no_packet, 0, sizeof(no_packet));
    #####: 2559:                usleep(10000);
    #####: 2560:                continue;
        -: 2561:            }
        -: 2562:            break;
        -: 2563:        }
        -: 2564:
        -: 2565:        /* finish if limit size exhausted */
    21551: 2566:        if (limit_filesize != 0 && limit_filesize <= url_ftell(output_files[0]->pb))
    #####: 2567:            break;
        -: 2568:
        -: 2569:        /* read a frame from it and output it in the fifo */
    21551: 2570:        is = input_files[file_index];
    21551: 2571:        ret= av_read_frame(is, &pkt);
    21551: 2572:        if(ret == AVERROR(EAGAIN)){
    #####: 2573:            no_packet[file_index]=1;
    #####: 2574:            no_packet_count++;
    #####: 2575:            continue;
        -: 2576:        }
    21551: 2577:        if (ret < 0) {
        1: 2578:            file_table[file_index].eof_reached = 1;
        1: 2579:            if (opt_shortest)
    #####: 2580:                break;
        -: 2581:            else
        1: 2582:                continue;
        -: 2583:        }
        -: 2584:
    21550: 2585:        no_packet_count=0;
    21550: 2586:        memset(no_packet, 0, sizeof(no_packet));
        -: 2587:
    21550: 2588:        if (do_pkt_dump) {
    #####: 2589:            av_pkt_dump_log(NULL, AV_LOG_DEBUG, &pkt, do_hex_dump);
        -: 2590:        }
        -: 2591:        /* the following test is needed in case new streams appear
        -: 2592:           dynamically in stream : we ignore them */
    21550: 2593:        if (pkt.stream_index >= file_table[file_index].nb_streams)
    #####: 2594:            goto discard_packet;
    21550: 2595:        ist_index = file_table[file_index].ist_index + pkt.stream_index;
    21550: 2596:        ist = ist_table[ist_index];
    21550: 2597:        if (ist->discard)
    #####: 2598:            goto discard_packet;
        -: 2599:
    21550: 2600:        if (pkt.dts != AV_NOPTS_VALUE)
    21550: 2601:            pkt.dts += av_rescale_q(input_files_ts_offset[ist->file_index], AV_TIME_BASE_Q, ist->st->time_base);
    21550: 2602:        if (pkt.pts != AV_NOPTS_VALUE)
    21550: 2603:            pkt.pts += av_rescale_q(input_files_ts_offset[ist->file_index], AV_TIME_BASE_Q, ist->st->time_base);
        -: 2604:
    21550: 2605:        if (pkt.stream_index < nb_input_files_ts_scale[file_index]
        -: 2606:            && input_files_ts_scale[file_index][pkt.stream_index]){
    #####: 2607:            if(pkt.pts != AV_NOPTS_VALUE)
    #####: 2608:                pkt.pts *= input_files_ts_scale[file_index][pkt.stream_index];
    #####: 2609:            if(pkt.dts != AV_NOPTS_VALUE)
    #####: 2610:                pkt.dts *= input_files_ts_scale[file_index][pkt.stream_index];
        -: 2611:        }
        -: 2612:
        -: 2613://        fprintf(stderr, "next:%"PRId64" dts:%"PRId64" off:%"PRId64" %d\n", ist->next_pts, pkt.dts, input_files_ts_offset[ist->file_index], ist->st->codec->codec_type);
    21550: 2614:        if (pkt.dts != AV_NOPTS_VALUE && ist->next_pts != AV_NOPTS_VALUE
        -: 2615:            && (is->iformat->flags & AVFMT_TS_DISCONT)) {
    21548: 2616:            int64_t pkt_dts= av_rescale_q(pkt.dts, ist->st->time_base, AV_TIME_BASE_Q);
    21548: 2617:            int64_t delta= pkt_dts - ist->next_pts;
    21548: 2618:            if((FFABS(delta) > 1LL*dts_delta_threshold*AV_TIME_BASE || pkt_dts+1<ist->pts)&& !copy_ts){
    #####: 2619:                input_files_ts_offset[ist->file_index]-= delta;
    #####: 2620:                if (verbose > 2)
    #####: 2621:                    fprintf(stderr, "timestamp discontinuity %"PRId64", new offset= %"PRId64"\n", delta, input_files_ts_offset[ist->file_index]);
    #####: 2622:                pkt.dts-= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
    #####: 2623:                if(pkt.pts != AV_NOPTS_VALUE)
    #####: 2624:                    pkt.pts-= av_rescale_q(delta, AV_TIME_BASE_Q, ist->st->time_base);
        -: 2625:            }
        -: 2626:        }
        -: 2627:
        -: 2628:        /* finish if recording time exhausted */
    21550: 2629:        if (recording_time != INT64_MAX &&
    #####: 2630:            av_compare_ts(pkt.pts, ist->st->time_base, recording_time + start_time, (AVRational){1, 1000000}) >= 0) {
    #####: 2631:            ist->is_past_recording_time = 1;
    #####: 2632:            goto discard_packet;
        -: 2633:        }
        -: 2634:
        -: 2635:        //fprintf(stderr,"read #%d.%d size=%d\n", ist->file_index, ist->index, pkt.size);
    21550: 2636:        if (output_packet(ist, ist_index, ost_table, nb_ostreams, &pkt) < 0) {
        -: 2637:
    #####: 2638:            if (verbose >= 0)
    #####: 2639:                fprintf(stderr, "Error while decoding stream #%d.%d\n",
        -: 2640:                        ist->file_index, ist->index);
    #####: 2641:            if (exit_on_error)
    #####: 2642:                ffmpeg_exit(1);
    #####: 2643:            av_free_packet(&pkt);
    #####: 2644:            goto redo;
        -: 2645:        }
        -: 2646:
    21550: 2647:    discard_packet:
    21550: 2648:        av_free_packet(&pkt);
        -: 2649:
        -: 2650:        /* dump report by using the output first video and audio streams */
    21550: 2651:        print_report(output_files, ost_table, nb_ostreams, 0);
        -: 2652:    }
        -: 2653:
        -: 2654:    /* at the end of stream, we must flush the decoder buffers */
        3: 2655:    for(i=0;i<nb_istreams;i++) {
        2: 2656:        ist = ist_table[i];
        2: 2657:        if (ist->decoding_needed) {
    #####: 2658:            output_packet(ist, i, ost_table, nb_ostreams, NULL);
        -: 2659:        }
        -: 2660:    }
        -: 2661:
        1: 2662:    term_exit();
        -: 2663:
        -: 2664:    /* write the trailer if needed and close file */
        2: 2665:    for(i=0;i<nb_output_files;i++) {
        1: 2666:        os = output_files[i];
        1: 2667:        av_write_trailer(os);
        -: 2668:    }
        -: 2669:
        -: 2670:    /* dump report by using the first video and audio streams */
        1: 2671:    print_report(output_files, ost_table, nb_ostreams, 1);
        -: 2672:
        -: 2673:    /* close each encoder */
        3: 2674:    for(i=0;i<nb_ostreams;i++) {
        2: 2675:        ost = ost_table[i];
        2: 2676:        if (ost->encoding_needed) {
    #####: 2677:            av_freep(&ost->st->codec->stats_in);
    #####: 2678:            avcodec_close(ost->st->codec);
        -: 2679:        }
        -: 2680:    }
        -: 2681:
        -: 2682:    /* close each decoder */
        3: 2683:    for(i=0;i<nb_istreams;i++) {
        2: 2684:        ist = ist_table[i];
        2: 2685:        if (ist->decoding_needed) {
    #####: 2686:            avcodec_close(ist->st->codec);
        -: 2687:        }
        -: 2688:    }
        -: 2689:#if CONFIG_AVFILTER
        1: 2690:    if (graph) {
    #####: 2691:        avfilter_graph_free(graph);
    #####: 2692:        av_freep(&graph);
        -: 2693:    }
        -: 2694:#endif
        -: 2695:
        -: 2696:    /* finished ! */
        1: 2697:    ret = 0;
        -: 2698:
        1: 2699: fail:
        1: 2700:    av_freep(&bit_buffer);
        1: 2701:    av_free(file_table);
        -: 2702:
        1: 2703:    if (ist_table) {
        3: 2704:        for(i=0;i<nb_istreams;i++) {
        2: 2705:            ist = ist_table[i];
        2: 2706:            av_free(ist);
        -: 2707:        }
        1: 2708:        av_free(ist_table);
        -: 2709:    }
        1: 2710:    if (ost_table) {
        3: 2711:        for(i=0;i<nb_ostreams;i++) {
        2: 2712:            ost = ost_table[i];
        2: 2713:            if (ost) {
        2: 2714:                if (ost->st->stream_copy)
        2: 2715:                    av_freep(&ost->st->codec->extradata);
        2: 2716:                if (ost->logfile) {
    #####: 2717:                    fclose(ost->logfile);
    #####: 2718:                    ost->logfile = NULL;
        -: 2719:                }
        2: 2720:                av_fifo_free(ost->fifo); /* works even if fifo is not
        -: 2721:                                             initialized but set to zero */
        2: 2722:                av_freep(&ost->st->codec->subtitle_header);
        2: 2723:                av_free(ost->pict_tmp.data[0]);
        2: 2724:                av_free(ost->forced_kf_pts);
        2: 2725:                if (ost->video_resample)
    #####: 2726:                    sws_freeContext(ost->img_resample_ctx);
        2: 2727:                if (ost->resample)
    #####: 2728:                    audio_resample_close(ost->resample);
        2: 2729:                if (ost->reformat_ctx)
    #####: 2730:                    av_audio_convert_free(ost->reformat_ctx);
        2: 2731:                av_free(ost);
        -: 2732:            }
        -: 2733:        }
        1: 2734:        av_free(ost_table);
        -: 2735:    }
        1: 2736:    return ret;
        -: 2737:}
        -: 2738:
        -: 2739:static void opt_format(const char *arg)
    #####: 2740:{
    #####: 2741:    last_asked_format = arg;
    #####: 2742:}
        -: 2743:
        -: 2744:static void opt_video_rc_override_string(const char *arg)
    #####: 2745:{
    #####: 2746:    video_rc_override_string = arg;
    #####: 2747:}
        -: 2748:
        -: 2749:static int opt_me_threshold(const char *opt, const char *arg)
    #####: 2750:{
    #####: 2751:    me_threshold = parse_number_or_die(opt, arg, OPT_INT64, INT_MIN, INT_MAX);
    #####: 2752:    return 0;
        -: 2753:}
        -: 2754:
        -: 2755:static int opt_verbose(const char *opt, const char *arg)
    #####: 2756:{
    #####: 2757:    verbose = parse_number_or_die(opt, arg, OPT_INT64, -10, 10);
    #####: 2758:    return 0;
        -: 2759:}
        -: 2760:
        -: 2761:static int opt_frame_rate(const char *opt, const char *arg)
    #####: 2762:{
    #####: 2763:    if (av_parse_video_rate(&frame_rate, arg) < 0) {
    #####: 2764:        fprintf(stderr, "Incorrect value for %s: %s\n", opt, arg);
    #####: 2765:        ffmpeg_exit(1);
        -: 2766:    }
    #####: 2767:    return 0;
        -: 2768:}
        -: 2769:
        -: 2770:static int opt_bitrate(const char *opt, const char *arg)
    #####: 2771:{
    #####: 2772:    int codec_type = opt[0]=='a' ? AVMEDIA_TYPE_AUDIO : AVMEDIA_TYPE_VIDEO;
        -: 2773:
    #####: 2774:    opt_default(opt, arg);
        -: 2775:
    #####: 2776:    if (av_get_int(avcodec_opts[codec_type], "b", NULL) < 1000)
    #####: 2777:        fprintf(stderr, "WARNING: The bitrate parameter is set too low. It takes bits/s as argument, not kbits/s\n");
        -: 2778:
    #####: 2779:    return 0;
        -: 2780:}
        -: 2781:
        -: 2782:static int opt_frame_crop(const char *opt, const char *arg)
    #####: 2783:{
    #####: 2784:    fprintf(stderr, "Option '%s' has been removed, use the crop filter instead\n", opt);
    #####: 2785:    return AVERROR(EINVAL);
        -: 2786:}
        -: 2787:
        -: 2788:static void opt_frame_size(const char *arg)
    #####: 2789:{
    #####: 2790:    if (av_parse_video_size(&frame_width, &frame_height, arg) < 0) {
    #####: 2791:        fprintf(stderr, "Incorrect frame size\n");
    #####: 2792:        ffmpeg_exit(1);
        -: 2793:    }
    #####: 2794:}
        -: 2795:
    #####: 2796:static int opt_pad(const char *opt, const char *arg) {
    #####: 2797:    fprintf(stderr, "Option '%s' has been removed, use the pad filter instead\n", opt);
    #####: 2798:    return -1;
        -: 2799:}
        -: 2800:
        -: 2801:static void opt_frame_pix_fmt(const char *arg)
    #####: 2802:{
    #####: 2803:    if (strcmp(arg, "list")) {
    #####: 2804:        frame_pix_fmt = av_get_pix_fmt(arg);
    #####: 2805:        if (frame_pix_fmt == PIX_FMT_NONE) {
    #####: 2806:            fprintf(stderr, "Unknown pixel format requested: %s\n", arg);
    #####: 2807:            ffmpeg_exit(1);
        -: 2808:        }
        -: 2809:    } else {
    #####: 2810:        show_pix_fmts();
    #####: 2811:        ffmpeg_exit(0);
        -: 2812:    }
    #####: 2813:}
        -: 2814:
        -: 2815:static void opt_frame_aspect_ratio(const char *arg)
    #####: 2816:{
    #####: 2817:    int x = 0, y = 0;
    #####: 2818:    double ar = 0;
        -: 2819:    const char *p;
        -: 2820:    char *end;
        -: 2821:
    #####: 2822:    p = strchr(arg, ':');
    #####: 2823:    if (p) {
    #####: 2824:        x = strtol(arg, &end, 10);
    #####: 2825:        if (end == p)
    #####: 2826:            y = strtol(end+1, &end, 10);
    #####: 2827:        if (x > 0 && y > 0)
    #####: 2828:            ar = (double)x / (double)y;
        -: 2829:    } else
    #####: 2830:        ar = strtod(arg, NULL);
        -: 2831:
    #####: 2832:    if (!ar) {
    #####: 2833:        fprintf(stderr, "Incorrect aspect ratio specification.\n");
    #####: 2834:        ffmpeg_exit(1);
        -: 2835:    }
    #####: 2836:    frame_aspect_ratio = ar;
    #####: 2837:}
        -: 2838:
        -: 2839:static int opt_metadata(const char *opt, const char *arg)
    #####: 2840:{
    #####: 2841:    char *mid= strchr(arg, '=');
        -: 2842:
    #####: 2843:    if(!mid){
    #####: 2844:        fprintf(stderr, "Missing =\n");
    #####: 2845:        ffmpeg_exit(1);
        -: 2846:    }
    #####: 2847:    *mid++= 0;
        -: 2848:
    #####: 2849:    av_metadata_set2(&metadata, arg, mid, 0);
        -: 2850:
    #####: 2851:    return 0;
        -: 2852:}
        -: 2853:
        -: 2854:static void opt_qscale(const char *arg)
    #####: 2855:{
    #####: 2856:    video_qscale = atof(arg);
    #####: 2857:    if (video_qscale <= 0 ||
        -: 2858:        video_qscale > 255) {
    #####: 2859:        fprintf(stderr, "qscale must be > 0.0 and <= 255\n");
    #####: 2860:        ffmpeg_exit(1);
        -: 2861:    }
    #####: 2862:}
        -: 2863:
        -: 2864:static void opt_top_field_first(const char *arg)
    #####: 2865:{
    #####: 2866:    top_field_first= atoi(arg);
    #####: 2867:}
        -: 2868:
        -: 2869:static int opt_thread_count(const char *opt, const char *arg)
    #####: 2870:{
    #####: 2871:    thread_count= parse_number_or_die(opt, arg, OPT_INT64, 0, INT_MAX);
        -: 2872:#if !HAVE_THREADS
        -: 2873:    if (verbose >= 0)
        -: 2874:        fprintf(stderr, "Warning: not compiled with thread support, using thread emulation\n");
        -: 2875:#endif
    #####: 2876:    return 0;
        -: 2877:}
        -: 2878:
        -: 2879:static void opt_audio_sample_fmt(const char *arg)
    #####: 2880:{
    #####: 2881:    if (strcmp(arg, "list")) {
    #####: 2882:        audio_sample_fmt = av_get_sample_fmt(arg);
    #####: 2883:        if (audio_sample_fmt == AV_SAMPLE_FMT_NONE) {
    #####: 2884:            av_log(NULL, AV_LOG_ERROR, "Invalid sample format '%s'\n", arg);
    #####: 2885:            ffmpeg_exit(1);
        -: 2886:        }
        -: 2887:    } else {
    #####: 2888:        list_fmts(av_get_sample_fmt_string, AV_SAMPLE_FMT_NB);
    #####: 2889:        ffmpeg_exit(0);
        -: 2890:    }
    #####: 2891:}
        -: 2892:
        -: 2893:static int opt_audio_rate(const char *opt, const char *arg)
    #####: 2894:{
    #####: 2895:    audio_sample_rate = parse_number_or_die(opt, arg, OPT_INT64, 0, INT_MAX);
    #####: 2896:    return 0;
        -: 2897:}
        -: 2898:
        -: 2899:static int opt_audio_channels(const char *opt, const char *arg)
    #####: 2900:{
    #####: 2901:    audio_channels = parse_number_or_die(opt, arg, OPT_INT64, 0, INT_MAX);
    #####: 2902:    return 0;
        -: 2903:}
        -: 2904:
        -: 2905:static void opt_video_channel(const char *arg)
    #####: 2906:{
    #####: 2907:    video_channel = strtol(arg, NULL, 0);
    #####: 2908:}
        -: 2909:
        -: 2910:static void opt_video_standard(const char *arg)
    #####: 2911:{
    #####: 2912:    video_standard = av_strdup(arg);
    #####: 2913:}
        -: 2914:
        -: 2915:static void opt_codec(int *pstream_copy, char **pcodec_name,
        -: 2916:                      int codec_type, const char *arg)
        2: 2917:{
        2: 2918:    av_freep(pcodec_name);
        2: 2919:    if (!strcmp(arg, "copy")) {
        2: 2920:        *pstream_copy = 1;
        -: 2921:    } else {
    #####: 2922:        *pcodec_name = av_strdup(arg);
        -: 2923:    }
        2: 2924:}
        -: 2925:
        -: 2926:static void opt_audio_codec(const char *arg)
        1: 2927:{
        1: 2928:    opt_codec(&audio_stream_copy, &audio_codec_name, AVMEDIA_TYPE_AUDIO, arg);
        1: 2929:}
        -: 2930:
        -: 2931:static void opt_video_codec(const char *arg)
        1: 2932:{
        1: 2933:    opt_codec(&video_stream_copy, &video_codec_name, AVMEDIA_TYPE_VIDEO, arg);
        1: 2934:}
        -: 2935:
        -: 2936:static void opt_subtitle_codec(const char *arg)
    #####: 2937:{
    #####: 2938:    opt_codec(&subtitle_stream_copy, &subtitle_codec_name, AVMEDIA_TYPE_SUBTITLE, arg);
    #####: 2939:}
        -: 2940:
        -: 2941:static int opt_codec_tag(const char *opt, const char *arg)
    #####: 2942:{
        -: 2943:    char *tail;
        -: 2944:    uint32_t *codec_tag;
        -: 2945:
    #####: 2946:    codec_tag = !strcmp(opt, "atag") ? &audio_codec_tag :
        -: 2947:                !strcmp(opt, "vtag") ? &video_codec_tag :
        -: 2948:                !strcmp(opt, "stag") ? &subtitle_codec_tag : NULL;
    #####: 2949:    if (!codec_tag)
    #####: 2950:        return -1;
        -: 2951:
    #####: 2952:    *codec_tag = strtol(arg, &tail, 0);
    #####: 2953:    if (!tail || *tail)
    #####: 2954:        *codec_tag = AV_RL32(arg);
        -: 2955:
    #####: 2956:    return 0;
        -: 2957:}
        -: 2958:
        -: 2959:static void opt_map(const char *arg)
    #####: 2960:{
        -: 2961:    AVStreamMap *m;
        -: 2962:    char *p;
        -: 2963:
    #####: 2964:    stream_maps = grow_array(stream_maps, sizeof(*stream_maps), &nb_stream_maps, nb_stream_maps + 1);
    #####: 2965:    m = &stream_maps[nb_stream_maps-1];
        -: 2966:
    #####: 2967:    m->file_index = strtol(arg, &p, 0);
    #####: 2968:    if (*p)
    #####: 2969:        p++;
        -: 2970:
    #####: 2971:    m->stream_index = strtol(p, &p, 0);
    #####: 2972:    if (*p) {
    #####: 2973:        p++;
    #####: 2974:        m->sync_file_index = strtol(p, &p, 0);
    #####: 2975:        if (*p)
    #####: 2976:            p++;
    #####: 2977:        m->sync_stream_index = strtol(p, &p, 0);
        -: 2978:    } else {
    #####: 2979:        m->sync_file_index = m->file_index;
    #####: 2980:        m->sync_stream_index = m->stream_index;
        -: 2981:    }
    #####: 2982:}
        -: 2983:
        -: 2984:static void parse_meta_type(char *arg, char *type, int *index, char **endptr)
    #####: 2985:{
    #####: 2986:    *endptr = arg;
    #####: 2987:    if (*arg == ',') {
    #####: 2988:        *type = *(++arg);
    #####: 2989:        switch (*arg) {
        -: 2990:        case 'g':
        -: 2991:            break;
        -: 2992:        case 's':
        -: 2993:        case 'c':
        -: 2994:        case 'p':
    #####: 2995:            *index = strtol(++arg, endptr, 0);
    #####: 2996:            break;
        -: 2997:        default:
    #####: 2998:            fprintf(stderr, "Invalid metadata type %c.\n", *arg);
    #####: 2999:            ffmpeg_exit(1);
        -: 3000:        }
        -: 3001:    } else
    #####: 3002:        *type = 'g';
    #####: 3003:}
        -: 3004:
        -: 3005:static void opt_map_meta_data(const char *arg)
    #####: 3006:{
        -: 3007:    AVMetaDataMap *m, *m1;
        -: 3008:    char *p;
        -: 3009:
    #####: 3010:    meta_data_maps = grow_array(meta_data_maps, sizeof(*meta_data_maps),
        -: 3011:                                &nb_meta_data_maps, nb_meta_data_maps + 1);
        -: 3012:
    #####: 3013:    m = &meta_data_maps[nb_meta_data_maps - 1][0];
    #####: 3014:    m->file = strtol(arg, &p, 0);
    #####: 3015:    parse_meta_type(p, &m->type, &m->index, &p);
    #####: 3016:    if (*p)
    #####: 3017:        p++;
        -: 3018:
    #####: 3019:    m1 = &meta_data_maps[nb_meta_data_maps - 1][1];
    #####: 3020:    m1->file = strtol(p, &p, 0);
    #####: 3021:    parse_meta_type(p, &m1->type, &m1->index, &p);
        -: 3022:
    #####: 3023:    if (m->type == 'g' || m1->type == 'g')
    #####: 3024:        metadata_global_autocopy = 0;
    #####: 3025:    if (m->type == 's' || m1->type == 's')
    #####: 3026:        metadata_streams_autocopy = 0;
    #####: 3027:    if (m->type == 'c' || m1->type == 'c')
    #####: 3028:        metadata_chapters_autocopy = 0;
    #####: 3029:}
        -: 3030:
        -: 3031:static void opt_map_chapters(const char *arg)
    #####: 3032:{
        -: 3033:    AVChapterMap *c;
        -: 3034:    char *p;
        -: 3035:
    #####: 3036:    chapter_maps = grow_array(chapter_maps, sizeof(*chapter_maps), &nb_chapter_maps,
        -: 3037:                              nb_chapter_maps + 1);
    #####: 3038:    c = &chapter_maps[nb_chapter_maps - 1];
    #####: 3039:    c->out_file = strtol(arg, &p, 0);
    #####: 3040:    if (*p)
    #####: 3041:        p++;
        -: 3042:
    #####: 3043:    c->in_file = strtol(p, &p, 0);
    #####: 3044:}
        -: 3045:
        -: 3046:static void opt_input_ts_scale(const char *arg)
    #####: 3047:{
        -: 3048:    unsigned int stream;
        -: 3049:    double scale;
        -: 3050:    char *p;
        -: 3051:
    #####: 3052:    stream = strtol(arg, &p, 0);
    #####: 3053:    if (*p)
    #####: 3054:        p++;
    #####: 3055:    scale= strtod(p, &p);
        -: 3056:
    #####: 3057:    if(stream >= MAX_STREAMS)
    #####: 3058:        ffmpeg_exit(1);
        -: 3059:
    #####: 3060:    input_files_ts_scale[nb_input_files] = grow_array(input_files_ts_scale[nb_input_files], sizeof(*input_files_ts_scale[nb_input_files]), &nb_input_files_ts_scale[nb_input_files], stream + 1);
    #####: 3061:    input_files_ts_scale[nb_input_files][stream]= scale;
    #####: 3062:}
        -: 3063:
        -: 3064:static int opt_recording_time(const char *opt, const char *arg)
    #####: 3065:{
    #####: 3066:    recording_time = parse_time_or_die(opt, arg, 1);
    #####: 3067:    return 0;
        -: 3068:}
        -: 3069:
        -: 3070:static int opt_start_time(const char *opt, const char *arg)
    #####: 3071:{
    #####: 3072:    start_time = parse_time_or_die(opt, arg, 1);
    #####: 3073:    return 0;
        -: 3074:}
        -: 3075:
        -: 3076:static int opt_recording_timestamp(const char *opt, const char *arg)
    #####: 3077:{
    #####: 3078:    recording_timestamp = parse_time_or_die(opt, arg, 0) / 1000000;
    #####: 3079:    return 0;
        -: 3080:}
        -: 3081:
        -: 3082:static int opt_input_ts_offset(const char *opt, const char *arg)
    #####: 3083:{
    #####: 3084:    input_ts_offset = parse_time_or_die(opt, arg, 1);
    #####: 3085:    return 0;
        -: 3086:}
        -: 3087:
        -: 3088:static enum CodecID find_codec_or_die(const char *name, int type, int encoder, int strict)
        3: 3089:{
        3: 3090:    const char *codec_string = encoder ? "encoder" : "decoder";
        -: 3091:    AVCodec *codec;
        -: 3092:
        3: 3093:    if(!name)
        3: 3094:        return CODEC_ID_NONE;
    #####: 3095:    codec = encoder ?
        -: 3096:        avcodec_find_encoder_by_name(name) :
        -: 3097:        avcodec_find_decoder_by_name(name);
    #####: 3098:    if(!codec) {
    #####: 3099:        fprintf(stderr, "Unknown %s '%s'\n", codec_string, name);
    #####: 3100:        ffmpeg_exit(1);
        -: 3101:    }
    #####: 3102:    if(codec->type != type) {
    #####: 3103:        fprintf(stderr, "Invalid %s type '%s'\n", codec_string, name);
    #####: 3104:        ffmpeg_exit(1);
        -: 3105:    }
    #####: 3106:    if(codec->capabilities & CODEC_CAP_EXPERIMENTAL &&
        -: 3107:       strict > FF_COMPLIANCE_EXPERIMENTAL) {
    #####: 3108:        fprintf(stderr, "%s '%s' is experimental and might produce bad "
        -: 3109:                "results.\nAdd '-strict experimental' if you want to use it.\n",
        -: 3110:                codec_string, codec->name);
    #####: 3111:        codec = encoder ?
        -: 3112:            avcodec_find_encoder(codec->id) :
        -: 3113:            avcodec_find_decoder(codec->id);
    #####: 3114:        if (!(codec->capabilities & CODEC_CAP_EXPERIMENTAL))
    #####: 3115:            fprintf(stderr, "Or use the non experimental %s '%s'.\n",
        -: 3116:                    codec_string, codec->name);
    #####: 3117:        ffmpeg_exit(1);
        -: 3118:    }
    #####: 3119:    return codec->id;
        -: 3120:}
        -: 3121:
        -: 3122:static void opt_input_file(const char *filename)
        1: 3123:{
        -: 3124:    AVFormatContext *ic;
        1: 3125:    AVFormatParameters params, *ap = &params;
        1: 3126:    AVInputFormat *file_iformat = NULL;
        -: 3127:    int err, i, ret, rfps, rfps_base;
        -: 3128:    int64_t timestamp;
        -: 3129:
        1: 3130:    if (last_asked_format) {
    #####: 3131:        if (!(file_iformat = av_find_input_format(last_asked_format))) {
    #####: 3132:            fprintf(stderr, "Unknown input format: '%s'\n", last_asked_format);
    #####: 3133:            ffmpeg_exit(1);
        -: 3134:        }
    #####: 3135:        last_asked_format = NULL;
        -: 3136:    }
        -: 3137:
        1: 3138:    if (!strcmp(filename, "-"))
    #####: 3139:        filename = "pipe:";
        -: 3140:
        1: 3141:    using_stdin |= !strncmp(filename, "pipe:", 5) ||
        -: 3142:                    !strcmp(filename, "/dev/stdin");
        -: 3143:
        -: 3144:    /* get default parameters from command line */
        1: 3145:    ic = avformat_alloc_context();
        1: 3146:    if (!ic) {
    #####: 3147:        print_error(filename, AVERROR(ENOMEM));
    #####: 3148:        ffmpeg_exit(1);
        -: 3149:    }
        -: 3150:
        1: 3151:    memset(ap, 0, sizeof(*ap));
        1: 3152:    ap->prealloced_context = 1;
        1: 3153:    ap->sample_rate = audio_sample_rate;
        1: 3154:    ap->channels = audio_channels;
        1: 3155:    ap->time_base.den = frame_rate.num;
        1: 3156:    ap->time_base.num = frame_rate.den;
        1: 3157:    ap->width = frame_width;
        1: 3158:    ap->height = frame_height;
        1: 3159:    ap->pix_fmt = frame_pix_fmt;
        -: 3160:   // ap->sample_fmt = audio_sample_fmt; //FIXME:not implemented in libavformat
        1: 3161:    ap->channel = video_channel;
        1: 3162:    ap->standard = video_standard;
        -: 3163:
        1: 3164:    set_context_opts(ic, avformat_opts, AV_OPT_FLAG_DECODING_PARAM, NULL);
        -: 3165:
        1: 3166:    ic->video_codec_id   =
        -: 3167:        find_codec_or_die(video_codec_name   , AVMEDIA_TYPE_VIDEO   , 0,
        -: 3168:                          avcodec_opts[AVMEDIA_TYPE_VIDEO   ]->strict_std_compliance);
        1: 3169:    ic->audio_codec_id   =
        -: 3170:        find_codec_or_die(audio_codec_name   , AVMEDIA_TYPE_AUDIO   , 0,
        -: 3171:                          avcodec_opts[AVMEDIA_TYPE_AUDIO   ]->strict_std_compliance);
        1: 3172:    ic->subtitle_codec_id=
        -: 3173:        find_codec_or_die(subtitle_codec_name, AVMEDIA_TYPE_SUBTITLE, 0,
        -: 3174:                          avcodec_opts[AVMEDIA_TYPE_SUBTITLE]->strict_std_compliance);
        1: 3175:    ic->flags |= AVFMT_FLAG_NONBLOCK;
        -: 3176:
        -: 3177:    /* open the input file with generic libav function */
        1: 3178:    err = av_open_input_file(&ic, filename, file_iformat, 0, ap);
        1: 3179:    if (err < 0) {
    #####: 3180:        print_error(filename, err);
    #####: 3181:        ffmpeg_exit(1);
        -: 3182:    }
        1: 3183:    if(opt_programid) {
        -: 3184:        int i, j;
    #####: 3185:        int found=0;
    #####: 3186:        for(i=0; i<ic->nb_streams; i++){
    #####: 3187:            ic->streams[i]->discard= AVDISCARD_ALL;
        -: 3188:        }
    #####: 3189:        for(i=0; i<ic->nb_programs; i++){
    #####: 3190:            AVProgram *p= ic->programs[i];
    #####: 3191:            if(p->id != opt_programid){
    #####: 3192:                p->discard = AVDISCARD_ALL;
        -: 3193:            }else{
    #####: 3194:                found=1;
    #####: 3195:                for(j=0; j<p->nb_stream_indexes; j++){
    #####: 3196:                    ic->streams[p->stream_index[j]]->discard= AVDISCARD_DEFAULT;
        -: 3197:                }
        -: 3198:            }
        -: 3199:        }
    #####: 3200:        if(!found){
    #####: 3201:            fprintf(stderr, "Specified program id not found\n");
    #####: 3202:            ffmpeg_exit(1);
        -: 3203:        }
    #####: 3204:        opt_programid=0;
        -: 3205:    }
        -: 3206:
        1: 3207:    ic->loop_input = loop_input;
        -: 3208:
        -: 3209:    /* If not enough info to get the stream parameters, we decode the
        -: 3210:       first frames to get it. (used in mpeg case for example) */
        1: 3211:    ret = av_find_stream_info(ic);
        1: 3212:    if (ret < 0 && verbose >= 0) {
    #####: 3213:        fprintf(stderr, "%s: could not find codec parameters\n", filename);
    #####: 3214:        av_close_input_file(ic);
    #####: 3215:        ffmpeg_exit(1);
        -: 3216:    }
        -: 3217:
        1: 3218:    timestamp = start_time;
        -: 3219:    /* add the stream start time */
        1: 3220:    if (ic->start_time != AV_NOPTS_VALUE)
        1: 3221:        timestamp += ic->start_time;
        -: 3222:
        -: 3223:    /* if seeking requested, we execute it */
        1: 3224:    if (start_time != 0) {
    #####: 3225:        ret = av_seek_frame(ic, -1, timestamp, AVSEEK_FLAG_BACKWARD);
    #####: 3226:        if (ret < 0) {
    #####: 3227:            fprintf(stderr, "%s: could not seek to position %0.3f\n",
        -: 3228:                    filename, (double)timestamp / AV_TIME_BASE);
        -: 3229:        }
        -: 3230:        /* reset seek info */
    #####: 3231:        start_time = 0;
        -: 3232:    }
        -: 3233:
        -: 3234:    /* update the current parameters so that they match the one of the input stream */
        3: 3235:    for(i=0;i<ic->nb_streams;i++) {
        2: 3236:        AVStream *st = ic->streams[i];
        2: 3237:        AVCodecContext *dec = st->codec;
        2: 3238:        avcodec_thread_init(dec, thread_count);
        2: 3239:        input_codecs = grow_array(input_codecs, sizeof(*input_codecs), &nb_input_codecs, nb_input_codecs + 1);
        2: 3240:        switch (dec->codec_type) {
        -: 3241:        case AVMEDIA_TYPE_AUDIO:
        1: 3242:            input_codecs[nb_input_codecs-1] = avcodec_find_decoder_by_name(audio_codec_name);
        1: 3243:            set_context_opts(dec, avcodec_opts[AVMEDIA_TYPE_AUDIO], AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM, input_codecs[nb_input_codecs-1]);
        -: 3244:            //fprintf(stderr, "\nInput Audio channels: %d", dec->channels);
        1: 3245:            channel_layout    = dec->channel_layout;
        1: 3246:            audio_channels    = dec->channels;
        1: 3247:            audio_sample_rate = dec->sample_rate;
        1: 3248:            audio_sample_fmt  = dec->sample_fmt;
        1: 3249:            if(audio_disable)
    #####: 3250:                st->discard= AVDISCARD_ALL;
        -: 3251:            /* Note that av_find_stream_info can add more streams, and we
        -: 3252:             * currently have no chance of setting up lowres decoding
        -: 3253:             * early enough for them. */
        1: 3254:            if (dec->lowres)
    #####: 3255:                audio_sample_rate >>= dec->lowres;
        -: 3256:            break;
        -: 3257:        case AVMEDIA_TYPE_VIDEO:
        1: 3258:            input_codecs[nb_input_codecs-1] = avcodec_find_decoder_by_name(video_codec_name);
        1: 3259:            set_context_opts(dec, avcodec_opts[AVMEDIA_TYPE_VIDEO], AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM, input_codecs[nb_input_codecs-1]);
        1: 3260:            frame_height = dec->height;
        1: 3261:            frame_width  = dec->width;
        1: 3262:            if(ic->streams[i]->sample_aspect_ratio.num)
    #####: 3263:                frame_aspect_ratio=av_q2d(ic->streams[i]->sample_aspect_ratio);
        -: 3264:            else
        1: 3265:                frame_aspect_ratio=av_q2d(dec->sample_aspect_ratio);
        1: 3266:            frame_aspect_ratio *= (float) dec->width / dec->height;
        1: 3267:            frame_pix_fmt = dec->pix_fmt;
        1: 3268:            rfps      = ic->streams[i]->r_frame_rate.num;
        1: 3269:            rfps_base = ic->streams[i]->r_frame_rate.den;
        1: 3270:            if (dec->lowres) {
    #####: 3271:                dec->flags |= CODEC_FLAG_EMU_EDGE;
    #####: 3272:                frame_height >>= dec->lowres;
    #####: 3273:                frame_width  >>= dec->lowres;
    #####: 3274:                dec->height = frame_height;
    #####: 3275:                dec->width  = frame_width;
        -: 3276:            }
        1: 3277:            if(me_threshold)
    #####: 3278:                dec->debug |= FF_DEBUG_MV;
        -: 3279:
        1: 3280:            if (dec->time_base.den != rfps*dec->ticks_per_frame || dec->time_base.num != rfps_base) {
        -: 3281:
    #####: 3282:                if (verbose >= 0)
    #####: 3283:                    fprintf(stderr,"\nSeems stream %d codec frame rate differs from container frame rate: %2.2f (%d/%d) -> %2.2f (%d/%d)\n",
        -: 3284:                            i, (float)dec->time_base.den / dec->time_base.num, dec->time_base.den, dec->time_base.num,
        -: 3285:
        -: 3286:                    (float)rfps / rfps_base, rfps, rfps_base);
        -: 3287:            }
        -: 3288:            /* update the current frame rate to match the stream frame rate */
        1: 3289:            frame_rate.num = rfps;
        1: 3290:            frame_rate.den = rfps_base;
        -: 3291:
        1: 3292:            if(video_disable)
    #####: 3293:                st->discard= AVDISCARD_ALL;
        1: 3294:            else if(video_discard)
    #####: 3295:                st->discard= video_discard;
        -: 3296:            break;
        -: 3297:        case AVMEDIA_TYPE_DATA:
        -: 3298:            break;
        -: 3299:        case AVMEDIA_TYPE_SUBTITLE:
    #####: 3300:            input_codecs[nb_input_codecs-1] = avcodec_find_decoder_by_name(subtitle_codec_name);
    #####: 3301:            if(subtitle_disable)
    #####: 3302:                st->discard = AVDISCARD_ALL;
        -: 3303:            break;
        -: 3304:        case AVMEDIA_TYPE_ATTACHMENT:
        -: 3305:        case AVMEDIA_TYPE_UNKNOWN:
        -: 3306:            break;
        -: 3307:        default:
    #####: 3308:            abort();
        -: 3309:        }
        -: 3310:    }
        -: 3311:
        1: 3312:    input_files[nb_input_files] = ic;
        1: 3313:    input_files_ts_offset[nb_input_files] = input_ts_offset - (copy_ts ? 0 : timestamp);
        -: 3314:    /* dump the file content */
        1: 3315:    if (verbose >= 0)
        1: 3316:        dump_format(ic, nb_input_files, filename, 0);
        -: 3317:
        1: 3318:    nb_input_files++;
        -: 3319:
        1: 3320:    video_channel = 0;
        -: 3321:
        1: 3322:    av_freep(&video_codec_name);
        1: 3323:    av_freep(&audio_codec_name);
        1: 3324:    av_freep(&subtitle_codec_name);
        1: 3325:}
        -: 3326:
        -: 3327:static void check_audio_video_sub_inputs(int *has_video_ptr, int *has_audio_ptr,
        -: 3328:                                         int *has_subtitle_ptr)
        1: 3329:{
        -: 3330:    int has_video, has_audio, has_subtitle, i, j;
        -: 3331:    AVFormatContext *ic;
        -: 3332:
        1: 3333:    has_video = 0;
        1: 3334:    has_audio = 0;
        1: 3335:    has_subtitle = 0;
        2: 3336:    for(j=0;j<nb_input_files;j++) {
        1: 3337:        ic = input_files[j];
        3: 3338:        for(i=0;i<ic->nb_streams;i++) {
        2: 3339:            AVCodecContext *enc = ic->streams[i]->codec;
        2: 3340:            switch(enc->codec_type) {
        -: 3341:            case AVMEDIA_TYPE_AUDIO:
        1: 3342:                has_audio = 1;
        1: 3343:                break;
        -: 3344:            case AVMEDIA_TYPE_VIDEO:
        1: 3345:                has_video = 1;
        1: 3346:                break;
        -: 3347:            case AVMEDIA_TYPE_SUBTITLE:
    #####: 3348:                has_subtitle = 1;
    #####: 3349:                break;
        -: 3350:            case AVMEDIA_TYPE_DATA:
        -: 3351:            case AVMEDIA_TYPE_ATTACHMENT:
        -: 3352:            case AVMEDIA_TYPE_UNKNOWN:
        -: 3353:                break;
        -: 3354:            default:
    #####: 3355:                abort();
        -: 3356:            }
        -: 3357:        }
        -: 3358:    }
        1: 3359:    *has_video_ptr = has_video;
        1: 3360:    *has_audio_ptr = has_audio;
        1: 3361:    *has_subtitle_ptr = has_subtitle;
        1: 3362:}
        -: 3363:
        -: 3364:static void new_video_stream(AVFormatContext *oc, int file_idx)
        1: 3365:{
        -: 3366:    AVStream *st;
        -: 3367:    AVOutputStream *ost;
        -: 3368:    AVCodecContext *video_enc;
        1: 3369:    enum CodecID codec_id = CODEC_ID_NONE;
        1: 3370:    AVCodec *codec= NULL;
        -: 3371:
        1: 3372:    st = av_new_stream(oc, oc->nb_streams < nb_streamid_map ? streamid_map[oc->nb_streams] : 0);
        1: 3373:    if (!st) {
    #####: 3374:        fprintf(stderr, "Could not alloc stream\n");
    #####: 3375:        ffmpeg_exit(1);
        -: 3376:    }
        1: 3377:    ost = new_output_stream(oc, file_idx);
        -: 3378:
        1: 3379:    output_codecs = grow_array(output_codecs, sizeof(*output_codecs), &nb_output_codecs, nb_output_codecs + 1);
        1: 3380:    if(!video_stream_copy){
    #####: 3381:        if (video_codec_name) {
    #####: 3382:            codec_id = find_codec_or_die(video_codec_name, AVMEDIA_TYPE_VIDEO, 1,
        -: 3383:                                         avcodec_opts[AVMEDIA_TYPE_VIDEO]->strict_std_compliance);
    #####: 3384:            codec = avcodec_find_encoder_by_name(video_codec_name);
    #####: 3385:            output_codecs[nb_output_codecs-1] = codec;
        -: 3386:        } else {
    #####: 3387:            codec_id = av_guess_codec(oc->oformat, NULL, oc->filename, NULL, AVMEDIA_TYPE_VIDEO);
    #####: 3388:            codec = avcodec_find_encoder(codec_id);
        -: 3389:        }
        -: 3390:    }
        -: 3391:
        1: 3392:    avcodec_get_context_defaults3(st->codec, codec);
        1: 3393:    ost->bitstream_filters = video_bitstream_filters;
        1: 3394:    video_bitstream_filters= NULL;
        -: 3395:
        1: 3396:    avcodec_thread_init(st->codec, thread_count);
        -: 3397:
        1: 3398:    video_enc = st->codec;
        -: 3399:
        1: 3400:    if(video_codec_tag)
    #####: 3401:        video_enc->codec_tag= video_codec_tag;
        -: 3402:
        1: 3403:    if(   (video_global_header&1)
        -: 3404:       || (video_global_header==0 && (oc->oformat->flags & AVFMT_GLOBALHEADER))){
    #####: 3405:        video_enc->flags |= CODEC_FLAG_GLOBAL_HEADER;
    #####: 3406:        avcodec_opts[AVMEDIA_TYPE_VIDEO]->flags|= CODEC_FLAG_GLOBAL_HEADER;
        -: 3407:    }
        1: 3408:    if(video_global_header&2){
    #####: 3409:        video_enc->flags2 |= CODEC_FLAG2_LOCAL_HEADER;
    #####: 3410:        avcodec_opts[AVMEDIA_TYPE_VIDEO]->flags2|= CODEC_FLAG2_LOCAL_HEADER;
        -: 3411:    }
        -: 3412:
        1: 3413:    if (video_stream_copy) {
        1: 3414:        st->stream_copy = 1;
        1: 3415:        video_enc->codec_type = AVMEDIA_TYPE_VIDEO;
        1: 3416:        video_enc->sample_aspect_ratio =
        -: 3417:        st->sample_aspect_ratio = av_d2q(frame_aspect_ratio*frame_height/frame_width, 255);
        -: 3418:    } else {
        -: 3419:        const char *p;
        -: 3420:        int i;
    #####: 3421:        AVRational fps= frame_rate.num ? frame_rate : (AVRational){25,1};
        -: 3422:
    #####: 3423:        video_enc->codec_id = codec_id;
    #####: 3424:        set_context_opts(video_enc, avcodec_opts[AVMEDIA_TYPE_VIDEO], AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM, codec);
        -: 3425:
    #####: 3426:        if (codec && codec->supported_framerates && !force_fps)
    #####: 3427:            fps = codec->supported_framerates[av_find_nearest_q_idx(fps, codec->supported_framerates)];
    #####: 3428:        video_enc->time_base.den = fps.num;
    #####: 3429:        video_enc->time_base.num = fps.den;
        -: 3430:
    #####: 3431:        video_enc->width = frame_width;
    #####: 3432:        video_enc->height = frame_height;
    #####: 3433:        video_enc->sample_aspect_ratio = av_d2q(frame_aspect_ratio*video_enc->height/video_enc->width, 255);
    #####: 3434:        video_enc->pix_fmt = frame_pix_fmt;
    #####: 3435:        st->sample_aspect_ratio = video_enc->sample_aspect_ratio;
        -: 3436:
    #####: 3437:        choose_pixel_fmt(st, codec);
        -: 3438:
    #####: 3439:        if (intra_only)
    #####: 3440:            video_enc->gop_size = 0;
    #####: 3441:        if (video_qscale || same_quality) {
    #####: 3442:            video_enc->flags |= CODEC_FLAG_QSCALE;
    #####: 3443:            video_enc->global_quality=
        -: 3444:                st->quality = FF_QP2LAMBDA * video_qscale;
        -: 3445:        }
        -: 3446:
    #####: 3447:        if(intra_matrix)
    #####: 3448:            video_enc->intra_matrix = intra_matrix;
    #####: 3449:        if(inter_matrix)
    #####: 3450:            video_enc->inter_matrix = inter_matrix;
        -: 3451:
    #####: 3452:        p= video_rc_override_string;
    #####: 3453:        for(i=0; p; i++){
        -: 3454:            int start, end, q;
    #####: 3455:            int e=sscanf(p, "%d,%d,%d", &start, &end, &q);
    #####: 3456:            if(e!=3){
    #####: 3457:                fprintf(stderr, "error parsing rc_override\n");
    #####: 3458:                ffmpeg_exit(1);
        -: 3459:            }
    #####: 3460:            video_enc->rc_override=
        -: 3461:                av_realloc(video_enc->rc_override,
        -: 3462:                           sizeof(RcOverride)*(i+1));
    #####: 3463:            video_enc->rc_override[i].start_frame= start;
    #####: 3464:            video_enc->rc_override[i].end_frame  = end;
    #####: 3465:            if(q>0){
    #####: 3466:                video_enc->rc_override[i].qscale= q;
    #####: 3467:                video_enc->rc_override[i].quality_factor= 1.0;
        -: 3468:            }
        -: 3469:            else{
    #####: 3470:                video_enc->rc_override[i].qscale= 0;
    #####: 3471:                video_enc->rc_override[i].quality_factor= -q/100.0;
        -: 3472:            }
    #####: 3473:            p= strchr(p, '/');
    #####: 3474:            if(p) p++;
        -: 3475:        }
    #####: 3476:        video_enc->rc_override_count=i;
    #####: 3477:        if (!video_enc->rc_initial_buffer_occupancy)
    #####: 3478:            video_enc->rc_initial_buffer_occupancy = video_enc->rc_buffer_size*3/4;
    #####: 3479:        video_enc->me_threshold= me_threshold;
    #####: 3480:        video_enc->intra_dc_precision= intra_dc_precision - 8;
        -: 3481:
    #####: 3482:        if (do_psnr)
    #####: 3483:            video_enc->flags|= CODEC_FLAG_PSNR;
        -: 3484:
        -: 3485:        /* two pass mode */
    #####: 3486:        if (do_pass) {
    #####: 3487:            if (do_pass == 1) {
    #####: 3488:                video_enc->flags |= CODEC_FLAG_PASS1;
        -: 3489:            } else {
    #####: 3490:                video_enc->flags |= CODEC_FLAG_PASS2;
        -: 3491:            }
        -: 3492:        }
        -: 3493:
    #####: 3494:        if (forced_key_frames)
    #####: 3495:            parse_forced_key_frames(forced_key_frames, ost, video_enc);
        -: 3496:    }
        1: 3497:    if (video_language) {
    #####: 3498:        av_metadata_set2(&st->metadata, "language", video_language, 0);
    #####: 3499:        av_freep(&video_language);
        -: 3500:    }
        -: 3501:
        -: 3502:    /* reset some key parameters */
        1: 3503:    video_disable = 0;
        1: 3504:    av_freep(&video_codec_name);
        1: 3505:    av_freep(&forced_key_frames);
        1: 3506:    video_stream_copy = 0;
        1: 3507:    frame_pix_fmt = PIX_FMT_NONE;
        1: 3508:}
        -: 3509:
        -: 3510:static void new_audio_stream(AVFormatContext *oc, int file_idx)
        1: 3511:{
        -: 3512:    AVStream *st;
        -: 3513:    AVOutputStream *ost;
        1: 3514:    AVCodec *codec= NULL;
        -: 3515:    AVCodecContext *audio_enc;
        1: 3516:    enum CodecID codec_id = CODEC_ID_NONE;
        -: 3517:
        1: 3518:    st = av_new_stream(oc, oc->nb_streams < nb_streamid_map ? streamid_map[oc->nb_streams] : 0);
        1: 3519:    if (!st) {
    #####: 3520:        fprintf(stderr, "Could not alloc stream\n");
    #####: 3521:        ffmpeg_exit(1);
        -: 3522:    }
        1: 3523:    ost = new_output_stream(oc, file_idx);
        -: 3524:
        1: 3525:    output_codecs = grow_array(output_codecs, sizeof(*output_codecs), &nb_output_codecs, nb_output_codecs + 1);
        1: 3526:    if(!audio_stream_copy){
    #####: 3527:        if (audio_codec_name) {
    #####: 3528:            codec_id = find_codec_or_die(audio_codec_name, AVMEDIA_TYPE_AUDIO, 1,
        -: 3529:                                         avcodec_opts[AVMEDIA_TYPE_AUDIO]->strict_std_compliance);
    #####: 3530:            codec = avcodec_find_encoder_by_name(audio_codec_name);
    #####: 3531:            output_codecs[nb_output_codecs-1] = codec;
        -: 3532:        } else {
    #####: 3533:            codec_id = av_guess_codec(oc->oformat, NULL, oc->filename, NULL, AVMEDIA_TYPE_AUDIO);
    #####: 3534:            codec = avcodec_find_encoder(codec_id);
        -: 3535:        }
        -: 3536:    }
        -: 3537:
        1: 3538:    avcodec_get_context_defaults3(st->codec, codec);
        -: 3539:
        1: 3540:    ost->bitstream_filters = audio_bitstream_filters;
        1: 3541:    audio_bitstream_filters= NULL;
        -: 3542:
        1: 3543:    avcodec_thread_init(st->codec, thread_count);
        -: 3544:
        1: 3545:    audio_enc = st->codec;
        1: 3546:    audio_enc->codec_type = AVMEDIA_TYPE_AUDIO;
        -: 3547:
        1: 3548:    if(audio_codec_tag)
    #####: 3549:        audio_enc->codec_tag= audio_codec_tag;
        -: 3550:
        1: 3551:    if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
    #####: 3552:        audio_enc->flags |= CODEC_FLAG_GLOBAL_HEADER;
    #####: 3553:        avcodec_opts[AVMEDIA_TYPE_AUDIO]->flags|= CODEC_FLAG_GLOBAL_HEADER;
        -: 3554:    }
        1: 3555:    if (audio_stream_copy) {
        1: 3556:        st->stream_copy = 1;
        1: 3557:        audio_enc->channels = audio_channels;
        1: 3558:        audio_enc->sample_rate = audio_sample_rate;
        -: 3559:    } else {
    #####: 3560:        audio_enc->codec_id = codec_id;
    #####: 3561:        set_context_opts(audio_enc, avcodec_opts[AVMEDIA_TYPE_AUDIO], AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM, codec);
        -: 3562:
    #####: 3563:        if (audio_qscale > QSCALE_NONE) {
    #####: 3564:            audio_enc->flags |= CODEC_FLAG_QSCALE;
    #####: 3565:            audio_enc->global_quality = st->quality = FF_QP2LAMBDA * audio_qscale;
        -: 3566:        }
    #####: 3567:        audio_enc->channels = audio_channels;
    #####: 3568:        audio_enc->sample_fmt = audio_sample_fmt;
    #####: 3569:        audio_enc->sample_rate = audio_sample_rate;
    #####: 3570:        audio_enc->channel_layout = channel_layout;
    #####: 3571:        if (av_get_channel_layout_nb_channels(channel_layout) != audio_channels)
    #####: 3572:            audio_enc->channel_layout = 0;
        -: 3573:        choose_sample_fmt(st, codec);
    #####: 3574:        choose_sample_rate(st, codec);
        -: 3575:    }
        1: 3576:    audio_enc->time_base= (AVRational){1, audio_sample_rate};
        1: 3577:    if (audio_language) {
    #####: 3578:        av_metadata_set2(&st->metadata, "language", audio_language, 0);
    #####: 3579:        av_freep(&audio_language);
        -: 3580:    }
        -: 3581:
        -: 3582:    /* reset some key parameters */
        1: 3583:    audio_disable = 0;
        1: 3584:    av_freep(&audio_codec_name);
        1: 3585:    audio_stream_copy = 0;
        1: 3586:}
        -: 3587:
        -: 3588:static void new_subtitle_stream(AVFormatContext *oc, int file_idx)
    #####: 3589:{
        -: 3590:    AVStream *st;
        -: 3591:    AVOutputStream *ost;
    #####: 3592:    AVCodec *codec=NULL;
        -: 3593:    AVCodecContext *subtitle_enc;
    #####: 3594:    enum CodecID codec_id = CODEC_ID_NONE;
        -: 3595:
    #####: 3596:    st = av_new_stream(oc, oc->nb_streams < nb_streamid_map ? streamid_map[oc->nb_streams] : 0);
    #####: 3597:    if (!st) {
    #####: 3598:        fprintf(stderr, "Could not alloc stream\n");
    #####: 3599:        ffmpeg_exit(1);
        -: 3600:    }
    #####: 3601:    ost = new_output_stream(oc, file_idx);
    #####: 3602:    subtitle_enc = st->codec;
    #####: 3603:    output_codecs = grow_array(output_codecs, sizeof(*output_codecs), &nb_output_codecs, nb_output_codecs + 1);
    #####: 3604:    if(!subtitle_stream_copy){
    #####: 3605:        if (subtitle_codec_name) {
    #####: 3606:            codec_id = find_codec_or_die(subtitle_codec_name, AVMEDIA_TYPE_SUBTITLE, 1,
        -: 3607:                                         avcodec_opts[AVMEDIA_TYPE_SUBTITLE]->strict_std_compliance);
    #####: 3608:            codec= output_codecs[nb_output_codecs-1] = avcodec_find_encoder_by_name(subtitle_codec_name);
        -: 3609:        } else {
    #####: 3610:            codec_id = av_guess_codec(oc->oformat, NULL, oc->filename, NULL, AVMEDIA_TYPE_SUBTITLE);
    #####: 3611:            codec = avcodec_find_encoder(codec_id);
        -: 3612:        }
        -: 3613:    }
    #####: 3614:    avcodec_get_context_defaults3(st->codec, codec);
        -: 3615:
    #####: 3616:    ost->bitstream_filters = subtitle_bitstream_filters;
    #####: 3617:    subtitle_bitstream_filters= NULL;
        -: 3618:
    #####: 3619:    subtitle_enc->codec_type = AVMEDIA_TYPE_SUBTITLE;
        -: 3620:
    #####: 3621:    if(subtitle_codec_tag)
    #####: 3622:        subtitle_enc->codec_tag= subtitle_codec_tag;
        -: 3623:
    #####: 3624:    if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
    #####: 3625:        subtitle_enc->flags |= CODEC_FLAG_GLOBAL_HEADER;
    #####: 3626:        avcodec_opts[AVMEDIA_TYPE_SUBTITLE]->flags |= CODEC_FLAG_GLOBAL_HEADER;
        -: 3627:    }
    #####: 3628:    if (subtitle_stream_copy) {
    #####: 3629:        st->stream_copy = 1;
        -: 3630:    } else {
    #####: 3631:        subtitle_enc->codec_id = codec_id;
    #####: 3632:        set_context_opts(avcodec_opts[AVMEDIA_TYPE_SUBTITLE], subtitle_enc, AV_OPT_FLAG_SUBTITLE_PARAM | AV_OPT_FLAG_ENCODING_PARAM, codec);
        -: 3633:    }
        -: 3634:
    #####: 3635:    if (subtitle_language) {
    #####: 3636:        av_metadata_set2(&st->metadata, "language", subtitle_language, 0);
    #####: 3637:        av_freep(&subtitle_language);
        -: 3638:    }
        -: 3639:
    #####: 3640:    subtitle_disable = 0;
    #####: 3641:    av_freep(&subtitle_codec_name);
    #####: 3642:    subtitle_stream_copy = 0;
    #####: 3643:}
        -: 3644:
        -: 3645:static int opt_new_stream(const char *opt, const char *arg)
    #####: 3646:{
        -: 3647:    AVFormatContext *oc;
    #####: 3648:    int file_idx = nb_output_files - 1;
    #####: 3649:    if (nb_output_files <= 0) {
    #####: 3650:        fprintf(stderr, "At least one output file must be specified\n");
    #####: 3651:        ffmpeg_exit(1);
        -: 3652:    }
    #####: 3653:    oc = output_files[file_idx];
        -: 3654:
    #####: 3655:    if      (!strcmp(opt, "newvideo"   )) new_video_stream   (oc, file_idx);
    #####: 3656:    else if (!strcmp(opt, "newaudio"   )) new_audio_stream   (oc, file_idx);
    #####: 3657:    else if (!strcmp(opt, "newsubtitle")) new_subtitle_stream(oc, file_idx);
    #####: 3658:    else av_assert0(0);
    #####: 3659:    return 0;
        -: 3660:}
        -: 3661:
        -: 3662:/* arg format is "output-stream-index:streamid-value". */
        -: 3663:static int opt_streamid(const char *opt, const char *arg)
    #####: 3664:{
        -: 3665:    int idx;
        -: 3666:    char *p;
        -: 3667:    char idx_str[16];
        -: 3668:
    #####: 3669:    strncpy(idx_str, arg, sizeof(idx_str));
    #####: 3670:    idx_str[sizeof(idx_str)-1] = '\0';
    #####: 3671:    p = strchr(idx_str, ':');
    #####: 3672:    if (!p) {
    #####: 3673:        fprintf(stderr,
        -: 3674:                "Invalid value '%s' for option '%s', required syntax is 'index:value'\n",
        -: 3675:                arg, opt);
    #####: 3676:        ffmpeg_exit(1);
        -: 3677:    }
    #####: 3678:    *p++ = '\0';
    #####: 3679:    idx = parse_number_or_die(opt, idx_str, OPT_INT, 0, MAX_STREAMS-1);
    #####: 3680:    streamid_map = grow_array(streamid_map, sizeof(*streamid_map), &nb_streamid_map, idx+1);
    #####: 3681:    streamid_map[idx] = parse_number_or_die(opt, p, OPT_INT, 0, INT_MAX);
    #####: 3682:    return 0;
        -: 3683:}
        -: 3684:
        -: 3685:static void opt_output_file(const char *filename)
        1: 3686:{
        -: 3687:    AVFormatContext *oc;
        -: 3688:    int err, use_video, use_audio, use_subtitle;
        -: 3689:    int input_has_video, input_has_audio, input_has_subtitle;
        1: 3690:    AVFormatParameters params, *ap = &params;
        -: 3691:    AVOutputFormat *file_oformat;
        -: 3692:
        1: 3693:    if (!strcmp(filename, "-"))
    #####: 3694:        filename = "pipe:";
        -: 3695:
        1: 3696:    oc = avformat_alloc_context();
        1: 3697:    if (!oc) {
    #####: 3698:        print_error(filename, AVERROR(ENOMEM));
    #####: 3699:        ffmpeg_exit(1);
        -: 3700:    }
        -: 3701:
        1: 3702:    if (last_asked_format) {
    #####: 3703:        file_oformat = av_guess_format(last_asked_format, NULL, NULL);
    #####: 3704:        if (!file_oformat) {
    #####: 3705:            fprintf(stderr, "Requested output format '%s' is not a suitable output format\n", last_asked_format);
    #####: 3706:            ffmpeg_exit(1);
        -: 3707:        }
    #####: 3708:        last_asked_format = NULL;
        -: 3709:    } else {
        1: 3710:        file_oformat = av_guess_format(NULL, filename, NULL);
        1: 3711:        if (!file_oformat) {
    #####: 3712:            fprintf(stderr, "Unable to find a suitable output format for '%s'\n",
        -: 3713:                    filename);
    #####: 3714:            ffmpeg_exit(1);
        -: 3715:        }
        -: 3716:    }
        -: 3717:
        1: 3718:    oc->oformat = file_oformat;
        1: 3719:    av_strlcpy(oc->filename, filename, sizeof(oc->filename));
        -: 3720:
        1: 3721:    if (!strcmp(file_oformat->name, "ffm") &&
        -: 3722:        av_strstart(filename, "http:", NULL)) {
        -: 3723:        /* special case for files sent to ffserver: we get the stream
        -: 3724:           parameters from ffserver */
    #####: 3725:        int err = read_ffserver_streams(oc, filename);
    #####: 3726:        if (err < 0) {
    #####: 3727:            print_error(filename, err);
    #####: 3728:            ffmpeg_exit(1);
        -: 3729:        }
        -: 3730:    } else {
        1: 3731:        use_video = file_oformat->video_codec != CODEC_ID_NONE || video_stream_copy || video_codec_name;
        1: 3732:        use_audio = file_oformat->audio_codec != CODEC_ID_NONE || audio_stream_copy || audio_codec_name;
        1: 3733:        use_subtitle = file_oformat->subtitle_codec != CODEC_ID_NONE || subtitle_stream_copy || subtitle_codec_name;
        -: 3734:
        -: 3735:        /* disable if no corresponding type found and at least one
        -: 3736:           input file */
        1: 3737:        if (nb_input_files > 0) {
        1: 3738:            check_audio_video_sub_inputs(&input_has_video, &input_has_audio,
        -: 3739:                                         &input_has_subtitle);
        1: 3740:            if (!input_has_video)
    #####: 3741:                use_video = 0;
        1: 3742:            if (!input_has_audio)
    #####: 3743:                use_audio = 0;
        1: 3744:            if (!input_has_subtitle)
        1: 3745:                use_subtitle = 0;
        -: 3746:        }
        -: 3747:
        -: 3748:        /* manual disable */
        1: 3749:        if (audio_disable)    use_audio    = 0;
        1: 3750:        if (video_disable)    use_video    = 0;
        1: 3751:        if (subtitle_disable) use_subtitle = 0;
        -: 3752:
        1: 3753:        if (use_video)    new_video_stream(oc, nb_output_files);
        1: 3754:        if (use_audio)    new_audio_stream(oc, nb_output_files);
        1: 3755:        if (use_subtitle) new_subtitle_stream(oc, nb_output_files);
        -: 3756:
        1: 3757:        oc->timestamp = recording_timestamp;
        -: 3758:
        1: 3759:        av_metadata_copy(&oc->metadata, metadata, 0);
        1: 3760:        av_metadata_free(&metadata);
        -: 3761:    }
        -: 3762:
        1: 3763:    output_files[nb_output_files++] = oc;
        -: 3764:
        -: 3765:    /* check filename in case of an image number is expected */
        1: 3766:    if (oc->oformat->flags & AVFMT_NEEDNUMBER) {
    #####: 3767:        if (!av_filename_number_test(oc->filename)) {
    #####: 3768:            print_error(oc->filename, AVERROR_NUMEXPECTED);
    #####: 3769:            ffmpeg_exit(1);
        -: 3770:        }
        -: 3771:    }
        -: 3772:
        1: 3773:    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        -: 3774:        /* test if it already exists to avoid loosing precious files */
        1: 3775:        if (!file_overwrite &&
        -: 3776:            (strchr(filename, ':') == NULL ||
        -: 3777:             filename[1] == ':' ||
        -: 3778:             av_strstart(filename, "file:", NULL))) {
    #####: 3779:            if (url_exist(filename)) {
    #####: 3780:                if (!using_stdin) {
    #####: 3781:                    fprintf(stderr,"File '%s' already exists. Overwrite ? [y/N] ", filename);
    #####: 3782:                    fflush(stderr);
    #####: 3783:                    if (!read_yesno()) {
    #####: 3784:                        fprintf(stderr, "Not overwriting - exiting\n");
    #####: 3785:                        ffmpeg_exit(1);
        -: 3786:                    }
        -: 3787:                }
        -: 3788:                else {
    #####: 3789:                    fprintf(stderr,"File '%s' already exists. Exiting.\n", filename);
    #####: 3790:                    ffmpeg_exit(1);
        -: 3791:                }
        -: 3792:            }
        -: 3793:        }
        -: 3794:
        -: 3795:        /* open the file */
        1: 3796:        if ((err = url_fopen(&oc->pb, filename, URL_WRONLY)) < 0) {
    #####: 3797:            print_error(filename, err);
    #####: 3798:            ffmpeg_exit(1);
        -: 3799:        }
        -: 3800:    }
        -: 3801:
        1: 3802:    memset(ap, 0, sizeof(*ap));
        1: 3803:    if (av_set_parameters(oc, ap) < 0) {
    #####: 3804:        fprintf(stderr, "%s: Invalid encoding parameters\n",
        -: 3805:                oc->filename);
    #####: 3806:        ffmpeg_exit(1);
        -: 3807:    }
        -: 3808:
        1: 3809:    oc->preload= (int)(mux_preload*AV_TIME_BASE);
        1: 3810:    oc->max_delay= (int)(mux_max_delay*AV_TIME_BASE);
        1: 3811:    oc->loop_output = loop_output;
        1: 3812:    oc->flags |= AVFMT_FLAG_NONBLOCK;
        -: 3813:
        1: 3814:    set_context_opts(oc, avformat_opts, AV_OPT_FLAG_ENCODING_PARAM, NULL);
        -: 3815:
        1: 3816:    nb_streamid_map = 0;
        1: 3817:    av_freep(&forced_key_frames);
        1: 3818:}
        -: 3819:
        -: 3820:/* same option as mencoder */
        -: 3821:static void opt_pass(const char *pass_str)
    #####: 3822:{
        -: 3823:    int pass;
    #####: 3824:    pass = atoi(pass_str);
    #####: 3825:    if (pass != 1 && pass != 2) {
    #####: 3826:        fprintf(stderr, "pass number can be only 1 or 2\n");
    #####: 3827:        ffmpeg_exit(1);
        -: 3828:    }
    #####: 3829:    do_pass = pass;
    #####: 3830:}
        -: 3831:
        -: 3832:static int64_t getutime(void)
        2: 3833:{
        -: 3834:#if HAVE_GETRUSAGE
        -: 3835:    struct rusage rusage;
        -: 3836:
        2: 3837:    getrusage(RUSAGE_SELF, &rusage);
        2: 3838:    return (rusage.ru_utime.tv_sec * 1000000LL) + rusage.ru_utime.tv_usec;
        -: 3839:#elif HAVE_GETPROCESSTIMES
        -: 3840:    HANDLE proc;
        -: 3841:    FILETIME c, e, k, u;
        -: 3842:    proc = GetCurrentProcess();
        -: 3843:    GetProcessTimes(proc, &c, &e, &k, &u);
        -: 3844:    return ((int64_t) u.dwHighDateTime << 32 | u.dwLowDateTime) / 10;
        -: 3845:#else
        -: 3846:    return av_gettime();
        -: 3847:#endif
        -: 3848:}
        -: 3849:
        -: 3850:static int64_t getmaxrss(void)
    #####: 3851:{
        -: 3852:#if HAVE_GETRUSAGE && HAVE_STRUCT_RUSAGE_RU_MAXRSS
        -: 3853:    struct rusage rusage;
        -: 3854:    getrusage(RUSAGE_SELF, &rusage);
        -: 3855:    return (int64_t)rusage.ru_maxrss * 1024;
        -: 3856:#elif HAVE_GETPROCESSMEMORYINFO
        -: 3857:    HANDLE proc;
        -: 3858:    PROCESS_MEMORY_COUNTERS memcounters;
        -: 3859:    proc = GetCurrentProcess();
        -: 3860:    memcounters.cb = sizeof(memcounters);
        -: 3861:    GetProcessMemoryInfo(proc, &memcounters, sizeof(memcounters));
        -: 3862:    return memcounters.PeakPagefileUsage;
        -: 3863:#else
    #####: 3864:    return 0;
        -: 3865:#endif
        -: 3866:}
        -: 3867:
        -: 3868:static void parse_matrix_coeffs(uint16_t *dest, const char *str)
    #####: 3869:{
        -: 3870:    int i;
    #####: 3871:    const char *p = str;
    #####: 3872:    for(i = 0;; i++) {
    #####: 3873:        dest[i] = atoi(p);
    #####: 3874:        if(i == 63)
    #####: 3875:            break;
    #####: 3876:        p = strchr(p, ',');
    #####: 3877:        if(!p) {
    #####: 3878:            fprintf(stderr, "Syntax error in matrix \"%s\" at coeff %d\n", str, i);
    #####: 3879:            ffmpeg_exit(1);
        -: 3880:        }
    #####: 3881:        p++;
    #####: 3882:    }
    #####: 3883:}
        -: 3884:
        -: 3885:static void opt_inter_matrix(const char *arg)
    #####: 3886:{
    #####: 3887:    inter_matrix = av_mallocz(sizeof(uint16_t) * 64);
    #####: 3888:    parse_matrix_coeffs(inter_matrix, arg);
    #####: 3889:}
        -: 3890:
        -: 3891:static void opt_intra_matrix(const char *arg)
    #####: 3892:{
    #####: 3893:    intra_matrix = av_mallocz(sizeof(uint16_t) * 64);
    #####: 3894:    parse_matrix_coeffs(intra_matrix, arg);
    #####: 3895:}
        -: 3896:
        -: 3897:static void show_usage(void)
    #####: 3898:{
    #####: 3899:    printf("Hyper fast Audio and Video encoder\n");
    #####: 3900:    printf("usage: ffmpeg [options] [[infile options] -i infile]... {[outfile options] outfile}...\n");
    #####: 3901:    printf("\n");
    #####: 3902:}
        -: 3903:
        -: 3904:static void show_help(void)
    #####: 3905:{
        -: 3906:    AVCodec *c;
    #####: 3907:    AVOutputFormat *oformat = NULL;
        -: 3908:
    #####: 3909:    av_log_set_callback(log_callback_help);
    #####: 3910:    show_usage();
    #####: 3911:    show_help_options(options, "Main options:\n",
        -: 3912:                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_SUBTITLE | OPT_GRAB, 0);
    #####: 3913:    show_help_options(options, "\nAdvanced options:\n",
        -: 3914:                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_SUBTITLE | OPT_GRAB,
        -: 3915:                      OPT_EXPERT);
    #####: 3916:    show_help_options(options, "\nVideo options:\n",
        -: 3917:                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_GRAB,
        -: 3918:                      OPT_VIDEO);
    #####: 3919:    show_help_options(options, "\nAdvanced Video options:\n",
        -: 3920:                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_GRAB,
        -: 3921:                      OPT_VIDEO | OPT_EXPERT);
    #####: 3922:    show_help_options(options, "\nAudio options:\n",
        -: 3923:                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_GRAB,
        -: 3924:                      OPT_AUDIO);
    #####: 3925:    show_help_options(options, "\nAdvanced Audio options:\n",
        -: 3926:                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_GRAB,
        -: 3927:                      OPT_AUDIO | OPT_EXPERT);
    #####: 3928:    show_help_options(options, "\nSubtitle options:\n",
        -: 3929:                      OPT_SUBTITLE | OPT_GRAB,
        -: 3930:                      OPT_SUBTITLE);
    #####: 3931:    show_help_options(options, "\nAudio/Video grab options:\n",
        -: 3932:                      OPT_GRAB,
        -: 3933:                      OPT_GRAB);
    #####: 3934:    printf("\n");
    #####: 3935:    av_opt_show2(avcodec_opts[0], NULL, AV_OPT_FLAG_ENCODING_PARAM|AV_OPT_FLAG_DECODING_PARAM, 0);
    #####: 3936:    printf("\n");
        -: 3937:
        -: 3938:    /* individual codec options */
    #####: 3939:    c = NULL;
    #####: 3940:    while ((c = av_codec_next(c))) {
    #####: 3941:        if (c->priv_class) {
    #####: 3942:            av_opt_show2(&c->priv_class, NULL, AV_OPT_FLAG_ENCODING_PARAM|AV_OPT_FLAG_DECODING_PARAM, 0);
    #####: 3943:            printf("\n");
        -: 3944:        }
        -: 3945:    }
        -: 3946:
    #####: 3947:    av_opt_show2(avformat_opts, NULL, AV_OPT_FLAG_ENCODING_PARAM|AV_OPT_FLAG_DECODING_PARAM, 0);
    #####: 3948:    printf("\n");
        -: 3949:
        -: 3950:    /* individual muxer options */
    #####: 3951:    while ((oformat = av_oformat_next(oformat))) {
    #####: 3952:        if (oformat->priv_class) {
    #####: 3953:            av_opt_show2(&oformat->priv_class, NULL, AV_OPT_FLAG_ENCODING_PARAM, 0);
    #####: 3954:            printf("\n");
        -: 3955:        }
        -: 3956:    }
        -: 3957:
    #####: 3958:    av_opt_show2(sws_opts, NULL, AV_OPT_FLAG_ENCODING_PARAM|AV_OPT_FLAG_DECODING_PARAM, 0);
    #####: 3959:}
        -: 3960:
        -: 3961:static void opt_target(const char *arg)
    #####: 3962:{
    #####: 3963:    enum { PAL, NTSC, FILM, UNKNOWN } norm = UNKNOWN;
        -: 3964:    static const char *const frame_rates[] = {"25", "30000/1001", "24000/1001"};
        -: 3965:
    #####: 3966:    if(!strncmp(arg, "pal-", 4)) {
    #####: 3967:        norm = PAL;
    #####: 3968:        arg += 4;
    #####: 3969:    } else if(!strncmp(arg, "ntsc-", 5)) {
    #####: 3970:        norm = NTSC;
    #####: 3971:        arg += 5;
    #####: 3972:    } else if(!strncmp(arg, "film-", 5)) {
    #####: 3973:        norm = FILM;
    #####: 3974:        arg += 5;
        -: 3975:    } else {
        -: 3976:        int fr;
        -: 3977:        /* Calculate FR via float to avoid int overflow */
    #####: 3978:        fr = (int)(frame_rate.num * 1000.0 / frame_rate.den);
    #####: 3979:        if(fr == 25000) {
    #####: 3980:            norm = PAL;
    #####: 3981:        } else if((fr == 29970) || (fr == 23976)) {
    #####: 3982:            norm = NTSC;
        -: 3983:        } else {
        -: 3984:            /* Try to determine PAL/NTSC by peeking in the input files */
    #####: 3985:            if(nb_input_files) {
        -: 3986:                int i, j;
    #####: 3987:                for(j = 0; j < nb_input_files; j++) {
    #####: 3988:                    for(i = 0; i < input_files[j]->nb_streams; i++) {
    #####: 3989:                        AVCodecContext *c = input_files[j]->streams[i]->codec;
    #####: 3990:                        if(c->codec_type != AVMEDIA_TYPE_VIDEO)
    #####: 3991:                            continue;
    #####: 3992:                        fr = c->time_base.den * 1000 / c->time_base.num;
    #####: 3993:                        if(fr == 25000) {
    #####: 3994:                            norm = PAL;
    #####: 3995:                            break;
    #####: 3996:                        } else if((fr == 29970) || (fr == 23976)) {
    #####: 3997:                            norm = NTSC;
    #####: 3998:                            break;
        -: 3999:                        }
        -: 4000:                    }
    #####: 4001:                    if(norm != UNKNOWN)
    #####: 4002:                        break;
        -: 4003:                }
        -: 4004:            }
        -: 4005:        }
    #####: 4006:        if(verbose && norm != UNKNOWN)
    #####: 4007:            fprintf(stderr, "Assuming %s for target.\n", norm == PAL ? "PAL" : "NTSC");
        -: 4008:    }
        -: 4009:
    #####: 4010:    if(norm == UNKNOWN) {
    #####: 4011:        fprintf(stderr, "Could not determine norm (PAL/NTSC/NTSC-Film) for target.\n");
    #####: 4012:        fprintf(stderr, "Please prefix target with \"pal-\", \"ntsc-\" or \"film-\",\n");
    #####: 4013:        fprintf(stderr, "or set a framerate with \"-r xxx\".\n");
    #####: 4014:        ffmpeg_exit(1);
        -: 4015:    }
        -: 4016:
    #####: 4017:    if(!strcmp(arg, "vcd")) {
        -: 4018:
    #####: 4019:        opt_video_codec("mpeg1video");
    #####: 4020:        opt_audio_codec("mp2");
    #####: 4021:        opt_format("vcd");
        -: 4022:
    #####: 4023:        opt_frame_size(norm == PAL ? "352x288" : "352x240");
    #####: 4024:        opt_frame_rate(NULL, frame_rates[norm]);
    #####: 4025:        opt_default("g", norm == PAL ? "15" : "18");
        -: 4026:
    #####: 4027:        opt_default("b", "1150000");
    #####: 4028:        opt_default("maxrate", "1150000");
    #####: 4029:        opt_default("minrate", "1150000");
    #####: 4030:        opt_default("bufsize", "327680"); // 40*1024*8;
        -: 4031:
    #####: 4032:        opt_default("ab", "224000");
    #####: 4033:        audio_sample_rate = 44100;
    #####: 4034:        audio_channels = 2;
        -: 4035:
    #####: 4036:        opt_default("packetsize", "2324");
    #####: 4037:        opt_default("muxrate", "1411200"); // 2352 * 75 * 8;
        -: 4038:
        -: 4039:        /* We have to offset the PTS, so that it is consistent with the SCR.
        -: 4040:           SCR starts at 36000, but the first two packs contain only padding
        -: 4041:           and the first pack from the other stream, respectively, may also have
        -: 4042:           been written before.
        -: 4043:           So the real data starts at SCR 36000+3*1200. */
    #####: 4044:        mux_preload= (36000+3*1200) / 90000.0; //0.44
    #####: 4045:    } else if(!strcmp(arg, "svcd")) {
        -: 4046:
    #####: 4047:        opt_video_codec("mpeg2video");
    #####: 4048:        opt_audio_codec("mp2");
    #####: 4049:        opt_format("svcd");
        -: 4050:
    #####: 4051:        opt_frame_size(norm == PAL ? "480x576" : "480x480");
    #####: 4052:        opt_frame_rate(NULL, frame_rates[norm]);
    #####: 4053:        opt_default("g", norm == PAL ? "15" : "18");
        -: 4054:
    #####: 4055:        opt_default("b", "2040000");
    #####: 4056:        opt_default("maxrate", "2516000");
    #####: 4057:        opt_default("minrate", "0"); //1145000;
    #####: 4058:        opt_default("bufsize", "1835008"); //224*1024*8;
    #####: 4059:        opt_default("flags", "+scan_offset");
        -: 4060:
        -: 4061:
    #####: 4062:        opt_default("ab", "224000");
    #####: 4063:        audio_sample_rate = 44100;
        -: 4064:
    #####: 4065:        opt_default("packetsize", "2324");
        -: 4066:
    #####: 4067:    } else if(!strcmp(arg, "dvd")) {
        -: 4068:
    #####: 4069:        opt_video_codec("mpeg2video");
    #####: 4070:        opt_audio_codec("ac3");
    #####: 4071:        opt_format("dvd");
        -: 4072:
    #####: 4073:        opt_frame_size(norm == PAL ? "720x576" : "720x480");
    #####: 4074:        opt_frame_rate(NULL, frame_rates[norm]);
    #####: 4075:        opt_default("g", norm == PAL ? "15" : "18");
        -: 4076:
    #####: 4077:        opt_default("b", "6000000");
    #####: 4078:        opt_default("maxrate", "9000000");
    #####: 4079:        opt_default("minrate", "0"); //1500000;
    #####: 4080:        opt_default("bufsize", "1835008"); //224*1024*8;
        -: 4081:
    #####: 4082:        opt_default("packetsize", "2048");  // from www.mpucoder.com: DVD sectors contain 2048 bytes of data, this is also the size of one pack.
    #####: 4083:        opt_default("muxrate", "10080000"); // from mplex project: data_rate = 1260000. mux_rate = data_rate * 8
        -: 4084:
    #####: 4085:        opt_default("ab", "448000");
    #####: 4086:        audio_sample_rate = 48000;
        -: 4087:
    #####: 4088:    } else if(!strncmp(arg, "dv", 2)) {
        -: 4089:
    #####: 4090:        opt_format("dv");
        -: 4091:
    #####: 4092:        opt_frame_size(norm == PAL ? "720x576" : "720x480");
    #####: 4093:        opt_frame_pix_fmt(!strncmp(arg, "dv50", 4) ? "yuv422p" :
        -: 4094:                          (norm == PAL ? "yuv420p" : "yuv411p"));
    #####: 4095:        opt_frame_rate(NULL, frame_rates[norm]);
        -: 4096:
    #####: 4097:        audio_sample_rate = 48000;
    #####: 4098:        audio_channels = 2;
        -: 4099:
        -: 4100:    } else {
    #####: 4101:        fprintf(stderr, "Unknown target: %s\n", arg);
    #####: 4102:        ffmpeg_exit(1);
        -: 4103:    }
    #####: 4104:}
        -: 4105:
        -: 4106:static void opt_vstats_file (const char *arg)
    #####: 4107:{
    #####: 4108:    av_free (vstats_filename);
    #####: 4109:    vstats_filename=av_strdup (arg);
    #####: 4110:}
        -: 4111:
        -: 4112:static void opt_vstats (void)
    #####: 4113:{
        -: 4114:    char filename[40];
    #####: 4115:    time_t today2 = time(NULL);
    #####: 4116:    struct tm *today = localtime(&today2);
        -: 4117:
    #####: 4118:    snprintf(filename, sizeof(filename), "vstats_%02d%02d%02d.log", today->tm_hour, today->tm_min,
        -: 4119:             today->tm_sec);
    #####: 4120:    opt_vstats_file(filename);
    #####: 4121:}
        -: 4122:
        -: 4123:static int opt_bsf(const char *opt, const char *arg)
    #####: 4124:{
    #####: 4125:    AVBitStreamFilterContext *bsfc= av_bitstream_filter_init(arg); //FIXME split name and args for filter at '='
        -: 4126:    AVBitStreamFilterContext **bsfp;
        -: 4127:
    #####: 4128:    if(!bsfc){
    #####: 4129:        fprintf(stderr, "Unknown bitstream filter %s\n", arg);
    #####: 4130:        ffmpeg_exit(1);
        -: 4131:    }
        -: 4132:
    #####: 4133:    bsfp= *opt == 'v' ? &video_bitstream_filters :
        -: 4134:          *opt == 'a' ? &audio_bitstream_filters :
        -: 4135:                        &subtitle_bitstream_filters;
    #####: 4136:    while(*bsfp)
    #####: 4137:        bsfp= &(*bsfp)->next;
        -: 4138:
    #####: 4139:    *bsfp= bsfc;
        -: 4140:
    #####: 4141:    return 0;
        -: 4142:}
        -: 4143:
        -: 4144:static int opt_preset(const char *opt, const char *arg)
    #####: 4145:{
    #####: 4146:    FILE *f=NULL;
        -: 4147:    char filename[1000], tmp[1000], tmp2[1000], line[1000];
        -: 4148:    char *codec_name = *opt == 'v' ? video_codec_name :
        -: 4149:                       *opt == 'a' ? audio_codec_name :
    #####: 4150:                                     subtitle_codec_name;
        -: 4151:
    #####: 4152:    if (!(f = get_preset_file(filename, sizeof(filename), arg, *opt == 'f', codec_name))) {
    #####: 4153:        fprintf(stderr, "File for preset '%s' not found\n", arg);
    #####: 4154:        ffmpeg_exit(1);
        -: 4155:    }
        -: 4156:
    #####: 4157:    while(!feof(f)){
    #####: 4158:        int e= fscanf(f, "%999[^\n]\n", line) - 1;
    #####: 4159:        if(line[0] == '#' && !e)
    #####: 4160:            continue;
    #####: 4161:        e|= sscanf(line, "%999[^=]=%999[^\n]\n", tmp, tmp2) - 2;
    #####: 4162:        if(e){
    #####: 4163:            fprintf(stderr, "%s: Invalid syntax: '%s'\n", filename, line);
    #####: 4164:            ffmpeg_exit(1);
        -: 4165:        }
    #####: 4166:        if(!strcmp(tmp, "acodec")){
    #####: 4167:            opt_audio_codec(tmp2);
    #####: 4168:        }else if(!strcmp(tmp, "vcodec")){
    #####: 4169:            opt_video_codec(tmp2);
    #####: 4170:        }else if(!strcmp(tmp, "scodec")){
    #####: 4171:            opt_subtitle_codec(tmp2);
    #####: 4172:        }else if(opt_default(tmp, tmp2) < 0){
    #####: 4173:            fprintf(stderr, "%s: Invalid option or argument: '%s', parsed as '%s' = '%s'\n", filename, line, tmp, tmp2);
    #####: 4174:            ffmpeg_exit(1);
        -: 4175:        }
        -: 4176:    }
        -: 4177:
    #####: 4178:    fclose(f);
        -: 4179:
    #####: 4180:    return 0;
        -: 4181:}
        -: 4182:
        -: 4183:static const OptionDef options[] = {
        -: 4184:    /* main options */
        -: 4185:#include "cmdutils_common_opts.h"
        -: 4186:    { "f", HAS_ARG, {(void*)opt_format}, "force format", "fmt" },
        -: 4187:    { "i", HAS_ARG, {(void*)opt_input_file}, "input file name", "filename" },
        -: 4188:    { "y", OPT_BOOL, {(void*)&file_overwrite}, "overwrite output files" },
        -: 4189:    { "map", HAS_ARG | OPT_EXPERT, {(void*)opt_map}, "set input stream mapping", "file:stream[:syncfile:syncstream]" },
        -: 4190:    { "map_meta_data", HAS_ARG | OPT_EXPERT, {(void*)opt_map_meta_data}, "set meta data information of outfile from infile", "outfile[,metadata]:infile[,metadata]" },
        -: 4191:    { "map_chapters",  HAS_ARG | OPT_EXPERT, {(void*)opt_map_chapters},  "set chapters mapping", "outfile:infile" },
        -: 4192:    { "t", OPT_FUNC2 | HAS_ARG, {(void*)opt_recording_time}, "record or transcode \"duration\" seconds of audio/video", "duration" },
        -: 4193:    { "fs", HAS_ARG | OPT_INT64, {(void*)&limit_filesize}, "set the limit file size in bytes", "limit_size" }, //
        -: 4194:    { "ss", OPT_FUNC2 | HAS_ARG, {(void*)opt_start_time}, "set the start time offset", "time_off" },
        -: 4195:    { "itsoffset", OPT_FUNC2 | HAS_ARG, {(void*)opt_input_ts_offset}, "set the input ts offset", "time_off" },
        -: 4196:    { "itsscale", HAS_ARG, {(void*)opt_input_ts_scale}, "set the input ts scale", "stream:scale" },
        -: 4197:    { "timestamp", OPT_FUNC2 | HAS_ARG, {(void*)opt_recording_timestamp}, "set the recording timestamp ('now' to set the current time)", "time" },
        -: 4198:    { "metadata", OPT_FUNC2 | HAS_ARG, {(void*)opt_metadata}, "add metadata", "string=string" },
        -: 4199:    { "dframes", OPT_INT | HAS_ARG, {(void*)&max_frames[AVMEDIA_TYPE_DATA]}, "set the number of data frames to record", "number" },
        -: 4200:    { "benchmark", OPT_BOOL | OPT_EXPERT, {(void*)&do_benchmark},
        -: 4201:      "add timings for benchmarking" },
        -: 4202:    { "timelimit", OPT_FUNC2 | HAS_ARG, {(void*)opt_timelimit}, "set max runtime in seconds", "limit" },
        -: 4203:    { "dump", OPT_BOOL | OPT_EXPERT, {(void*)&do_pkt_dump},
        -: 4204:      "dump each input packet" },
        -: 4205:    { "hex", OPT_BOOL | OPT_EXPERT, {(void*)&do_hex_dump},
        -: 4206:      "when dumping packets, also dump the payload" },
        -: 4207:    { "re", OPT_BOOL | OPT_EXPERT, {(void*)&rate_emu}, "read input at native frame rate", "" },
        -: 4208:    { "loop_input", OPT_BOOL | OPT_EXPERT, {(void*)&loop_input}, "loop (current only works with images)" },
        -: 4209:    { "loop_output", HAS_ARG | OPT_INT | OPT_EXPERT, {(void*)&loop_output}, "number of times to loop output in formats that support looping (0 loops forever)", "" },
        -: 4210:    { "v", HAS_ARG | OPT_FUNC2, {(void*)opt_verbose}, "set ffmpeg verbosity level", "number" },
        -: 4211:    { "target", HAS_ARG, {(void*)opt_target}, "specify target file type (\"vcd\", \"svcd\", \"dvd\", \"dv\", \"dv50\", \"pal-vcd\", \"ntsc-svcd\", ...)", "type" },
        -: 4212:    { "threads", OPT_FUNC2 | HAS_ARG | OPT_EXPERT, {(void*)opt_thread_count}, "thread count", "count" },
        -: 4213:    { "vsync", HAS_ARG | OPT_INT | OPT_EXPERT, {(void*)&video_sync_method}, "video sync method", "" },
        -: 4214:    { "async", HAS_ARG | OPT_INT | OPT_EXPERT, {(void*)&audio_sync_method}, "audio sync method", "" },
        -: 4215:    { "adrift_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT, {(void*)&audio_drift_threshold}, "audio drift threshold", "threshold" },
        -: 4216:    { "vglobal", HAS_ARG | OPT_INT | OPT_EXPERT, {(void*)&video_global_header}, "video global header storage type", "" },
        -: 4217:    { "copyts", OPT_BOOL | OPT_EXPERT, {(void*)&copy_ts}, "copy timestamps" },
        -: 4218:    { "copytb", OPT_BOOL | OPT_EXPERT, {(void*)&copy_tb}, "copy input stream time base when stream copying" },
        -: 4219:    { "shortest", OPT_BOOL | OPT_EXPERT, {(void*)&opt_shortest}, "finish encoding within shortest input" }, //
        -: 4220:    { "dts_delta_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT, {(void*)&dts_delta_threshold}, "timestamp discontinuity delta threshold", "threshold" },
        -: 4221:    { "programid", HAS_ARG | OPT_INT | OPT_EXPERT, {(void*)&opt_programid}, "desired program number", "" },
        -: 4222:    { "xerror", OPT_BOOL, {(void*)&exit_on_error}, "exit on error", "error" },
        -: 4223:    { "copyinkf", OPT_BOOL | OPT_EXPERT, {(void*)&copy_initial_nonkeyframes}, "copy initial non-keyframes" },
        -: 4224:
        -: 4225:    /* video options */
        -: 4226:    { "b", OPT_FUNC2 | HAS_ARG | OPT_VIDEO, {(void*)opt_bitrate}, "set bitrate (in bits/s)", "bitrate" },
        -: 4227:    { "vb", OPT_FUNC2 | HAS_ARG | OPT_VIDEO, {(void*)opt_bitrate}, "set bitrate (in bits/s)", "bitrate" },
        -: 4228:    { "vframes", OPT_INT | HAS_ARG | OPT_VIDEO, {(void*)&max_frames[AVMEDIA_TYPE_VIDEO]}, "set the number of video frames to record", "number" },
        -: 4229:    { "r", OPT_FUNC2 | HAS_ARG | OPT_VIDEO, {(void*)opt_frame_rate}, "set frame rate (Hz value, fraction or abbreviation)", "rate" },
        -: 4230:    { "s", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_size}, "set frame size (WxH or abbreviation)", "size" },
        -: 4231:    { "aspect", HAS_ARG | OPT_VIDEO, {(void*)opt_frame_aspect_ratio}, "set aspect ratio (4:3, 16:9 or 1.3333, 1.7777)", "aspect" },
        -: 4232:    { "pix_fmt", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_frame_pix_fmt}, "set pixel format, 'list' as argument shows all the pixel formats supported", "format" },
        -: 4233:    { "croptop", OPT_FUNC2 | HAS_ARG | OPT_VIDEO, {(void*)opt_frame_crop}, "Removed, use the crop filter instead", "size" },
        -: 4234:    { "cropbottom", OPT_FUNC2 | HAS_ARG | OPT_VIDEO, {(void*)opt_frame_crop}, "Removed, use the crop filter instead", "size" },
        -: 4235:    { "cropleft", OPT_FUNC2 | HAS_ARG | OPT_VIDEO, {(void*)opt_frame_crop}, "Removed, use the crop filter instead", "size" },
        -: 4236:    { "cropright", OPT_FUNC2 | HAS_ARG | OPT_VIDEO, {(void*)opt_frame_crop}, "Removed, use the crop filter instead", "size" },
        -: 4237:    { "padtop", OPT_FUNC2 | HAS_ARG | OPT_VIDEO, {(void*)opt_pad}, "Removed, use the pad filter instead", "size" },
        -: 4238:    { "padbottom", OPT_FUNC2 | HAS_ARG | OPT_VIDEO, {(void*)opt_pad}, "Removed, use the pad filter instead", "size" },
        -: 4239:    { "padleft", OPT_FUNC2 | HAS_ARG | OPT_VIDEO, {(void*)opt_pad}, "Removed, use the pad filter instead", "size" },
        -: 4240:    { "padright", OPT_FUNC2 | HAS_ARG | OPT_VIDEO, {(void*)opt_pad}, "Removed, use the pad filter instead", "size" },
        -: 4241:    { "padcolor", OPT_FUNC2 | HAS_ARG | OPT_VIDEO, {(void*)opt_pad}, "Removed, use the pad filter instead", "color" },
        -: 4242:    { "intra", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&intra_only}, "use only intra frames"},
        -: 4243:    { "vn", OPT_BOOL | OPT_VIDEO, {(void*)&video_disable}, "disable video" },
        -: 4244:    { "vdt", OPT_INT | HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)&video_discard}, "discard threshold", "n" },
        -: 4245:    { "qscale", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_qscale}, "use fixed video quantizer scale (VBR)", "q" },
        -: 4246:    { "rc_override", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_video_rc_override_string}, "rate control override for specific intervals", "override" },
        -: 4247:    { "vcodec", HAS_ARG | OPT_VIDEO, {(void*)opt_video_codec}, "force video codec ('copy' to copy stream)", "codec" },
        -: 4248:    { "me_threshold", HAS_ARG | OPT_FUNC2 | OPT_EXPERT | OPT_VIDEO, {(void*)opt_me_threshold}, "motion estimaton threshold",  "threshold" },
        -: 4249:    { "sameq", OPT_BOOL | OPT_VIDEO, {(void*)&same_quality},
        -: 4250:      "use same video quality as source (implies VBR)" },
        -: 4251:    { "pass", HAS_ARG | OPT_VIDEO, {(void*)&opt_pass}, "select the pass number (1 or 2)", "n" },
        -: 4252:    { "passlogfile", HAS_ARG | OPT_STRING | OPT_VIDEO, {(void*)&pass_logfilename_prefix}, "select two pass log file name prefix", "prefix" },
        -: 4253:    { "deinterlace", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&do_deinterlace},
        -: 4254:      "deinterlace pictures" },
        -: 4255:    { "psnr", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&do_psnr}, "calculate PSNR of compressed frames" },
        -: 4256:    { "vstats", OPT_EXPERT | OPT_VIDEO, {(void*)&opt_vstats}, "dump video coding statistics to file" },
        -: 4257:    { "vstats_file", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_vstats_file}, "dump video coding statistics to file", "file" },
        -: 4258:#if CONFIG_AVFILTER
        -: 4259:    { "vf", OPT_STRING | HAS_ARG, {(void*)&vfilters}, "video filters", "filter list" },
        -: 4260:#endif
        -: 4261:    { "intra_matrix", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_intra_matrix}, "specify intra matrix coeffs", "matrix" },
        -: 4262:    { "inter_matrix", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_inter_matrix}, "specify inter matrix coeffs", "matrix" },
        -: 4263:    { "top", HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_top_field_first}, "top=1/bottom=0/auto=-1 field first", "" },
        -: 4264:    { "dc", OPT_INT | HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)&intra_dc_precision}, "intra_dc_precision", "precision" },
        -: 4265:    { "vtag", OPT_FUNC2 | HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void*)opt_codec_tag}, "force video tag/fourcc", "fourcc/tag" },
        -: 4266:    { "newvideo", OPT_VIDEO | OPT_FUNC2, {(void*)opt_new_stream}, "add a new video stream to the current output stream" },
        -: 4267:    { "vlang", HAS_ARG | OPT_STRING | OPT_VIDEO, {(void *)&video_language}, "set the ISO 639 language code (3 letters) of the current video stream" , "code" },
        -: 4268:    { "qphist", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, { (void *)&qp_hist }, "show QP histogram" },
        -: 4269:    { "force_fps", OPT_BOOL | OPT_EXPERT | OPT_VIDEO, {(void*)&force_fps}, "force the selected framerate, disable the best supported framerate selection" },
        -: 4270:    { "streamid", OPT_FUNC2 | HAS_ARG | OPT_EXPERT, {(void*)opt_streamid}, "set the value of an outfile streamid", "streamIndex:value" },
        -: 4271:    { "force_key_frames", OPT_STRING | HAS_ARG | OPT_EXPERT | OPT_VIDEO, {(void *)&forced_key_frames}, "force key frames at specified timestamps", "timestamps" },
        -: 4272:
        -: 4273:    /* audio options */
        -: 4274:    { "ab", OPT_FUNC2 | HAS_ARG | OPT_AUDIO, {(void*)opt_bitrate}, "set bitrate (in bits/s)", "bitrate" },
        -: 4275:    { "aframes", OPT_INT | HAS_ARG | OPT_AUDIO, {(void*)&max_frames[AVMEDIA_TYPE_AUDIO]}, "set the number of audio frames to record", "number" },
        -: 4276:    { "aq", OPT_FLOAT | HAS_ARG | OPT_AUDIO, {(void*)&audio_qscale}, "set audio quality (codec-specific)", "quality", },
        -: 4277:    { "ar", HAS_ARG | OPT_FUNC2 | OPT_AUDIO, {(void*)opt_audio_rate}, "set audio sampling rate (in Hz)", "rate" },
        -: 4278:    { "ac", HAS_ARG | OPT_FUNC2 | OPT_AUDIO, {(void*)opt_audio_channels}, "set number of audio channels", "channels" },
        -: 4279:    { "an", OPT_BOOL | OPT_AUDIO, {(void*)&audio_disable}, "disable audio" },
        -: 4280:    { "acodec", HAS_ARG | OPT_AUDIO, {(void*)opt_audio_codec}, "force audio codec ('copy' to copy stream)", "codec" },
        -: 4281:    { "atag", OPT_FUNC2 | HAS_ARG | OPT_EXPERT | OPT_AUDIO, {(void*)opt_codec_tag}, "force audio tag/fourcc", "fourcc/tag" },
        -: 4282:    { "vol", OPT_INT | HAS_ARG | OPT_AUDIO, {(void*)&audio_volume}, "change audio volume (256=normal)" , "volume" }, //
        -: 4283:    { "newaudio", OPT_AUDIO | OPT_FUNC2, {(void*)opt_new_stream}, "add a new audio stream to the current output stream" },
        -: 4284:    { "alang", HAS_ARG | OPT_STRING | OPT_AUDIO, {(void *)&audio_language}, "set the ISO 639 language code (3 letters) of the current audio stream" , "code" },
        -: 4285:    { "sample_fmt", HAS_ARG | OPT_EXPERT | OPT_AUDIO, {(void*)opt_audio_sample_fmt}, "set sample format, 'list' as argument shows all the sample formats supported", "format" },
        -: 4286:
        -: 4287:    /* subtitle options */
        -: 4288:    { "sn", OPT_BOOL | OPT_SUBTITLE, {(void*)&subtitle_disable}, "disable subtitle" },
        -: 4289:    { "scodec", HAS_ARG | OPT_SUBTITLE, {(void*)opt_subtitle_codec}, "force subtitle codec ('copy' to copy stream)", "codec" },
        -: 4290:    { "newsubtitle", OPT_SUBTITLE | OPT_FUNC2, {(void*)opt_new_stream}, "add a new subtitle stream to the current output stream" },
        -: 4291:    { "slang", HAS_ARG | OPT_STRING | OPT_SUBTITLE, {(void *)&subtitle_language}, "set the ISO 639 language code (3 letters) of the current subtitle stream" , "code" },
        -: 4292:    { "stag", OPT_FUNC2 | HAS_ARG | OPT_EXPERT | OPT_SUBTITLE, {(void*)opt_codec_tag}, "force subtitle tag/fourcc", "fourcc/tag" },
        -: 4293:
        -: 4294:    /* grab options */
        -: 4295:    { "vc", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_GRAB, {(void*)opt_video_channel}, "set video grab channel (DV1394 only)", "channel" },
        -: 4296:    { "tvstd", HAS_ARG | OPT_EXPERT | OPT_VIDEO | OPT_GRAB, {(void*)opt_video_standard}, "set television standard (NTSC, PAL (SECAM))", "standard" },
        -: 4297:    { "isync", OPT_BOOL | OPT_EXPERT | OPT_GRAB, {(void*)&input_sync}, "sync read on input", "" },
        -: 4298:
        -: 4299:    /* muxer options */
        -: 4300:    { "muxdelay", OPT_FLOAT | HAS_ARG | OPT_EXPERT, {(void*)&mux_max_delay}, "set the maximum demux-decode delay", "seconds" },
        -: 4301:    { "muxpreload", OPT_FLOAT | HAS_ARG | OPT_EXPERT, {(void*)&mux_preload}, "set the initial demux-decode delay", "seconds" },
        -: 4302:
        -: 4303:    { "absf", OPT_FUNC2 | HAS_ARG | OPT_AUDIO | OPT_EXPERT, {(void*)opt_bsf}, "", "bitstream_filter" },
        -: 4304:    { "vbsf", OPT_FUNC2 | HAS_ARG | OPT_VIDEO | OPT_EXPERT, {(void*)opt_bsf}, "", "bitstream_filter" },
        -: 4305:    { "sbsf", OPT_FUNC2 | HAS_ARG | OPT_SUBTITLE | OPT_EXPERT, {(void*)opt_bsf}, "", "bitstream_filter" },
        -: 4306:
        -: 4307:    { "apre", OPT_FUNC2 | HAS_ARG | OPT_AUDIO | OPT_EXPERT, {(void*)opt_preset}, "set the audio options to the indicated preset", "preset" },
        -: 4308:    { "vpre", OPT_FUNC2 | HAS_ARG | OPT_VIDEO | OPT_EXPERT, {(void*)opt_preset}, "set the video options to the indicated preset", "preset" },
        -: 4309:    { "spre", OPT_FUNC2 | HAS_ARG | OPT_SUBTITLE | OPT_EXPERT, {(void*)opt_preset}, "set the subtitle options to the indicated preset", "preset" },
        -: 4310:    { "fpre", OPT_FUNC2 | HAS_ARG | OPT_EXPERT, {(void*)opt_preset}, "set options from indicated preset file", "filename" },
        -: 4311:
        -: 4312:    { "default", OPT_FUNC2 | HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, {(void*)opt_default}, "generic catch all option", "" },
        -: 4313:    { NULL, },
        -: 4314:};
        -: 4315:
        -: 4316:int main(int argc, char **argv)
        1: 4317:{
        -: 4318:    int64_t ti;
        -: 4319:
        1: 4320:    av_log_set_flags(AV_LOG_SKIP_REPEATED);
        -: 4321:
        1: 4322:    avcodec_register_all();
        -: 4323:#if CONFIG_AVDEVICE
        1: 4324:    avdevice_register_all();
        -: 4325:#endif
        -: 4326:#if CONFIG_AVFILTER
        1: 4327:    avfilter_register_all();
        -: 4328:#endif
        1: 4329:    av_register_all();
        -: 4330:
        -: 4331:#if HAVE_ISATTY
        1: 4332:    if(isatty(STDIN_FILENO))
        1: 4333:        url_set_interrupt_cb(decode_interrupt_cb);
        -: 4334:#endif
        -: 4335:
        1: 4336:    init_opts();
        -: 4337:
        1: 4338:    show_banner();
        -: 4339:
        -: 4340:    /* parse options */
        1: 4341:    parse_options(argc, argv, options, opt_output_file);
        -: 4342:
        1: 4343:    if(nb_output_files <= 0 && nb_input_files == 0) {
    #####: 4344:        show_usage();
    #####: 4345:        fprintf(stderr, "Use -h to get full help or, even better, run 'man ffmpeg'\n");
    #####: 4346:        ffmpeg_exit(1);
        -: 4347:    }
        -: 4348:
        -: 4349:    /* file converter / grab */
        1: 4350:    if (nb_output_files <= 0) {
    #####: 4351:        fprintf(stderr, "At least one output file must be specified\n");
    #####: 4352:        ffmpeg_exit(1);
        -: 4353:    }
        -: 4354:
        1: 4355:    if (nb_input_files == 0) {
    #####: 4356:        fprintf(stderr, "At least one input file must be specified\n");
    #####: 4357:        ffmpeg_exit(1);
        -: 4358:    }
        -: 4359:
        1: 4360:    ti = getutime();
        1: 4361:    if (transcode(output_files, nb_output_files, input_files, nb_input_files,
        -: 4362:                  stream_maps, nb_stream_maps) < 0)
    #####: 4363:        ffmpeg_exit(1);
        1: 4364:    ti = getutime() - ti;
        1: 4365:    if (do_benchmark) {
    #####: 4366:        int maxrss = getmaxrss() / 1024;
    #####: 4367:        printf("bench: utime=%0.3fs maxrss=%ikB\n", ti / 1000000.0, maxrss);
        -: 4368:    }
        -: 4369:
        1: 4370:    return ffmpeg_exit(0);
        -: 4371:}
