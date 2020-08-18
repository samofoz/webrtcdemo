
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
#include <glib.h>

#include <assert.h>

#include "media_file_writer.h"

#define AV_CODEC_FLAG_GLOBAL_HEADER (1 << 22)
#define CODEC_FLAG_GLOBAL_HEADER AV_CODEC_FLAG_GLOBAL_HEADER

#define MAX_RESAMPLERS 5
#define MAX_SWSCALERS 10


#define USE_AUDIO_FILTER 0
#define USE_VIDEO_FILTER 0
#define HASVIDEO 0
#define USE_SWR 1
#define USE_SWS 1

const int src_audio_format = AV_SAMPLE_FMT_S16;
const int out_pix_format = AV_PIX_FMT_YUV420P;
const int out_audio_format = AV_SAMPLE_FMT_FLTP;
const int out_audio_num_channels = 1;
const int64_t out_sample_rate = 48000;

const char *video_filter_descr = "scale=1280:720";
const char *audio_filter_descr = "aresample=48000,aformat=sample_fmts=s16:channel_layouts=mono";

const AVRational global_time_base = { 1, 1000 };
const int64_t out_video_fps = 30;


enum file_writer_event_t {
    FILE_WRITER_EVENT_UNKNOWN,
    FILE_WRITER_EVENT_CREATED,
    FILE_WRITER_EVENT_DESTROYED,
    FILE_WRITER_EVENT_FREED,
    FILE_WRITER_EVENT_AUDIO_FRAME,
    FILE_WRITER_EVENT_VIDEO_FRAME
};


struct file_writer_t {
    GThread* pgthread_rec;
    int exit;
    GAsyncQueue* main_loop_queue;
    GHashTable* main_context_hash_table;
};

struct file_writer_event_queue_item_t {
    enum file_writer_event_t event;
    char* context;
    void* in;
};

struct resampler_t {
    int channels;
    enum AVSampleFormat sample_fmt;
    int sample_rate;
    SwrContext* resample_context;
};

struct swscaler_t {
    int width;
    int height;
    enum AVPixelFormat fmt;
    struct SwsContext* sws_context;
};

struct file_writer_instance_t {
    struct file_writer_t* file_writer;
    char* crn;

    int out_width;
    int out_height;

    FILE* filep;

    /* stream filtering */
    AVFilterContext* audio_buffersink_ctx;
    AVFilterContext* audio_buffersrc_ctx;
    AVFilterContext* video_buffersink_ctx;
    AVFilterContext* video_buffersrc_ctx;
    AVFilterGraph* video_filter_graph;
    AVFilterGraph* audio_filter_graph;

    /* container codec configuration */
    AVCodec* video_codec_out;
    AVCodec* audio_codec_out;
    AVCodecContext* video_ctx_out;
    AVCodecContext* audio_ctx_out;
    AVFormatContext* format_ctx_out;
    AVStream* video_stream;
    AVStream* audio_stream;
    AVAudioFifo* fifo;
    int64_t video_frame_ct;
    int64_t audio_frame_ct;

    struct resampler_t resamplers[MAX_RESAMPLERS];
    struct swscaler_t swscalers[MAX_SWSCALERS];

    gint64 first_video_frame_time;
    int64_t next_audio_pts;

    GThread* pgthread_rec;
    int exit;
    GAsyncQueue* main_loop_queue;
};

static int file_writer_open(struct file_writer_instance_t* writer_instance, const char* filename, int out_width, int out_height);
static int file_writer_close(struct file_writer_instance_t* writer_instance);
static int init_audio_filters(struct file_writer_instance_t* file_writer, const char *filters_descr);
static int init_video_filters(struct file_writer_instance_t* file_writer, const char *filters_descr, int out_width, int out_height);
static int init_resampler(struct resampler_t* resampler);
static int init_swscaler(struct file_writer_instance_t* file_writer, struct swscaler_t* swscaler, int in_w, int in_h);
static int write_audio_frame(struct file_writer_instance_t* file_writer, AVFrame* frame, int* data_present);
static int write_video_frame(struct file_writer_instance_t* file_writer, AVFrame* frame, int* data_present);
static gpointer cgs_file_writer_recording_thread(gpointer context);
static gpointer cgs_file_writer_individual_recording_thread(gpointer context);
static void main_loop_queue_item_free(gpointer data);
static char* get_next_crn();
int process_audio_frame(struct file_writer_instance_t* file_writer, AVFrame* frame);
int process_video_frame(struct file_writer_instance_t* file_writer, AVFrame* frame);


int file_writer_alloc(struct file_writer_t** writer) {

    enum file_writer_error ret;

    *writer = (struct file_writer_t*)calloc(1, sizeof(struct file_writer_t));
    if (*writer == NULL) {
        ret = CGS_FILE_WRITER_ERROR_NOMEM;
        goto GET_OUT;
    }

    /* Start the global thread if not already */
    (*writer)->main_loop_queue = g_async_queue_new_full(main_loop_queue_item_free);
    if (!(*writer)->main_loop_queue)
    {
        ret = CGS_FILE_WRITER_ERROR_NOMEM;
        goto GET_OUT;
    }

    (*writer)->main_context_hash_table = g_hash_table_new(NULL, g_str_equal);
    if (!(*writer)->main_context_hash_table)
    {
        ret = CGS_FILE_WRITER_ERROR_NOMEM;
        goto GET_OUT;
    }

    GError* error;
    (*writer)->pgthread_rec = g_thread_try_new("cgs_file_writer_recording_thread", cgs_file_writer_recording_thread, *writer, &error);
    if ((*writer)->pgthread_rec == NULL) {
        ret = CGS_FILE_WRITER_ERROR_THREAD_CREATE;
        goto GET_OUT;
    }

    return CGS_FILE_WRITER_ERROR_SUCCESS;

GET_OUT:
    if (*writer) {
        if ((*writer)->main_loop_queue)
            g_async_queue_unref((*writer)->main_loop_queue);
        if ((*writer)->main_context_hash_table)
            g_hash_table_destroy((*writer)->main_context_hash_table);
        free(*writer);
    }
    return (int)ret;
}


