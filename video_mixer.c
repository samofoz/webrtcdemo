
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <MagickWand/MagickWand.h>
#include <MagickWand/magick-image.h>
#include <glib.h>

#include "video_mixer.h"

#define RGB_BYTES_PER_PIXEL 3

struct video_mixer_job_t;

struct video_mixer_t {
    int width;
    int height;

    video_mixer_event_callback callback;
    void* user_context;

    GThread* pgthread;
    GAsyncQueue* main_loop_queue;
    int exit;

    GThreadPool* pgthread_pool;

    struct video_mixer_job_t* current_job;
    int current_serial_number;
};


struct video_mixer_job_frame_t {
    AVFrame* avframe;
    size_t x_offset;
    size_t y_offset;
    struct border_t border;
    size_t output_width;
    size_t output_height;
    enum object_fit_t object_fit;
    void* user_instance_context;
};


struct video_mixer_job_t {
    struct video_mixer_t* video_mixer;
    GAsyncQueue* frames;
    int serial_number;
    int64_t pts;
};

static void draw_border_radius(MagickWand* wand, int radius, size_t width, size_t height);
static void draw_border_stroke(MagickWand* wand, size_t width, size_t height, double thickness, double red, double green, double blue);
static void main_loop_queue_item_free(gpointer data);
static gpointer video_mixer_thread(gpointer context);
static void video_mixer_do_mix_job(gpointer data, gpointer user_data);

int video_mixer_alloc(struct video_mixer_t** video_mixer, int width, int height, video_mixer_event_callback callback, void* user_context) {
    int ret;

    *video_mixer = (struct video_mixer_t*)calloc(1, sizeof(struct video_mixer_t));
    if (*video_mixer == NULL) {
        ret = CGS_VIDEO_MIXER_ERROR_NOMEM;
        goto GET_OUT;
    }

    (*video_mixer)->width = width;
    (*video_mixer)->height = height;
    (*video_mixer)->callback = callback;
    (*video_mixer)->user_context = user_context;

    (*video_mixer)->main_loop_queue = g_async_queue_new_full(main_loop_queue_item_free);
    if (!(*video_mixer)->main_loop_queue)
    {
        ret = CGS_VIDEO_MIXER_ERROR_NOMEM;
        goto GET_OUT;
    }

    /* Post an event */
    struct video_mixer_event* event = (struct video_mixer_event*) calloc(1, sizeof(struct video_mixer_event));
    if (!event) {
        ret = CGS_VIDEO_MIXER_ERROR_NOMEM;
        goto GET_OUT;
    }
    event->code = VIDEO_MIXER_EVENT_ALLOCATED;
    g_async_queue_push((*video_mixer)->main_loop_queue, event);

    GError* error;
    (*video_mixer)->pgthread_pool = g_thread_pool_new(video_mixer_do_mix_job, *video_mixer, 
#if 0
                                                        g_get_num_processors(),
#else
                                                        1,
#endif
                                                        FALSE, &error);
    if ((*video_mixer)->pgthread_pool == NULL) {
        ret = CGS_VIDEO_MIXER_ERROR_THREAD_POOL;
        goto GET_OUT;
    }

    (*video_mixer)->pgthread = g_thread_try_new("video_mixer_thread", video_mixer_thread, *video_mixer, &error);
    if ((*video_mixer)->pgthread == NULL) {
        ret = CGS_VIDEO_MIXER_ERROR_THREAD_CREATE;
        goto GET_OUT;
    }

    return CGS_VIDEO_MIXER_ERROR_SUCCESS;

GET_OUT:
    if (*video_mixer) {
        if ((*video_mixer)->pgthread_pool)
            g_thread_pool_free((*video_mixer)->pgthread_pool, TRUE, TRUE);
        if ((*video_mixer)->main_loop_queue)
            g_async_queue_unref((*video_mixer)->main_loop_queue);
        free(*video_mixer);
    }
    return ret;
}

