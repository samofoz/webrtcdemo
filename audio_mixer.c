
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include <assert.h>

#include "audio_mixer.h"


struct audio_mixer_t {
    AVFrame* output_frame;
    int frames_mixed;
};


int audio_mixer_alloc(struct audio_mixer_t** audio_mixer, AVCodecContext* audio_ctx_out){
    int ret;

    *audio_mixer = (struct audio_mixer_t*)calloc(1, sizeof(struct audio_mixer_t));
    if (*audio_mixer == NULL) {
        ret = CGS_AUDIO_MIXER_ERROR_NOMEM;
        goto GET_OUT;
    }

    /* Allocate the output frame */
    (*audio_mixer)->output_frame = av_frame_alloc();
    if (!(*audio_mixer)->output_frame) {
        printf("av_frame_alloc() failed\n");
        ret = CGS_AUDIO_MIXER_ERROR_NOMEM;
        goto GET_OUT;
    }
    (*audio_mixer)->output_frame->nb_samples = audio_ctx_out->frame_size;
    (*audio_mixer)->output_frame->channel_layout = audio_ctx_out->channel_layout;
    (*audio_mixer)->output_frame->format = audio_ctx_out->sample_fmt;
    (*audio_mixer)->output_frame->sample_rate = audio_ctx_out->sample_rate;
    if ((ret = av_frame_get_buffer((*audio_mixer)->output_frame, 0)) < 0) {
        printf("Could not allocate output frame samples (error '%s')\n", av_err2str(ret));
        ret = CGS_AUDIO_MIXER_ERROR_FFMPEG;
        goto GET_OUT;
    }
    audio_mixer_start(*audio_mixer);

    return CGS_AUDIO_MIXER_ERROR_SUCCESS;

GET_OUT:
    if (*audio_mixer) {
        if((*audio_mixer)->output_frame)
            av_frame_free(&(*audio_mixer)->output_frame);
        free(*audio_mixer);
    }
    return ret;
}



void audio_mixer_free(struct audio_mixer_t* audio_mixer) {
    av_frame_free(&audio_mixer->output_frame);
    free(audio_mixer);
}


int audio_mixer_start(struct audio_mixer_t* audio_mixer) {
    audio_mixer->frames_mixed = 0;
    for (int i = 0; i < audio_mixer->output_frame->channels; i++) {
        memset(audio_mixer->output_frame->data[i], 0, (size_t)audio_mixer->output_frame->nb_samples * av_get_bytes_per_sample(audio_mixer->output_frame->format));
    }
    return CGS_AUDIO_MIXER_ERROR_SUCCESS;
}

int audio_mixer_add_frame(struct audio_mixer_t* audio_mixer, AVFrame* input_frame) {
    float* dest_samples = (float*)audio_mixer->output_frame->data[0];
    int16_t* src_samples = (int16_t*)input_frame->data[0];
    for (int k = 0; k < input_frame->nb_samples; ++k) {
        dest_samples[k] += (((float)src_samples[k]) / INT16_MAX);
        if (fabs(dest_samples[k]) > 1.0) {
            printf("audio clip detected\n");
            dest_samples[k] = (float)fmin(1.0, dest_samples[k]);
            dest_samples[k] = (float)fmax(-1.0, dest_samples[k]);
        }
    }
    audio_mixer->frames_mixed++;
    return CGS_AUDIO_MIXER_ERROR_SUCCESS;
}

int audio_mixer_get_frames_mixed(struct audio_mixer_t* audio_mixer) {
    return audio_mixer->frames_mixed;
}

int audio_mixer_finish(struct audio_mixer_t* audio_mixer, AVFrame** output_frame) {
    *output_frame = audio_mixer->output_frame;
    return CGS_AUDIO_MIXER_ERROR_SUCCESS;
}