int file_writer_create_context(struct file_writer_t* writer, struct file_writer_instance_t** writer_instance) {

    enum file_writer_error ret;

    /* Allocate the context */
    *writer_instance = (struct file_writer_instance_t*) calloc(1, sizeof(struct file_writer_instance_t));
    if (*writer_instance == NULL) {
        ret = CGS_FILE_WRITER_ERROR_NOMEM;
        goto GET_OUT;
    }

    (*writer_instance)->file_writer = writer;

    /* Start the global thread if not already */
    (*writer_instance)->main_loop_queue = g_async_queue_new_full(main_loop_queue_item_free);
    if (!(*writer_instance)->main_loop_queue)
    {
        ret = CGS_FILE_WRITER_ERROR_NOMEM;
        goto GET_OUT;
    }

    GError* error;
    (*writer_instance)->pgthread_rec = g_thread_try_new("cgs_file_writer_individual_recording_thread", cgs_file_writer_individual_recording_thread, *writer_instance, &error);
    if ((*writer_instance)->pgthread_rec == NULL) {
        ret = CGS_FILE_WRITER_ERROR_THREAD_CREATE;
        goto GET_OUT;
    }

    /* Allocate a unique crn for this context */
    do {
        if ((*writer_instance)->crn) free((*writer_instance)->crn);
        (*writer_instance)->crn = get_next_crn();
        if (!(*writer_instance)->crn) {
            ret = CGS_FILE_WRITER_ERROR_NOMEM;
            goto GET_OUT;
        }
    } while (g_hash_table_contains(writer->main_context_hash_table, (*writer_instance)->crn));

    if (!g_hash_table_insert(writer->main_context_hash_table, (*writer_instance)->crn, *writer_instance)) {
        ret = CGS_FILE_WRITER_ERROR_NOMEM;
        goto GET_OUT;
    }

    char szFile[1024];
    sprintf(szFile, "c:\\temp\\%p.mp4", *writer_instance);
    ret = file_writer_open(*writer_instance, szFile, 1280, 720);
    if (ret) {
        g_hash_table_remove(writer->main_context_hash_table, (*writer_instance)->crn);
        goto GET_OUT;
    }

    /* Post an event to the global thread */
    struct file_writer_event_queue_item_t *event = (struct file_writer_event_queue_item_t*) calloc(1, sizeof(struct file_writer_event_queue_item_t));
    if (!event) {
        g_hash_table_remove(writer->main_context_hash_table, (*writer_instance)->crn);
        ret = CGS_FILE_WRITER_ERROR_NOMEM;
        goto GET_OUT;
    }
    event->event = FILE_WRITER_EVENT_CREATED;
    event->context = (*writer_instance)->crn;
    g_async_queue_push((*writer_instance)->main_loop_queue, event);

    return CGS_FILE_WRITER_ERROR_SUCCESS;

GET_OUT:
    if (*writer_instance) {
        if ((*writer_instance)->main_loop_queue)
            g_async_queue_unref((*writer_instance)->main_loop_queue);
        if((*writer_instance)->crn)
            free((*writer_instance)->crn);
        free(*writer_instance);
    }
    return (int)ret;
}


