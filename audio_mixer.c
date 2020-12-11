
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <glib.h>

#include "audio_mixer.h"

#define RGB_BYTES_PER_PIXEL 3

struct audio_mixer_job_t;

struct audio_mixer_t {
    int nb_samples;
    uint64_t channel_layout;
    enum AVSampleFormat format;
    int sample_rate;

    audio_mixer_event_callback callback;
    void* user_context;

    GThread* pgthread;
    GAsyncQueue* main_loop_queue;
    int exit;

    GThreadPool* pgthread_pool;

    struct audio_mixer_job_t* current_job;
    int current_serial_number;
};


struct audio_mixer_job_frame_t {
    AVFrame* avframe;
};


struct audio_mixer_job_t {
    struct audio_mixer_t* audio_mixer;
    GAsyncQueue* frames;
    int serial_number;
    int64_t pts;
};

static void main_loop_queue_item_free(gpointer data);
static gpointer audio_mixer_thread(gpointer context);
static void audio_mixer_do_mix_job(gpointer data, gpointer user_data);

int audio_mixer_alloc(struct audio_mixer_t** audio_mixer, AVCodecContext* audio_ctx_out, audio_mixer_event_callback callback, void* user_context) {
    int ret;

    *audio_mixer = (struct audio_mixer_t*)calloc(1, sizeof(struct audio_mixer_t));
    if (*audio_mixer == NULL) {
        ret = CGS_AUDIO_MIXER_ERROR_NOMEM;
        goto GET_OUT;
    }

    (*audio_mixer)->callback = callback;
    (*audio_mixer)->user_context = user_context;

    (*audio_mixer)->main_loop_queue = g_async_queue_new_full(main_loop_queue_item_free);
    if (!(*audio_mixer)->main_loop_queue)
    {
        ret = CGS_AUDIO_MIXER_ERROR_NOMEM;
        goto GET_OUT;
    }

    /* Post an event */
    struct audio_mixer_event* event = (struct audio_mixer_event*) calloc(1, sizeof(struct audio_mixer_event));
    if (!event) {
        ret = CGS_AUDIO_MIXER_ERROR_NOMEM;
        goto GET_OUT;
    }
    event->code = AUDIO_MIXER_EVENT_ALLOCATED;
    g_async_queue_push((*audio_mixer)->main_loop_queue, event);

    GError* error;
    (*audio_mixer)->pgthread_pool = g_thread_pool_new(audio_mixer_do_mix_job, *audio_mixer, 
#if 1
                                                        g_get_num_processors() / 2, 
#else
                                                        1,
#endif
                                                        FALSE, &error);
    if ((*audio_mixer)->pgthread_pool == NULL) {
        ret = CGS_AUDIO_MIXER_ERROR_THREAD_POOL;
        goto GET_OUT;
    }

    (*audio_mixer)->pgthread = g_thread_try_new("audio_mixer_thread", audio_mixer_thread, *audio_mixer, &error);
    if ((*audio_mixer)->pgthread == NULL) {
        ret = CGS_AUDIO_MIXER_ERROR_THREAD_CREATE;
        goto GET_OUT;
    }

    (*audio_mixer)->nb_samples = audio_ctx_out->frame_size;
    (*audio_mixer)->channel_layout = audio_ctx_out->channel_layout;
    (*audio_mixer)->format = audio_ctx_out->sample_fmt;
    (*audio_mixer)->sample_rate = audio_ctx_out->sample_rate;

    return CGS_AUDIO_MIXER_ERROR_SUCCESS;

GET_OUT:
    if (*audio_mixer) {
        if ((*audio_mixer)->pgthread_pool)
            g_thread_pool_free((*audio_mixer)->pgthread_pool, TRUE, TRUE);
        if ((*audio_mixer)->main_loop_queue)
            g_async_queue_unref((*audio_mixer)->main_loop_queue);
        free(*audio_mixer);
    }
    return ret;
}

int audio_mixer_start(struct audio_mixer_t* audio_mixer) {
    printf("audio_mixer_start()\n");

    int ret;
    audio_mixer->current_job = (struct audio_mixer_job_t*)calloc(1, sizeof(struct audio_mixer_job_t));
    if (!audio_mixer->current_job) {
        ret = CGS_AUDIO_MIXER_ERROR_NOMEM;
        goto OUT;
    }

    audio_mixer->current_job->frames = g_async_queue_new_full(main_loop_queue_item_free);
    if (!audio_mixer->current_job->frames) {
        ret = CGS_AUDIO_MIXER_ERROR_NOMEM;
        goto OUT;
    }

    audio_mixer->current_job->audio_mixer = audio_mixer;
    audio_mixer->current_job->serial_number = audio_mixer->current_serial_number++;
    return CGS_AUDIO_MIXER_ERROR_SUCCESS;

OUT:
    if (audio_mixer->current_job)
        free(audio_mixer->current_job);
    return ret;
}

int audio_mixer_add_frame(struct audio_mixer_t* audio_mixer, AVFrame* input_frame) {
    printf("audio_mixer_add_frame(%d)\n", input_frame->pts);
    int ret;

    struct audio_mixer_job_frame_t* frame = (struct audio_mixer_job_frame_t*)calloc(1, sizeof(struct audio_mixer_job_frame_t));
    if (!frame) {
        ret = CGS_AUDIO_MIXER_ERROR_NOMEM;
        goto OUT;
    }

    frame->avframe = input_frame;
    g_async_queue_push(audio_mixer->current_job->frames, frame);
    return CGS_AUDIO_MIXER_ERROR_SUCCESS;

OUT:
    if (frame)
        free(frame);
    return ret;
}