int video_mixer_start(struct video_mixer_t* video_mixer) {
    printf("video_mixer_start()\n");

    int ret;
    video_mixer->current_job = (struct video_mixer_job_t*)calloc(1, sizeof(struct video_mixer_job_t));
    if (!video_mixer->current_job) {
        ret = CGS_VIDEO_MIXER_ERROR_NOMEM;
        goto OUT;
    }

    video_mixer->current_job->frames = g_async_queue_new_full(main_loop_queue_item_free);
    if (!video_mixer->current_job->frames) {
        ret = CGS_VIDEO_MIXER_ERROR_NOMEM;
        goto OUT;
    }

    video_mixer->current_job->video_mixer = video_mixer;
    video_mixer->current_job->serial_number = video_mixer->current_serial_number++;
    return CGS_VIDEO_MIXER_ERROR_SUCCESS;

OUT:
    if (video_mixer->current_job)
        free(video_mixer->current_job);
    return ret;
}

int video_mixer_add_frame(struct video_mixer_t* video_mixer,
                            AVFrame* input_frame,
                            size_t x_offset,
                            size_t y_offset,
                            struct border_t border,
                            size_t output_width,
                            size_t output_height,
                            enum object_fit_t object_fit, 
                            void *user_instance_context, 
                            video_mixer_compare_func compare_func) {
    printf("video_mixer_add_frame(%d)\n", input_frame->pts);
    int ret;

#if 1
    struct video_mixer_job_frame_t* job_frame_first_non_match = NULL;
    while (1) {
        struct video_mixer_job_frame_t* job_frame = g_async_queue_try_pop(video_mixer->current_job->frames);
        if (job_frame == NULL)
            break;

        if (job_frame_first_non_match == job_frame) {
            g_async_queue_push(video_mixer->current_job->frames, job_frame);
            break;
        }

        if (0 == compare_func(job_frame->user_instance_context, user_instance_context)) {
            free(job_frame);
            break;
        }

        /* non-match, push the frame back*/
        if (!job_frame_first_non_match)
            job_frame_first_non_match = job_frame;
        g_async_queue_push(video_mixer->current_job->frames, job_frame);
    }
#endif

    struct video_mixer_job_frame_t* frame = (struct video_mixer_job_frame_t*)calloc(1, sizeof(struct video_mixer_job_frame_t));
    if (!frame) {
        ret = CGS_VIDEO_MIXER_ERROR_NOMEM;
        goto OUT;
    }

    frame->border.radius = 0;
    frame->border.width = 0;
    frame->object_fit = object_fit_cover;
    frame->avframe = input_frame;
    frame->x_offset = x_offset;
    frame->y_offset = y_offset;
    frame->output_width = output_width;
    frame->output_height = output_height;
    frame->user_instance_context = user_instance_context;

    g_async_queue_push(video_mixer->current_job->frames, frame);
    return CGS_VIDEO_MIXER_ERROR_SUCCESS;

OUT:
    if (frame)
        free(frame);
    return ret;
}

int video_mixer_finish(struct video_mixer_t* video_mixer, int64_t pts) {
    GError* error;

    printf("video_mixer_finish(%d)\n", pts);
    video_mixer->current_job->pts = pts;
    printf("\n\nVideo mixer, Got video frame %d\n\n", video_mixer->current_job->serial_number);
    struct video_mixer_event* event = (struct video_mixer_event*) calloc(1, sizeof(struct video_mixer_event));
    if (event) {
        event->code = VIDEO_MIXER_EVENT_JOB;
        event->in = video_mixer->current_job;
        video_mixer->current_job = NULL;
        g_async_queue_push(video_mixer->main_loop_queue, event);
        return CGS_VIDEO_MIXER_ERROR_SUCCESS;
    } else {
        g_async_queue_unref(video_mixer->current_job->frames);
        free(video_mixer->current_job);
        video_mixer->current_job = NULL;
        return CGS_VIDEO_MIXER_ERROR_NOMEM;
    }
}