int file_writer_open(struct file_writer_instance_t* file_writer, const char* filename, int out_width, int out_height)
{
    int error;
    AVDictionary* opt = NULL;

    file_writer->out_height = out_height;
    file_writer->out_width = out_width;

    /* Create a new format context for the output container format. */
    if (!(file_writer->format_ctx_out = avformat_alloc_context())) {
        fprintf(stderr, "Could not allocate output format context\n");
        return AVERROR(ENOMEM);
    }

    /* Open the output file to write to it. */
    if ((error = avio_open(&(file_writer->format_ctx_out->pb), filename,
        AVIO_FLAG_WRITE)) < 0) {
        fprintf(stderr, "Could not open output file '%s' (error '%s')\n",
            filename, av_err2str(error));
        return error;
    }

    /* Guess the desired container format based on the file extension. */
    if (!(file_writer->format_ctx_out->oformat = av_guess_format(NULL, filename, NULL))) {
        fprintf(stderr, "Could not find output file format\n");
        goto cleanup;
    }

    av_dump_format(file_writer->format_ctx_out, 0, filename, 1);

    if (!(file_writer->format_ctx_out->url = av_strdup(filename))) {
        fprintf(stderr, "Could not allocate url.\n");
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    file_writer->video_codec_out = avcodec_find_encoder(file_writer->format_ctx_out->oformat->video_codec); //avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!file_writer->video_codec_out) {
        fprintf(stderr, "Could not find an Video encoder.\n");
        goto cleanup;
    }

    file_writer->audio_codec_out = avcodec_find_encoder(file_writer->format_ctx_out->oformat->audio_codec); //avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!file_writer->audio_codec_out) {
        fprintf(stderr, "Could not find an Audio encoder.\n");
        goto cleanup;
    }

    /* Create a new audio stream in the output file container. */
    if (!(file_writer->audio_stream = avformat_new_stream(file_writer->format_ctx_out, NULL))) {
        fprintf(stderr, "Could not create new audio stream\n");
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    /* Create a new audio stream in the output file container. */
    if (!(file_writer->video_stream = avformat_new_stream(file_writer->format_ctx_out, NULL))) {
        fprintf(stderr, "Could not create new video stream\n");
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    file_writer->audio_ctx_out = avcodec_alloc_context3(file_writer->audio_codec_out);
    if (!file_writer->audio_ctx_out) {
        fprintf(stderr, "Could not allocate an encoding context\n");
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    file_writer->video_ctx_out = avcodec_alloc_context3(file_writer->video_codec_out);
    if (!file_writer->video_ctx_out) {
        fprintf(stderr, "Could not allocate an encoding context\n");
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    /* Stream params */
    file_writer->audio_stream->time_base.num = 1;
    file_writer->audio_stream->time_base.den = out_sample_rate;

    /* Codec configuration */
    file_writer->audio_ctx_out->bit_rate = 96000;
    file_writer->audio_ctx_out->sample_fmt = out_audio_format;
    file_writer->audio_ctx_out->sample_rate = out_sample_rate;
    file_writer->audio_ctx_out->channels = 1;
    file_writer->audio_ctx_out->channel_layout = AV_CH_LAYOUT_MONO;
    file_writer->audio_ctx_out->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    file_writer->audio_ctx_out->time_base.num = 1;
    file_writer->audio_ctx_out->time_base.den = out_sample_rate;

    /* put sample parameters */
    file_writer->video_ctx_out->qmin = 20;
    /* resolution must be a multiple of two */
    file_writer->video_ctx_out->width = file_writer->out_width;
    file_writer->video_ctx_out->height = file_writer->out_height;
    file_writer->video_ctx_out->pix_fmt = out_pix_format;
    file_writer->video_ctx_out->time_base = global_time_base;
    //video_ctx_out->max_b_frames = 1;

    if (file_writer->format_ctx_out->oformat->video_codec == AV_CODEC_ID_H264) {
        av_opt_set(file_writer->video_ctx_out->priv_data, "preset", "fast", 0);
    }

    /* Some formats want stream headers to be separate. */
    if (file_writer->format_ctx_out->oformat->flags & AVFMT_GLOBALHEADER)
        file_writer->audio_ctx_out->flags |= CODEC_FLAG_GLOBAL_HEADER;

    /* Open the encoder for the audio stream to use it later. */
    if ((error = avcodec_open2(file_writer->audio_ctx_out, file_writer->audio_codec_out, NULL)) < 0) {
        fprintf(stderr, "Could not open output codec (error '%s')\n",
            av_err2str(error));
        goto cleanup;
    }

    /* Open the encoder for the video stream to use it later. */
    if ((error = avcodec_open2(file_writer->video_ctx_out, file_writer->video_codec_out, NULL)) < 0) {
        fprintf(stderr, "Could not open output codec (error '%s')\n",
            av_err2str(error));
        goto cleanup;
    }

    error = avcodec_parameters_from_context(file_writer->audio_stream->codecpar, file_writer->audio_ctx_out);
    if (error < 0) {
        fprintf(stderr, "Could not initialize stream parameters\n");
        goto cleanup;
    }

    error = avcodec_parameters_from_context(file_writer->video_stream->codecpar, file_writer->video_ctx_out);
    if (error < 0) {
        fprintf(stderr, "Could not initialize stream parameters\n");
        goto cleanup;
    }

    /* Write the stream header, if any. */
    error = avformat_write_header(file_writer->format_ctx_out, &opt);
    if (error < 0) {
        fprintf(stderr, "Error occurred when writing header to file: %s\n",
            av_err2str(error));
        goto cleanup;
    }

    /* Create the FIFO buffer based on the specified output sample format. */
    if (!(file_writer->fifo = av_audio_fifo_alloc(file_writer->audio_ctx_out->sample_fmt, file_writer->audio_ctx_out->channels, 1))) {
        fprintf(stderr, "Could not allocate FIFO\n");
        error = AVERROR(ENOMEM);
        goto cleanup;
    }


#if USE_AUDIO_FILTER
    error = init_audio_filters(file_writer, audio_filter_descr);
    if (error < 0) {
        printf("Error: init audio filters\n");
        goto cleanup;
    }
#endif

#if USE_VIDEO_FILTER
    error = init_video_filters(file_writer, video_filter_descr, out_width, out_height);
    if (error < 0) {
        printf("Error: init video filters\n");
        goto cleanup;
    }
#endif

    printf("Ready to encode %s\n", filename);
    return 0;

cleanup:
    if (file_writer->audio_filter_graph)
        avfilter_graph_free(&file_writer->audio_filter_graph);
    if (file_writer->video_filter_graph)
        avfilter_graph_free(&file_writer->video_filter_graph);
    avcodec_free_context(&file_writer->audio_ctx_out);
    avcodec_free_context(&file_writer->video_ctx_out);
    avio_closep(&file_writer->format_ctx_out->pb);
    avformat_free_context(file_writer->format_ctx_out);
    file_writer->format_ctx_out = NULL;
    return error;
}




static int init_swscaler(struct file_writer_instance_t *file_writer, struct swscaler_t* swscaler, int in_w, int in_h)
{
    struct SwsContext** sws_context = &swscaler->sws_context;

    *sws_context = sws_getContext(in_w, in_h, AV_PIX_FMT_YUV420P, file_writer->out_width, file_writer->out_height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
    if (!*sws_context) {
        fprintf(stderr, "Could not allocate sws context, sws_getContext() failed\n");
        return AVERROR(ENOMEM);
    }

    swscaler->fmt = AV_PIX_FMT_YUV420P;
    swscaler->width = in_w;
    swscaler->height = in_h;

    return 0;
}


/**
 * Initialize the audio resampler based on the input and output codec settings.
 * If the input and output sample formats differ, a conversion is required
 * libswresample takes care of this, but requires initialization.
 */
static int init_resampler(struct resampler_t *resampler)
{
    int error;
    SwrContext** resample_context = &resampler->resample_context;
    /**
     * Create a resampler context for the conversion.
     * Set the conversion parameters.
     * Default channel layouts based on the number of channels
     * are assumed for simplicity (they are sometimes not detected
     * properly by the demuxer and/or decoder).
     */
    *resample_context =
    swr_alloc_set_opts(NULL,
                        av_get_default_channel_layout(1),
                        AV_SAMPLE_FMT_FLTP,
                        48000,
                        av_get_default_channel_layout(resampler->channels),
                        resampler->sample_fmt,
                        resampler->sample_rate, 0, NULL);
    if (!*resample_context) {
        fprintf(stderr, "Could not allocate resample context\n");
        return AVERROR(ENOMEM);
    }

    /** Open the resampler with the specified parameters. */
    if ((error = swr_init(*resample_context)) < 0) {
        fprintf(stderr, "Could not open resample context\n");
        swr_free(resample_context);
        *resample_context = NULL;
        return error;
    }
    return 0;
}


static int safe_write_packet(struct file_writer_instance_t* file_writer, AVPacket* packet)
{
    return av_interleaved_write_frame(file_writer->format_ctx_out, packet);
}

static int write_audio_frame(struct file_writer_instance_t* file_writer, AVFrame* frame, int* data_present)
{
    int ret;
    AVPacket pkt;
    av_init_packet(&pkt);
    /* Set the packet data and size so that it is recognized as being empty. */
    pkt.data = NULL;
    pkt.size = 0;

    if (frame) {
        frame->pts = file_writer->next_audio_pts;
        file_writer->next_audio_pts = frame->pts + frame->nb_samples;
    }

    /* encode the frame */
    ret = avcodec_send_frame(file_writer->audio_ctx_out, frame);
    if (ret == AVERROR_EOF) {
        ;
    }
    else if(ret < 0) {
        fprintf(stderr, "Error encoding audio frame, avcodec_send_frame() failed: %s\n", av_err2str(ret));
        av_packet_unref(&pkt);
        return ret;
    }

    ret = avcodec_receive_packet(file_writer->audio_ctx_out, &pkt);
    if ((AVERROR(EAGAIN) == ret) || (ret == AVERROR_EOF)) {
        av_packet_unref(&pkt);
        return 0;
    }
    else if (ret < 0) {
        fprintf(stderr, "Error encoding audio frame, avcodec_receive_packet() failed: %s\n", av_err2str(ret));
        av_packet_unref(&pkt);
        return ret;
    }
 
    if(data_present)
        *data_present = 1;
    pkt.stream_index = file_writer->audio_stream->index;

    /* Write the compressed frame to the media file. */
    printf("Write audio frame %lld, size=%d pts=%lld duration=%lld\n",
        file_writer->audio_frame_ct, pkt.size, pkt.pts, pkt.duration);
    file_writer->audio_frame_ct++;

    ret = safe_write_packet(file_writer, &pkt);
    av_packet_unref(&pkt);
    return ret;
}

int file_writer_push_audio_data(struct file_writer_instance_t* file_writer,
    const void* audio_data,
    int bits_per_sample,
    int sample_rate,
    size_t number_of_channels,
    size_t number_of_frames)
{
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        av_log(NULL, AV_LOG_ERROR, "av_frame_alloc() failed\n");
        return 1;
    }

    switch (bits_per_sample) {
    case 8:
        frame->format = AV_SAMPLE_FMT_U8;
        break;
    case 16:
        frame->format = AV_SAMPLE_FMT_S16;
        break;
    case 32:
        frame->format = AV_SAMPLE_FMT_S32;
        break;
    default:
        frame->format = AV_SAMPLE_FMT_S16;
        break;
    }
    frame->nb_samples = number_of_channels * number_of_frames;
    frame->sample_rate = sample_rate;
    frame->channels = number_of_channels;
    frame->channel_layout = av_get_default_channel_layout(frame->channels);

    int ret = av_frame_get_buffer(frame, 1);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "av_frame_get_buffer() failed: %s\n", av_err2str(ret));
        return 2;
    }

    memcpy(frame->data[0], audio_data, (frame->nb_samples * bits_per_sample) / 8);

    /* Send this frame to the global thread for further processing and writing to the file */
    struct file_writer_event_queue_item_t* event = (struct file_writer_event_queue_item_t*) calloc(1, sizeof(struct file_writer_event_queue_item_t));
    if(!event) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate FILE_WRITER_EVENT_AUDIO_FRAME event to be posted to the global thread\n");
        return 3;
    }

    event->event = FILE_WRITER_EVENT_AUDIO_FRAME;
    event->context = file_writer->crn;
    event->in = (void*)frame;
    g_async_queue_push(file_writer->main_loop_queue, event);

    return 0;
}



