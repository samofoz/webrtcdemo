//
//  file_writer.h
//  barc
//
//  Created by Charley Robinson on 2/9/17.
//

#ifndef file_writer_h
#define file_writer_h

#ifdef __cplusplus
extern "C" {
#endif


struct file_writer_t;
struct file_writer_instance_t;

enum file_writer_error {
	CGS_FILE_WRITER_ERROR_SUCCESS,
	CGS_FILE_WRITER_ERROR_NOMEM,
	CGS_FILE_WRITER_ERROR_BAD_ARG,
	CGS_FILE_WRITER_ERROR_THREAD_CREATE
};

int file_writer_alloc(struct file_writer_t** writer);

int file_writer_create_context(struct file_writer_t* writer,
	struct file_writer_instance_t** writer_instance);


int file_writer_push_audio_data(struct file_writer_instance_t* writer_instance,
								const void* audio_data,
								int bits_per_sample,
								int sample_rate,
								size_t number_of_channels,
								size_t number_of_frames);
int file_writer_push_video_frame(struct file_writer_instance_t* writer_instance,
								int64_t ts_us, int w, int h,
								uint8_t* y, uint8_t* u, uint8_t* v,
								uint32_t pitchY, uint32_t pitchU, uint32_t pitchV);

void file_writer_destroy_context(struct file_writer_instance_t* writer_instance);

void file_writer_free(struct file_writer_t* writer);

#ifdef __cplusplus
}
#endif
#endif
