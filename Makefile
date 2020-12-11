TARGET ?= webrtcdemo
SRC_DIRS ?= ./

SRCS := GoogleWebRTC.cpp WebRTC_Server.cpp Websockets.c media_file_writer.c fake_audio_capture_module.cc audio_mixer.c video_mixer.c
OBJS := $(addsuffix .o,$(basename $(SRCS)))
DEPS := $(OBJS:.o=.d)

INC_DIRS :=  /usr/include /usr/include/glib-2.0/ /usr/lib/glib-2.0/include/ /home/ubuntu/webrtc/webrtc-checkout/src /home/ubuntu/webrtc/webrtc-checkout/src/third_party/abseil-cpp /home/ubuntu/webrtc/ffmpeg/ffmpeg-4.3/ /usr/local/include/ImageMagick-7/
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

CPPFLAGS ?= $(INC_FLAGS) -fPIC -MMD -MP -pthread -DMAGICKCORE_QUANTUM_DEPTH=16
CXXFLAGS := -std=c++14

LDLIBS = -lwebsockets -lwebrtc -ljansson -lglib-2.0 -lMagickWand-7.Q16HDRI -lMagickCore-7.Q16HDRI -lX11 -Wl,--start-group -lavdevice -lavfilter -lavutil -lavformat -lavcodec -lswresample -lswscale -lz -lx264 -Wl,--end-group -llzma -lbz2 -lrt -lpostproc

$(TARGET): $(OBJS)
        $(CC)  $(LDFLAGS) $(OBJS) -o $@ $(LOADLIBES) $(LDLIBS) $(CXXFLAGS) -L/home/ubuntu/webrtc/webrtc-checkout/src/out/release1/obj  -L/home/ubuntu/ffmpeg_build/lib -lpthread -ldl -lrt -lm -lGL -lstdc++ -lavfilter -pthread -lm -lass -lm -lharfbuzz -lm -lglib-2.0 -pthread -lpcre -pthread -lgraphite2 -lfontconfig -lexpat -lfreetype -lexpat -lfribidi -lfreetype -lpng16 -lm -lz -lm -lz -lfreetype -lpng16 -lm -lz -lm -lz -lswscale -lm -lpostproc -lm -lavformat -lm -lbz2 -lz -lavcodec -lvpx -lm -lpthread -lvpx -lm -lpthread -lvpx -lm -lpthread -lvpx -lm -lpthread -pthread -lm -llzma -lz -lfdk-aac -lm -lmp3lame -lm -lopus -lm -ltheoraenc -ltheoradec -logg -lvorbis -lm -logg -lvorbisenc -lvorbis -lm -logg -lx264 -lpthread -lm -ldl -lswresample -lm -lavutil -pthread -lm

.PHONY: clean
clean:
         $(RM) $(TARGET) $(OBJS) $(DEPS)

-include $(DEPS)