static void video_mixer_do_mix_job(gpointer data, gpointer user_data) {
    struct video_mixer_job_t* video_mixer_job = (struct video_mixer_job_t*)data;
    MagickWand* magic_wand = NewMagickWand();
    PixelWand* background = NewPixelWand();
    // set background for layout debugging
    //PixelSetGreen(background, 1.0);
    //PixelSetColor(background, "white");
    MagickNewImage(magic_wand, video_mixer_job->video_mixer->width, video_mixer_job->video_mixer->height, background);
    DestroyPixelWand(background);

    while(1){
        struct video_mixer_job_frame_t* job_frame = g_async_queue_try_pop(video_mixer_job->frames);
        if (!job_frame)
            break;

        /* Allocate scaler */
        struct SwsContext* sws_context;
        sws_context = sws_getContext(job_frame->avframe->width,
                                        job_frame->avframe->height,
                                        AV_PIX_FMT_YUV420P,
                                        video_mixer_job->video_mixer->width, //file_writer_instance->mixer_width,
                                        video_mixer_job->video_mixer->height, //file_writer_instance->mixer_height,
                                        AV_PIX_FMT_RGB24, 0, NULL, NULL, NULL);
        if (sws_context) {
            AVFrame* avframe = av_frame_alloc();
            avframe->format = AV_PIX_FMT_RGB24;
            avframe->width = video_mixer_job->video_mixer->width; //file_writer_instance->mixer_width;
            avframe->height = video_mixer_job->video_mixer->height; //file_writer_instance->mixer_height;
            int ret = av_frame_get_buffer(avframe, 0);
            if (ret < 0) {
                printf("Could not allocate raw picture buffer\n");
            }
            else {
                ret = sws_scale(sws_context, job_frame->avframe->data, job_frame->avframe->linesize, 0, job_frame->avframe->height, avframe->data, avframe->linesize);
                if (ret < 0) {
                    printf("sws_scale() failed: [%s]\n", av_err2str(ret));
                }
                else {
                    // background on image color for scale/crop debugging
                    PixelWand* background = NewPixelWand();
                    // change the color to something with an active alpha channel if you need
                    // to see what's happening with the image processor
                    //PixelSetColor(background, "none");
                    //PixelSetColor(background, "red");

                    MagickWand* input_wand = NewMagickWand();

                    // import pixel buffer (there's gotta be a better way to do this)
                    MagickBooleanType status = MagickConstituteImage(input_wand, avframe->width, avframe->height, "RGB", CharPixel, avframe->data[0]);

                    float w_factor = (float)job_frame->output_width / (float)job_frame->avframe->width;
                    float h_factor = (float)job_frame->output_height / (float)job_frame->avframe->height;
                    float scale_factor = 1;
                    float internal_x_offset = 0;
                    float internal_y_offset = 0;
                    char rescale_dimensions = 0;

                    // see https://developer.mozilla.org/en-US/docs/Web/CSS/object-fit
                    if (object_fit_fill == job_frame->object_fit) {
                        // don't preserve aspect ratio.
                        rescale_dimensions = 0;
                    }
                    else if (object_fit_scale_down == job_frame->object_fit || object_fit_contain == job_frame->object_fit) {
                        // scale to fit all source pixels inside container,
                        // preserving aspect ratio
                        scale_factor = fmin(w_factor, h_factor);
                        rescale_dimensions = 1;
                    }
                    else {
                        // fill the container completely, preserving aspect ratio
                        scale_factor = fmax(w_factor, h_factor);
                        rescale_dimensions = 1;
                    }

                    float scaled_width = job_frame->output_width;
                    float scaled_height = job_frame->output_height;

                    if (rescale_dimensions) {
                        scaled_width = job_frame->avframe->width * scale_factor;
                        scaled_height = job_frame->avframe->height * scale_factor;

                        internal_y_offset = (scaled_height - job_frame->output_height) / 2;
                        internal_x_offset = (scaled_width - job_frame->output_width) / 2;
                    }

                    MagickSetImageBackgroundColor(input_wand, background);
                    //MagickResizeImage(input_wand, scaled_width, scaled_height, Lanczos2SharpFilter);
                    // TODO: we should use extent without resizing for for object-fill: none.
                    if (rescale_dimensions) {
                        MagickExtentImage(input_wand, job_frame->output_width, job_frame->output_height, internal_x_offset, internal_y_offset);
                    }

                    if (job_frame->border.radius > 0) {
                        draw_border_radius(input_wand, job_frame->border.radius, job_frame->output_width, job_frame->output_height);
                    }

                    if (job_frame->border.width > 0) {
                        draw_border_stroke(input_wand, job_frame->output_width, job_frame->output_height, job_frame->border.width, job_frame->border.red, job_frame->border.green, job_frame->border.blue);
                    }

                    if (status == MagickFalse) {
                        printf("MagickConstituteImage() failed\n");
                    }

                    // compose source frames
                    MagickCompositeImage(magic_wand, input_wand, OverCompositeOp, MagickTrue, job_frame->x_offset, job_frame->y_offset);
                    DestroyMagickWand(input_wand);
                    DestroyPixelWand(background);
                }
            }
            av_frame_free(&avframe);
            sws_freeContext(sws_context);
        }
        else {
            fprintf(stderr, "Could not allocate scaler\n");
        }

        av_free(job_frame->avframe->data[0]);
        av_frame_free(&job_frame->avframe);
        free(job_frame);
    }

    AVFrame* frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_RGB24;
    frame->width = video_mixer_job->video_mixer->width; //file_writer_instance->mixer_width;
    frame->height = video_mixer_job->video_mixer->height; //file_writer_instance->mixer_height;
    av_frame_get_buffer(frame, 0);

    size_t width = MagickGetImageWidth(magic_wand);
    size_t height = MagickGetImageHeight(magic_wand);

    // push modified wand back to rgb buffer
    MagickExportImagePixels(magic_wand, 0, 0, width, height, "RGB", CharPixel, frame->data[0]);
    frame->linesize[0] = 3 * width;

    /* Convert back to Yuv420p */
    struct SwsContext* sws_context;
    sws_context = sws_getContext(frame->width,
                                frame->height,
                                AV_PIX_FMT_RGB24,
                                frame->width,
                                frame->height,
                                AV_PIX_FMT_YUV420P, 0, NULL, NULL, NULL);
    if (sws_context) {
        AVFrame* avframe = av_frame_alloc();
        avframe->format = AV_PIX_FMT_YUV420P;
        avframe->width = frame->width;
        avframe->height = frame->height;
        int ret = av_frame_get_buffer(avframe, 0);
        if (ret < 0) {
            printf("Could not allocate raw picture buffer\n");
            av_frame_free(&avframe);
        }
        else {
            ret = sws_scale(sws_context,
                frame->data,
                frame->linesize,
                0,
                frame->height,
                avframe->data,
                avframe->linesize);
            if (ret < 0) {
                printf("sws_scale() failed: [%s]\n", av_err2str(ret));
                av_frame_free(&avframe);
            }
            else {
                struct video_mixer_event_frame_ready_data* data = (struct video_mixer_event_frame_ready_data*)calloc(1, sizeof(struct video_mixer_event_frame_ready_data));
                if(data){
                    avframe->pts = video_mixer_job->pts;
                    data->frame = avframe;
                    data->serial_number = video_mixer_job->serial_number;
                    struct video_mixer_event event;
                    event.code = VIDEO_MIXER_EVENT_FRAME_READY;
                    event.in = data;
                    video_mixer_job->video_mixer->callback(video_mixer_job->video_mixer, &event, video_mixer_job->video_mixer->user_context);
                }
            }
        }
        sws_freeContext(sws_context);
    }
    DestroyMagickWand(magic_wand);
    g_async_queue_unref(video_mixer_job->frames);
    free(video_mixer_job);
}