int file_writer_push_video_frame(struct file_writer_instance_t* file_writer, int64_t ts_us, int w, int h, uint8_t* y, uint8_t* u, uint8_t* v, uint32_t pitchY, uint32_t pitchU, uint32_t pitchV)
{
    if (!file_writer->first_video_frame_time)
        file_writer->first_video_frame_time = ts_us;

    AVFrame* avframe = av_frame_alloc();
    avframe->width = w;
    avframe->height = h;
    avframe->format = AV_PIX_FMT_YUV420P;

#if 0
    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, avframe->width, avframe->height, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(num_bytes * sizeof(uint8_t));

    int ret = av_image_fill_arrays(avframe->data, avframe->linesize, buffer, AV_PIX_FMT_YUV420P, avframe->width, avframe->height, 1);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate raw picture buffer\n");
        av_frame_free(&avframe);
        return 1;
    }
#else
    avframe->linesize[0] = pitchY;
    avframe->linesize[1] = pitchU;
    avframe->linesize[2] = pitchV;

    avframe->data[0] = (uint8_t*)av_malloc((avframe->linesize[0] * avframe->height) + (avframe->linesize[1] * avframe->height / 2) + (avframe->linesize[2] * avframe->height / 2));
    avframe->data[1] = avframe->data[0] + (avframe->linesize[0] * avframe->height);
    avframe->data[2] = avframe->data[1] + (avframe->linesize[1] * avframe->height / 2);
