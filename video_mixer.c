
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <MagickWand/MagickWand.h>
#include <MagickWand/magick-image.h>

#include "video_mixer.h"
#include "yuv_rgb.h"

#define RGB_BYTES_PER_PIXEL 3

struct video_mixer_t {
    MagickWand* magic_wand;
    AVFrame* output_frame;
    int width;
    int height;
};


static void draw_border_radius(MagickWand* wand, int radius, size_t width, size_t height);
static void draw_border_stroke(MagickWand* wand, size_t width, size_t height, double thickness, double red, double green, double blue);


int video_mixer_alloc(struct video_mixer_t** video_mixer, int width, int height) {
    int ret;

    *video_mixer = (struct video_mixer_t*)calloc(1, sizeof(struct video_mixer_t));
    if (*video_mixer == NULL) {
        ret = CGS_VIDEO_MIXER_ERROR_NOMEM;
        goto GET_OUT;
    }

    (*video_mixer)->width = width;
    (*video_mixer)->height = height;

    /* Allocate the output frame */
    (*video_mixer)->output_frame = av_frame_alloc();
    if (!(*video_mixer)->output_frame) {
        printf("av_frame_alloc() failed\n");
        ret = CGS_VIDEO_MIXER_ERROR_NOMEM;
        goto GET_OUT;
    }

    (*video_mixer)->output_frame->format = AV_PIX_FMT_RGB24;
    (*video_mixer)->output_frame->width = width;
    (*video_mixer)->output_frame->height = height;
    if ((ret = av_frame_get_buffer((*video_mixer)->output_frame, 0)) < 0) {
        printf("Could not allocate output frame samples (error '%s')\n", av_err2str(ret));
        ret = CGS_VIDEO_MIXER_ERROR_FFMPEG;
        goto GET_OUT;
    }
    video_mixer_start(*video_mixer);

    return CGS_VIDEO_MIXER_ERROR_SUCCESS;

GET_OUT:
    if (*video_mixer) {
        if ((*video_mixer)->output_frame)
            av_frame_free(&(*video_mixer)->output_frame);
        free(*video_mixer);
    }
    return ret;
}

int video_mixer_start(struct video_mixer_t* video_mixer) {

    video_mixer->magic_wand = NewMagickWand();
    PixelWand* background = NewPixelWand();
    // set background for layout debugging
    //PixelSetGreen(background, 1.0);
    //PixelSetColor(background, "white");
    MagickNewImage(video_mixer->magic_wand, video_mixer->width, video_mixer->height, background);
    DestroyPixelWand(background);
    return 0;
}

int video_mixer_add_frame(struct video_mixer_t* video_mixer,
                            AVFrame* input_frame,
                            size_t x_offset,
                            size_t y_offset,
                            struct border_t border,
                            size_t output_width,
                            size_t output_height,
                            enum object_fit_t object_fit) {

/*
    uint8_t* rgb_buf_in = malloc(RGB_BYTES_PER_PIXEL * input_frame->height * input_frame->width);

    // Convert colorspace (AVFrame YUV -> pixelbuf RGB)
    yuv420_rgb24_std(input_frame->width, input_frame->height,
        input_frame->data[0],
        input_frame->data[1],
        input_frame->data[2],
        input_frame->linesize[0],
        input_frame->linesize[1],
        rgb_buf_in,
        input_frame->width * RGB_BYTES_PER_PIXEL,
        YCBCR_709);
*/
    // background on image color for scale/crop debugging
    PixelWand* background = NewPixelWand();
    // change the color to something with an active alpha channel if you need
    // to see what's happening with the image processor
    //PixelSetColor(background, "none");
    //PixelSetColor(background, "red");

    MagickWand* input_wand = NewMagickWand();

    // import pixel buffer (there's gotta be a better way to do this)
    MagickBooleanType status = MagickConstituteImage(input_wand,
        input_frame->width,
        input_frame->height,
        "RGB",
        CharPixel,
        input_frame->data[0]);

    float w_factor = (float)output_width / (float)input_frame->width;
    float h_factor = (float)output_height / (float)input_frame->height;
    float scale_factor = 1;
    float internal_x_offset = 0;
    float internal_y_offset = 0;
    char rescale_dimensions = 0;

    // see https://developer.mozilla.org/en-US/docs/Web/CSS/object-fit
    if (object_fit_fill == object_fit) {
        // don't preserve aspect ratio.
        rescale_dimensions = 0;
    } else if (object_fit_scale_down == object_fit || object_fit_contain == object_fit) {
        // scale to fit all source pixels inside container,
        // preserving aspect ratio
        scale_factor = fmin(w_factor, h_factor);
        rescale_dimensions = 1;
    } else {
        // fill the container completely, preserving aspect ratio
        scale_factor = fmax(w_factor, h_factor);
        rescale_dimensions = 1;
    }

    float scaled_width = output_width;
    float scaled_height = output_height;

    if (rescale_dimensions) {
        scaled_width = input_frame->width * scale_factor;
        scaled_height = input_frame->height * scale_factor;

        internal_y_offset = (scaled_height - output_height) / 2;
        internal_x_offset = (scaled_width - output_width) / 2;
    }

    MagickSetImageBackgroundColor(input_wand, background);
    //MagickResizeImage(input_wand, scaled_width, scaled_height, Lanczos2SharpFilter);
    // TODO: we should use extent without resizing for for object-fill: none.
    if (rescale_dimensions) {
        MagickExtentImage(input_wand, output_width, output_height,
            internal_x_offset, internal_y_offset);
    }

    if (border.radius > 0) {
        draw_border_radius(input_wand, border.radius, output_width, output_height);
    }

    if (border.width > 0) {
        draw_border_stroke(input_wand, output_width, output_height,
            border.width, border.red, border.green, border.blue);
    }

    if (status == MagickFalse) {
        printf("MagickConstituteImage() failed\n");
    }

    // compose source frames
    MagickCompositeImage(video_mixer->magic_wand, input_wand, OverCompositeOp,
        MagickTrue, x_offset, y_offset);
    DestroyMagickWand(input_wand);
    DestroyPixelWand(background);

    //free(rgb_buf_in);
    return 0;
}

int video_mixer_finish(struct video_mixer_t* video_mixer, AVFrame** output_frame) {
    size_t width = MagickGetImageWidth(video_mixer->magic_wand);
    size_t height = MagickGetImageHeight(video_mixer->magic_wand);

    *output_frame = video_mixer->output_frame;

    // push modified wand back to rgb buffer
    MagickExportImagePixels(video_mixer->magic_wand, 0, 0, width, height, "RGB", CharPixel, (*output_frame)->data[0]);
    (*output_frame)->linesize[0] = 3 * width;

    DestroyMagickWand(video_mixer->magic_wand);
    return 0;
}

void video_mixer_free(struct video_mixer_t* video_mixer) {
    av_frame_free(&video_mixer->output_frame);
    free(video_mixer);
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