void video_mixer_free(struct video_mixer_t* video_mixer) {
    /* Post an event to the thread */
    struct video_mixer_event* event = (struct video_mixer_event*) calloc(1, sizeof(struct video_mixer_event));
    if (event) {
        event->code = VIDEO_MIXER_EVENT_FREED;
        g_async_queue_push(video_mixer->main_loop_queue, event);
    }
}


static gpointer video_mixer_thread(gpointer context)
{
    struct video_mixer_t* video_mixer = (struct video_mixer_t*)context;

    /* Event loop here */
    while (!video_mixer->exit) {
        struct video_mixer_event* event = (struct video_mixer_event*)g_async_queue_timeout_pop(video_mixer->main_loop_queue, 1000000);
        if (event) {
            printf("\n\nVIDEO MIXER Got event %d\n\n", event->code);
            switch (event->code) {
                case VIDEO_MIXER_EVENT_ALLOCATED: {
                    break;
                }
                case VIDEO_MIXER_EVENT_JOB: {
                    GError* error;
                    struct video_mixer_job_t* current_job = (struct video_mixer_job_t*)event->in;
                    if (FALSE == g_thread_pool_push(video_mixer->pgthread_pool, current_job, &error)) {
                        g_async_queue_unref(current_job->frames);
                        free(current_job);
                    }
                    break;
                }
                case VIDEO_MIXER_EVENT_FREED: {
                    video_mixer->exit = 1;
                    break;
                }
                default:
                    break;
            }
            free(event);
        }
    }

    g_thread_pool_free(video_mixer->pgthread_pool, FALSE, TRUE);
    g_async_queue_unref(video_mixer->main_loop_queue);
    struct video_mixer_event event;
    event.code = VIDEO_MIXER_EVENT_FREED;
    event.in = video_mixer;
    video_mixer->callback(video_mixer, &event, video_mixer->user_context);
    free(video_mixer);
    int retval;
    g_thread_exit(&retval);

    return NULL;
}