#endif
    memcpy(avframe->data[0], (uint8_t*)y, avframe->linesize[0] * avframe->height);
    memcpy(avframe->data[1], (uint8_t*)u, avframe->linesize[1] * avframe->height / 2);
    memcpy(avframe->data[2], (uint8_t*)v, avframe->linesize[2] * avframe->height / 2);

    avframe->pts = (ts_us - file_writer->first_video_frame_time) / 1000;

    /* Send this frame to the global thread for further processing and writing to the file */
    struct file_writer_event_queue_item_t* event = (struct file_writer_event_queue_item_t*) calloc(1, sizeof(struct file_writer_event_queue_item_t));
    if (!event) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate FILE_WRITER_EVENT_AUDIO_FRAME event to be posted to the global thread\n");
        return 2;
    }

    event->event = FILE_WRITER_EVENT_VIDEO_FRAME;
    event->context = file_writer->crn;
    event->in = (void*)avframe;
    g_async_queue_push(file_writer->main_loop_queue, event);
    return 0;
}

int file_writer_close(struct file_writer_instance_t* file_writer)
{
    /* Flush the encoder as it may have delayed frames. */
    int data_written;
    do {
        data_written = 0;
        write_audio_frame(file_writer, NULL, &data_written);
    } while (data_written);

    do {
        data_written = 0;
        write_video_frame(file_writer, NULL, &data_written);
    } while (data_written);

    int ret = av_write_trailer(file_writer->format_ctx_out);
    if (ret) {
        printf("no trailer!\n");
    }

    if (file_writer->audio_filter_graph)
        avfilter_graph_free(&file_writer->audio_filter_graph);

    if (file_writer->video_filter_graph)
        avfilter_graph_free(&file_writer->video_filter_graph);

    if(file_writer->video_ctx_out)
        avcodec_close(file_writer->video_ctx_out);

    if(file_writer->audio_ctx_out)
        avcodec_close(file_writer->audio_ctx_out);

    if (!(file_writer->format_ctx_out->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&file_writer->format_ctx_out->pb);
    }
    
    avformat_free_context(file_writer->format_ctx_out);

    if (file_writer->filep) {
        fclose(file_writer->filep);
    }

    for (int i = 0; i < MAX_RESAMPLERS; ++i) {
        if (file_writer->resamplers[i].resample_context) {
            swr_close(file_writer->resamplers[i].resample_context);
            swr_free(&(file_writer->resamplers[i].resample_context));
        }
        else
            break;
    }

    for (int i = 0; i < MAX_SWSCALERS; ++i) {
        if (file_writer->swscalers[i].sws_context) {
            sws_freeContext(file_writer->swscalers[i].sws_context);
        }
        else
            break;
    }

    if (file_writer->fifo)
        av_audio_fifo_free(file_writer->fifo);

    printf("File write done!\n");
    return 0;
}

void file_writer_destroy_context(struct file_writer_instance_t* writer) {

    /* Post an event to the global thread */
    struct file_writer_event_queue_item_t* event = (struct file_writer_event_queue_item_t*) calloc(1, sizeof(struct file_writer_event_queue_item_t));
    if (event) {
        event->event = FILE_WRITER_EVENT_DESTROYED;
        event->context = writer->crn;
        g_async_queue_push(writer->main_loop_queue, event);
    }
}

void file_writer_free(struct file_writer_t* file_writer) {
    /* Post an event to the global thread */
    struct file_writer_event_queue_item_t* event = (struct file_writer_event_queue_item_t*) calloc(1, sizeof(struct file_writer_event_queue_item_t));
    if (event) {
        event->event = FILE_WRITER_EVENT_FREED;
        g_async_queue_push(file_writer->main_loop_queue, event);
    }
}

