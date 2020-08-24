
#ifndef audio_mixer_h
#define audio_mixer_h

#ifdef __cplusplus
extern "C" {
#endif

#include <libavformat/avformat.h>

struct audio_mixer_t;

enum audio_mixer_error_t {
	CGS_AUDIO_MIXER_ERROR_SUCCESS,
	CGS_AUDIO_MIXER_ERROR_NOMEM,
	CGS_AUDIO_MIXER_ERROR_BAD_ARG,
	CGS_AUDIO_MIXER_ERROR_THREAD_CREATE,
	CGS_AUDIO_MIXER_ERROR_FFMPEG
};

int audio_mixer_alloc(struct audio_mixer_t** audio_mixer, AVCodecContext* audio_ctx_out);

int audio_mixer_start(struct audio_mixer_t* audio_mixer);

int audio_mixer_add_frame(struct audio_mixer_t* audio_mixer, AVFrame* input_frame);

int audio_mixer_finish(struct audio_mixer_t* audio_mixer, AVFrame** output_frame);

void audio_mixer_free(struct audio_mixer_t* writer);

#ifdef __cplusplus
}
#endif
#endif