int audio_mixer_finish(struct audio_mixer_t* audio_mixer, int64_t pts) {
    GError* error;

    printf("audio_mixer_finish(%d)\n", pts);
    audio_mixer->current_job->pts = pts;
    printf("\n\nAudio mixer, Got audio frame %d\n\n", audio_mixer->current_job->serial_number);
    struct audio_mixer_event* event = (struct audio_mixer_event*) calloc(1, sizeof(struct audio_mixer_event));
    if (event) {
        event->code = AUDIO_MIXER_EVENT_JOB;
        event->in = audio_mixer->current_job;
        audio_mixer->current_job = NULL;
        g_async_queue_push(audio_mixer->main_loop_queue, event);
        return CGS_AUDIO_MIXER_ERROR_SUCCESS;
    } else {
        g_async_queue_unref(audio_mixer->current_job->frames);
        free(audio_mixer->current_job);
        audio_mixer->current_job = NULL;
        return CGS_AUDIO_MIXER_ERROR_NOMEM;
    }
}

static void audio_mixer_do_mix_job(gpointer data, gpointer user_data) {
    struct audio_mixer_job_t* audio_mixer_job = (struct audio_mixer_job_t*)data;
    int ret;
    AVFrame *output_frame = av_frame_alloc();
    if (!output_frame) {
        printf("av_frame_alloc() failed\n");
    } else{
        output_frame->nb_samples = audio_mixer_job->audio_mixer->nb_samples;
        output_frame->channel_layout = audio_mixer_job->audio_mixer->channel_layout;
        output_frame->format = audio_mixer_job->audio_mixer->format;
        output_frame->sample_rate = audio_mixer_job->audio_mixer->sample_rate;
        if ((ret = av_frame_get_buffer(output_frame, 0)) < 0) {
            printf("Could not allocate output frame samples (error '%s')\n", av_err2str(ret));
        } else {
            for (int i = 0; i < output_frame->channels; i++)
                memset(output_frame->data[i], 0, (size_t)output_frame->nb_samples * av_get_bytes_per_sample(output_frame->format));
            while (1) {
                struct audio_mixer_job_frame_t* job_frame = g_async_queue_try_pop(audio_mixer_job->frames);
                if (!job_frame)
                    break;

                float* dest_samples = (float*)output_frame->data[0];
                int16_t* src_samples = (int16_t*)job_frame->avframe->data[0];
                for (int k = 0; k < job_frame->avframe->nb_samples; ++k) {
                    dest_samples[k] += (((float)src_samples[k]) / INT16_MAX);
                    if (fabs(dest_samples[k]) > 1.0) {
                        printf("audio clip detected\n");
                        dest_samples[k] = (float)fmin(1.0, dest_samples[k]);
                        dest_samples[k] = (float)fmax(-1.0, dest_samples[k]);
                    }
                }
                av_frame_free(&job_frame->avframe);
                free(job_frame);
            }
            struct audio_mixer_event_frame_ready_data* data = (struct audio_mixer_event_frame_ready_data*)calloc(1, sizeof(struct audio_mixer_event_frame_ready_data));
            if (data) {
                output_frame->pts = audio_mixer_job->pts;
                data->frame = output_frame;
                data->serial_number = audio_mixer_job->serial_number;
                struct audio_mixer_event event;
                event.code = AUDIO_MIXER_EVENT_FRAME_READY;
                event.in = data;
                audio_mixer_job->audio_mixer->callback(audio_mixer_job->audio_mixer, &event, audio_mixer_job->audio_mixer->user_context);
            }
        }
    }
    g_async_queue_unref(audio_mixer_job->frames);
    free(audio_mixer_job);
}


void audio_mixer_free(struct audio_mixer_t* audio_mixer) {
    /* Post an event to the thread */
    struct audio_mixer_event* event = (struct audio_mixer_event*) calloc(1, sizeof(struct audio_mixer_event));
    if (event) {
        event->code = AUDIO_MIXER_EVENT_FREED;
        g_async_queue_push(audio_mixer->main_loop_queue, event);
    }
}


static gpointer audio_mixer_thread(gpointer context)
{
    struct audio_mixer_t* audio_mixer = (struct audio_mixer_t*)context;

    /* Event loop here */
    while (!audio_mixer->exit) {
        struct audio_mixer_event* event = (struct audio_mixer_event*)g_async_queue_timeout_pop(audio_mixer->main_loop_queue, 1000000);
        if (event) {
            printf("\n\nAUDIO MIXER Got event %d\n\n", event->code);
            switch (event->code) {
                case AUDIO_MIXER_EVENT_ALLOCATED: {
                    break;
                }
                case AUDIO_MIXER_EVENT_JOB: {
                    GError* error;
                    struct audio_mixer_job_t* current_job = (struct audio_mixer_job_t*)event->in;
                    if (FALSE == g_thread_pool_push(audio_mixer->pgthread_pool, current_job, &error)) {
                        g_async_queue_unref(current_job->frames);
                        free(current_job);
                    }
                    break;
                }
                case AUDIO_MIXER_EVENT_FREED: {
                    audio_mixer->exit = 1;
                    break;
                }
                default:
                    break;
            }
            free(event);
        }
    }

    g_thread_pool_free(audio_mixer->pgthread_pool, FALSE, TRUE);
    g_async_queue_unref(audio_mixer->main_loop_queue);
    struct audio_mixer_event event;
    event.code = AUDIO_MIXER_EVENT_FREED;
    event.in = audio_mixer;
    audio_mixer->callback(audio_mixer, &event, audio_mixer->user_context);
    free(audio_mixer);
    int retval;
    g_thread_exit(&retval);

    return NULL;
}

static void main_loop_queue_item_free(gpointer data) {

    if (data)
        free(data);
}