static int init_audio_filters(struct file_writer_instance_t* file_writer, const char* filters_descr)
{
    char args[512];
    int ret = 0;
    const AVFilter* abuffersrc = avfilter_get_by_name("abuffer");
    const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    static enum AVSampleFormat out_sample_fmts[2];
    out_sample_fmts[0] = out_audio_format;
    out_sample_fmts[1] = -1;
    static const int64_t out_channel_layouts[] = { AV_CH_LAYOUT_MONO, -1 };
    static const int out_sample_rates[] = { 48000, -1 };
    const AVFilterLink* outlink;
    AVRational time_base = { 1, 1000000 };

    file_writer->audio_filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !file_writer->audio_filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer audio source: the decoded frames from the decoder will be inserted here. */
    if (!file_writer->audio_ctx_out->channel_layout) {
        file_writer->audio_ctx_out->channel_layout =
            av_get_default_channel_layout(file_writer->audio_ctx_out->channels);
    }
    snprintf(args, sizeof(args),
        "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
        time_base.num, time_base.den,
        file_writer->audio_ctx_out->sample_rate,
        av_get_sample_fmt_name(AV_SAMPLE_FMT_S16),
        file_writer->audio_ctx_out->channel_layout);
    ret = avfilter_graph_create_filter(&file_writer->audio_buffersrc_ctx,
        abuffersrc, "in",
        args, NULL,
        file_writer->audio_filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
        goto end;
    }

    /* buffer audio sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&file_writer->audio_buffersink_ctx,
        abuffersink, "out",
        NULL, NULL,
        file_writer->audio_filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
        goto end;
    }


    ret = av_opt_set_int_list(file_writer->audio_buffersink_ctx,
        "sample_fmts", out_sample_fmts, -1,
        AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
        goto end;
    }

    ret = av_opt_set_int_list(file_writer->audio_buffersink_ctx,
        "channel_layouts", out_channel_layouts, -1,
        AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
        goto end;
    }

    ret = av_opt_set_int_list(file_writer->audio_buffersink_ctx,
        "sample_rates", out_sample_rates, -1,
        AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
        goto end;
    }


    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

     /*
      * The buffer source output must be connected to the input pad of
      * the first filter described by filters_descr; since the first
      * filter input label is not specified, it is set to "in" by
      * default.
      */
    outputs->name = av_strdup("in");
    outputs->filter_ctx = file_writer->audio_buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name = av_strdup("out");
    inputs->filter_ctx = file_writer->audio_buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;

    if ((ret = avfilter_graph_parse_ptr(file_writer->audio_filter_graph,
        filters_descr,
        &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(file_writer->audio_filter_graph,
        NULL)) < 0)
        goto end;

    /* Print summary of the sink buffer
     * Note: args buffer is reused to store channel layout string */
    outlink = file_writer->audio_buffersink_ctx->inputs[0];
    av_get_channel_layout_string(args, sizeof(args), -1,
        outlink->channel_layout);
    av_log(NULL, AV_LOG_INFO, "Output: srate:%dHz fmt:%s chlayout:%s\n",
        (int)outlink->sample_rate,
        (char*)av_x_if_null(av_get_sample_fmt_name(outlink->format), "?"),
        args);

    printf("%s\n", avfilter_graph_dump(file_writer->audio_filter_graph, NULL));
end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}


static int init_video_filters(struct file_writer_instance_t* file_writer, const char* filters_descr, int out_width, int out_height)
{
    char args[512];
    int ret = 0;
    const AVFilter* buffersrc = avfilter_get_by_name("buffer");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    //AVRational time_base = dec_ctx->time_base;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
    AVRational out_aspect_ratio = { out_width , out_height };

    file_writer->video_filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !file_writer->video_filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source */
    snprintf(args, sizeof(args),
        "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
        640,
        360,
        out_pix_format,
        global_time_base.num, global_time_base.den,
        16,
        9);

    ret = avfilter_graph_create_filter(&file_writer->video_buffersrc_ctx,
        buffersrc, "in",
        args, NULL,
        file_writer->video_filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&file_writer->video_buffersink_ctx,
        buffersink, "out",
        NULL, NULL,
        file_writer->video_filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

    /*
        ret = av_opt_set_int_list(file_writer->video_buffersink_ctx,
                                  "pix_fmts", pix_fmts,
                                  AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
            goto end;
        }
    */
    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

     /*
      * The buffer source output must be connected to the input pad of
      * the first filter described by filters_descr; since the first
      * filter input label is not specified, it is set to "in" by
      * default.
      */
    outputs->name = av_strdup("in");
    outputs->filter_ctx = file_writer->video_buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name = av_strdup("out");
    inputs->filter_ctx = file_writer->video_buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;

    if ((ret = avfilter_graph_parse_ptr(file_writer->video_filter_graph,
        filters_descr,
        &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(file_writer->video_filter_graph,
        NULL)) < 0)
        goto end;

    printf("%s\n", avfilter_graph_dump(file_writer->video_filter_graph, NULL));
end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

int process_audio_frame(struct file_writer_instance_t* file_writer, AVFrame* frame)
{
#if USE_AUDIO_FILTER
    AVFrame* filt_frame = av_frame_alloc();
    int ret = av_buffersrc_add_frame_flags(file_writer->audio_buffersrc_ctx,
        frame, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the audio filtergraph [%s]\n", av_err2str(ret));
    }

    /* pull filtered audio from the filtergraph */
    while (0 == ret) {
        ret = av_buffersink_get_frame(file_writer->audio_buffersink_ctx,
            filt_frame);
        if (ret < 0) {
            break;
        }
        ret = write_audio_frame(file_writer, filt_frame);
        av_frame_unref(filt_frame);
    }
    av_frame_free(&filt_frame);
#else

#if USE_SWR
    int ret = -1, i;

    /* Find\allocate resampler */
    for (i = 0; i < MAX_RESAMPLERS; ++i) {
        /* Find/allocate resampler */
        if (!file_writer->resamplers[i].resample_context) {
            file_writer->resamplers[i].channels = frame->channels;
            file_writer->resamplers[i].sample_fmt = frame->format;
            file_writer->resamplers[i].sample_rate = frame->sample_rate;
            ret = init_resampler(&(file_writer->resamplers[i]));
            break;
        }
        else if (file_writer->resamplers[i].channels == frame->channels &&
            file_writer->resamplers[i].sample_rate == frame->sample_rate &&
            file_writer->resamplers[i].sample_fmt == frame->format) {
            ret = 0;
            break;
        }
    }
    if (!ret && i < MAX_RESAMPLERS) {
        AVFrame* avframe = av_frame_alloc();
        avframe->format = AV_SAMPLE_FMT_FLTP;
        avframe->channels = 1;
        avframe->channel_layout = av_get_default_channel_layout(avframe->channels);
        avframe->sample_rate = 48000;
        avframe->pts = (frame->pts * avframe->sample_rate) / 1000000;

        ret = swr_convert_frame(file_writer->resamplers[i].resample_context, avframe, frame);
        if (ret) {
            fprintf(stderr, "swr_convert_frame() failed: [%s]\n", av_err2str(ret));
            av_frame_free(&avframe);
            goto exit;
        }

        /* Make the FIFO as large as it needs to be to hold both the old and the new samples. */
        ret = av_audio_fifo_realloc(file_writer->fifo, av_audio_fifo_size(file_writer->fifo) + avframe->nb_samples);
        if (ret) {
            fprintf(stderr, "av_audio_fifo_realloc() failed: [%s]\n", av_err2str(ret));
            av_frame_free(&avframe);
            goto exit;
        }

        /* Store the new samples in the FIFO buffer. */
        ret = av_audio_fifo_write(file_writer->fifo, avframe->data, avframe->nb_samples);
        if (ret < avframe->nb_samples) {
            fprintf(stderr, "av_audio_fifo_write() failed: [%s]\n", av_err2str(ret));
            av_frame_free(&avframe);
        }

        av_frame_free(&avframe);

        /* Fetch any remaining samples */
        avframe = av_frame_alloc();
        avframe->format = AV_SAMPLE_FMT_FLTP;
        avframe->channels = 1;
        avframe->channel_layout = av_get_default_channel_layout(avframe->channels);
        avframe->sample_rate = 48000;
        int64_t delay = swr_get_delay(file_writer->resamplers[i].resample_context, avframe->sample_rate);
        if (delay) {
            avframe->pts = ((frame->pts * avframe->sample_rate) / 1000000) + delay;
            ret = swr_convert_frame(file_writer->resamplers[i].resample_context, avframe, NULL);
            if (ret) {
                fprintf(stderr, "swr_convert_frame() failed: [%s]\n", av_err2str(ret));
                av_frame_free(&avframe);
                goto exit;
            }

            /* Make the FIFO as large as it needs to be to hold both the old and the new samples. */
            ret = av_audio_fifo_realloc(file_writer->fifo, av_audio_fifo_size(file_writer->fifo) + avframe->nb_samples);
            if (ret) {
                fprintf(stderr, "av_audio_fifo_realloc() failed: [%s]\n", av_err2str(ret));
                av_frame_free(&avframe);
                goto exit;
            }

            /* Store the new samples in the FIFO buffer. */
            ret = av_audio_fifo_write(file_writer->fifo, avframe->data, avframe->nb_samples);
            if (ret < avframe->nb_samples) {
                fprintf(stderr, "av_audio_fifo_write() failed: [%s]\n", av_err2str(ret));
                av_frame_free(&avframe);
            }
            av_frame_free(&avframe);
        }

        /* Pull samples from the audio fifo and write to file */
        while (av_audio_fifo_size(file_writer->fifo) >= file_writer->audio_ctx_out->frame_size) {
            /* Take one frame worth of audio samples from the FIFO buffer, * encode it and write it to the output file. */
            AVFrame* output_frame = av_frame_alloc();
            if (!output_frame) {
                fprintf(stderr, "av_frame_alloc() failed\n");
                ret = AVERROR(ENOMEM);
                goto exit;
            }

            /* Set the frame's parameters, especially its size and format.
             * av_frame_get_buffer needs this to allocate memory for the
             * audio samples of the frame.
             * Default channel layouts based on the number of channels
             * are assumed for simplicity. */
            output_frame->nb_samples = file_writer->audio_ctx_out->frame_size;
            output_frame->channel_layout = file_writer->audio_ctx_out->channel_layout;
            output_frame->format = file_writer->audio_ctx_out->sample_fmt;
            output_frame->sample_rate = file_writer->audio_ctx_out->sample_rate;

            /* Allocate the samples of the created frame. This call will make
            * sure that the audio frame can hold as many samples as specified. */
            if ((ret = av_frame_get_buffer(output_frame, 0)) < 0) {
                fprintf(stderr, "Could not allocate output frame samples (error '%s')\n", av_err2str(ret));
                av_frame_free(&output_frame);
                goto exit;
            }

            /* Read as many samples from the FIFO buffer as required to fill the frame.
             * The samples are stored in the frame temporarily. */
            ret = av_audio_fifo_read(file_writer->fifo, (void**)output_frame->data, file_writer->audio_ctx_out->frame_size);
            if (ret < file_writer->audio_ctx_out->frame_size) {
                fprintf(stderr, "Could not read data from FIFO %s\n", av_err2str(ret));
                av_frame_free(&output_frame);
                goto exit;
            }

            ret = write_audio_frame(file_writer, output_frame, NULL);
            if (ret) {
                fprintf(stderr, "write_audio_frame() failed %s\n", av_err2str(ret));
                av_frame_free(&output_frame);
                goto exit;
            }
            av_frame_free(&output_frame);
        }
    }
    else {
        fprintf(stderr, "Could not allocate resampler\n");
        goto exit;
    }
#else
    int ret = write_audio_frame(file_writer, frame);
#endif
#endif
exit:
    return ret;
}

int process_video_frame(struct file_writer_instance_t* file_writer, AVFrame *frame) {
#if USE_VIDEO_FILTER
    AVFrame *filt_frame = av_frame_alloc();

    int ret = av_buffersrc_add_frame_flags(file_writer->video_buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF);

    /* push the output frame into the filtergraph */
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR,
               "Error while feeding the filtergraph\n");
        goto end;
    }

    /* pull filtered frames from the filtergraph */
    while (1) {
        ret = av_buffersink_get_frame(file_writer->video_buffersink_ctx,
                                      filt_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            ret = 0;
            break;
        }
        if (ret < 0) {
            goto end;
        }
        write_video_frame(file_writer, filt_frame, NULL);
        av_frame_unref(filt_frame);
    }
end:
    av_frame_free(&filt_frame);
#else

#if USE_SWS
    int ret = -1, i;

    /* Find\allocate resampler */
    for (i = 0; i < MAX_SWSCALERS; ++i) {
        /* Find/allocate resampler */
        if (!file_writer->swscalers[i].sws_context) {
            ret = init_swscaler(file_writer, &(file_writer->swscalers[i]), frame->width, frame->height);
            break;
        }
        else if (file_writer->swscalers[i].width == frame->width &&
            file_writer->swscalers[i].height == frame->height &&
            file_writer->swscalers[i].fmt == frame->format) {
            ret = 0;
            break;
        }
    }
    if (!ret && i < MAX_SWSCALERS) {
        AVFrame* avframe = av_frame_alloc();
        avframe->format = AV_PIX_FMT_YUV420P;
        avframe->width = file_writer->out_width;
        avframe->height = file_writer->out_height;
        avframe->pts = frame->pts;
        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, avframe->width, avframe->height, 1);;
        uint8_t *buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));

        ret = av_image_fill_arrays(avframe->data, avframe->linesize, buffer, AV_PIX_FMT_YUV420P, avframe->width, avframe->height, 1);
        if (ret < 0) {
            fprintf(stderr, "Could not allocate raw picture buffer\n");
        }
        else {
            ret = sws_scale(file_writer->swscalers[i].sws_context, frame->data, frame->linesize, 0, frame->height, avframe->data, avframe->linesize);
            if (ret < 0) {
                fprintf(stderr, "sws_scale() failed: [%s]\n", av_err2str(ret));
                fflush(stderr);
            }
            else {
                write_video_frame(file_writer, avframe, NULL);
            }
        }
        av_free(buffer);
        av_frame_free(&avframe);
    }
    else {
        fprintf(stderr, "Could not allocate swscaler\n");
    }
