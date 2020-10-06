
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
	CGS_AUDIO_MIXER_ERROR_THREAD_POOL,
	CGS_AUDIO_MIXER_ERROR_NO_FRAME_ADDED
};

enum audio_mixer_event_code {
	AUDIO_MIXER_EVENT_UNKNOWN,
	AUDIO_MIXER_EVENT_ALLOCATED,
	AUDIO_MIXER_EVENT_FREED,
	AUDIO_MIXER_EVENT_JOB,
	AUDIO_MIXER_EVENT_FRAME_READY
};

struct audio_mixer_event {
	enum audio_mixer_event_code code;
	void* in;
};

struct audio_mixer_event_frame_ready_data {
	int serial_number;
	AVFrame *frame;
};

typedef int (*audio_mixer_event_callback)(struct audio_mixer_t* audio_mixer, struct audio_mixer_event* pevent, void* user_context);

int audio_mixer_alloc(struct audio_mixer_t** audio_mixer, AVCodecContext* audio_ctx_out, audio_mixer_event_callback callback, void* user_context);
int audio_mixer_start(struct audio_mixer_t* audio_mixer);
int audio_mixer_add_frame(struct audio_mixer_t* audio_mixer, AVFrame* input_frame);
int audio_mixer_finish(struct audio_mixer_t* audio_mixer, int64_t pts);
void audio_mixer_free(struct audio_mixer_t* audio_mixer);

#ifdef __cplusplus
}
#endif
#endif