static void main_loop_queue_item_free(gpointer data) {

    if (data)
        free(data);
}

MagickBooleanType MagickSetImageMask(MagickWand* wand, const PixelMask type, const MagickWand* clip_mask);

static void draw_border_stroke(MagickWand* wand, size_t width, size_t height, double thickness, double red, double green, double blue)
{
    PixelWand* stroke_color = NewPixelWand();
    PixelSetRed(stroke_color, red / 255);
    PixelSetGreen(stroke_color, green / 255);
    PixelSetBlue(stroke_color, blue / 255);
    PixelWand* transparent_color = NewPixelWand();
    PixelSetColor(transparent_color, "white");
    MagickWand* mask = MagickGetImageMask(wand, ReadPixelMask);
    MagickWand* stroke_wand = NewMagickWand();
    MagickNewImage(stroke_wand, width, height, transparent_color);
    MagickAddImage(stroke_wand, mask);

    MagickScaleImage(wand, width - thickness, height - thickness);

    MagickSetImageMask(stroke_wand, ReadPixelMask, mask);
    MagickFloodfillPaintImage(stroke_wand, stroke_color, 150, stroke_color,
        thickness / 2, thickness / 2, MagickTrue);
    MagickCompositeImage(stroke_wand, wand, OverCompositeOp, MagickTrue,
        thickness / 2, thickness / 2);
    MagickRemoveImage(wand);
    MagickAddImage(wand, stroke_wand);
    DestroyMagickWand(stroke_wand);
    DestroyPixelWand(transparent_color);
    DestroyPixelWand(stroke_color);
}

static void draw_border_radius(MagickWand* wand, int radius, size_t width, size_t height)
{
    PixelWand* black_pixel = NewPixelWand();
    PixelSetColor(black_pixel, "#000000");
    PixelWand* white_pixel = NewPixelWand();
    PixelSetColor(white_pixel, "#ffffff");
    DrawingWand* rounded = NewDrawingWand();
    DrawSetFillColor(rounded, white_pixel);
    DrawRoundRectangle(rounded, 1, 1, width - 1, height - 1, radius, radius);

    MagickWand* border = NewMagickWand();
    MagickNewImage(border, width, height, black_pixel);
    MagickDrawImage(border, rounded);
    MagickSetImageMask(wand, ReadPixelMask, border);

    DestroyPixelWand(black_pixel);
    DestroyPixelWand(white_pixel);
    DestroyDrawingWand(rounded);
    DestroyMagickWand(border);
}