#else
    int ret = write_video_frame(file_writer, frame, NULL);
#endif
#endif
    return ret;
}


static void main_loop_queue_item_free(gpointer data) {

    if (data)
        free(data);
}

static char* get_next_crn()
{
    static int crn;
    size_t count = 0;
    int n = INT_MAX;

    /* Count the number of digits in the max integer */
    while (n != 0) {
        n = n / 10;
        ++count;
    }

    char* str_crn = (char*)calloc(count + 1, sizeof(char));
    if (str_crn) {
        sprintf(str_crn, "%d", ++crn);
    }
    return str_crn;
}

static gpointer cgs_file_writer_recording_thread(gpointer context)
{
    struct file_writer_t* file_writer = (struct file_writer_t*)context;

    /* Event loop here */
    while (!file_writer->exit) {
        struct file_writer_event_queue_item_t* event = (struct file_writer_event_queue_item_t*)g_async_queue_timeout_pop(file_writer->main_loop_queue, 1000000);
        if (event) {
            struct file_writer_instance_t* pfw = (struct file_writer_instance_t*)g_hash_table_lookup(file_writer->main_context_hash_table, event->context);
            if (pfw) {
                printf("\n\nFILE WRITER INSTANCE[%s] Got event %d\n\n", event->context, event->event);
                switch (event->event) {
                    case FILE_WRITER_EVENT_CREATED: {
                        break;
                    }
                    case FILE_WRITER_EVENT_AUDIO_FRAME: {
                        av_frame_free(&(AVFrame*)event->in);
                        break;
                    }
                    case FILE_WRITER_EVENT_VIDEO_FRAME: {
                        av_frame_free(&(AVFrame*)event->in);
                        break;
                    }
                    case FILE_WRITER_EVENT_DESTROYED: {
                        file_writer_close(pfw);
                        g_hash_table_remove(file_writer->main_context_hash_table, event->context);
                        free(event->context);
                        free(pfw);
                        break;
                    }
                }
            }
            else if (event->event == FILE_WRITER_EVENT_FREED) {
                file_writer->exit = 1;
            } else {
                printf("\n\nStray event %d\n\n", event->event);
            }
            free(event);
        }
    }

    g_async_queue_unref(file_writer->main_loop_queue);
    g_hash_table_destroy(file_writer->main_context_hash_table);
    free(file_writer);
    int retval;
    g_thread_exit(&retval);

    return NULL;
}



