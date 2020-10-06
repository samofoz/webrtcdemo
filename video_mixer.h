
#ifndef video_mixer_h
#define video_mixer_h

#ifdef __cplusplus
extern "C" {
#endif

#include <libavformat/avformat.h>

struct video_mixer_t;

enum video_mixer_error_t {
	CGS_VIDEO_MIXER_ERROR_SUCCESS,
	CGS_VIDEO_MIXER_ERROR_NOMEM,
	CGS_VIDEO_MIXER_ERROR_BAD_ARG,
	CGS_VIDEO_MIXER_ERROR_THREAD_CREATE,
	CGS_VIDEO_MIXER_ERROR_THREAD_POOL,
	CGS_VIDEO_MIXER_ERROR_NO_FRAME_ADDED
};

enum object_fit_t {
	object_fit_contain = 0,
	object_fit_cover = 1,
	object_fit_fill = 2,
	object_fit_none = 3,
	object_fit_scale_down = 4
};


struct border_t {
	int radius;
	int width;
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

enum video_mixer_event_code {
	VIDEO_MIXER_EVENT_UNKNOWN,
	VIDEO_MIXER_EVENT_ALLOCATED,
	VIDEO_MIXER_EVENT_FREED,
	VIDEO_MIXER_EVENT_JOB,
	VIDEO_MIXER_EVENT_FRAME_READY
};

struct video_mixer_event {
	enum video_mixer_event_code code;
	void* in;
};

struct video_mixer_event_frame_ready_data {
	int serial_number;
	AVFrame *frame;
};

typedef int (*video_mixer_event_callback)(struct video_mixer_t* video_mixer, struct video_mixer_event* pevent, void* user_context);
typedef int (*video_mixer_compare_func)(void* user_instance_context1, void* user_instance_context2);


int video_mixer_alloc(struct video_mixer_t** video_mixer, int width, int height, video_mixer_event_callback callback, void* user_context);
int video_mixer_start(struct video_mixer_t* video_mixer);
int video_mixer_add_frame(struct video_mixer_t* video_mixer,
							AVFrame* input_frame,
							size_t x_offset,
							size_t y_offset,
							struct border_t border,
							size_t output_width,
							size_t output_height,
							enum object_fit_t object_fit,
							void* user_instance_context,
							video_mixer_compare_func compare_func);
int video_mixer_finish(struct video_mixer_t* video_mixer, int64_t pts);
void video_mixer_free(struct video_mixer_t* video_mixer);

#ifdef __cplusplus
}
#endif
#endif