static gpointer cgs_file_writer_individual_recording_thread(gpointer context)
{
    struct file_writer_instance_t* file_writer = (struct file_writer_instance_t*)context;

    /* Event loop here */
    while (!file_writer->exit) {
        struct file_writer_event_queue_item_t* event = (struct file_writer_event_queue_item_t*)g_async_queue_timeout_pop(file_writer->main_loop_queue, 1000000);
        if (event) {
            switch (event->event) {
                case FILE_WRITER_EVENT_AUDIO_FRAME: {
                    process_audio_frame(file_writer, (AVFrame*)event->in);
                    break;
                }
                case FILE_WRITER_EVENT_VIDEO_FRAME: {
                    process_video_frame(file_writer, (AVFrame*)event->in);
                    break;
                }
                case FILE_WRITER_EVENT_DESTROYED: {
                    file_writer->exit = 1;
                    break;
                }
            }
            g_async_queue_push(file_writer->file_writer->main_loop_queue, event);
        }
    }

    int retval;
    g_thread_exit(&retval);

    return NULL;
}

static int write_video_frame(struct file_writer_instance_t* file_writer, AVFrame* frame, int* data_present)
{
    int ret;
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);

    /* encode the image */
    ret = avcodec_send_frame(file_writer->video_ctx_out, frame);
    if (ret == AVERROR_EOF) {
        av_packet_unref(&pkt);
        return 0;
    }
    else if (ret < 0) {
        fprintf(stderr, "Error encoding video frame, avcodec_send_frame() failed: %s\n", av_err2str(ret));
        av_packet_unref(&pkt);
        return ret;
    }

    ret = avcodec_receive_packet(file_writer->video_ctx_out, &pkt);
    if ((AVERROR(EAGAIN) == ret) || (ret == AVERROR_EOF)) {
        av_packet_unref(&pkt);
        return 0;
    }
    else if (ret < 0) {
        fprintf(stderr, "Error encoding video frame, avcodec_receive_packet() failed: %s\n", av_err2str(ret));
        av_packet_unref(&pkt);
        return ret;
    }

    if (data_present)
        *data_present = 1;

    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(&pkt, global_time_base, file_writer->video_stream->time_base);
    pkt.stream_index = file_writer->video_stream->index;

    /* Write the compressed frame to the media file. */
    printf("Write video frame %lld, size=%d pts=%lld\n",
        file_writer->video_frame_ct, pkt.size, pkt.pts);
    file_writer->video_frame_ct++;
    ret = safe_write_packet(file_writer, &pkt);
    av_packet_unref(&pkt);
    return ret;
}
