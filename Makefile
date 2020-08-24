TARGET ?= webrtcdemo
SRC_DIRS ?= ./

SRCS := GoogleWebRTC.cpp WebRTC_Server.cpp Websockets.c media_file_writer.c fake_audio_capture_module.cc
OBJS := $(addsuffix .o,$(basename $(SRCS)))
DEPS := $(OBJS:.o=.d)

INC_DIRS :=  /usr/include /usr/include/glib-2.0/ /usr/lib/glib-2.0/include/ /home/ubuntu/webrtc/webrtc-checkout/src /home/ubuntu/webrtc/webrtc-checkout/src/third_party/abseil-cpp
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

CPPFLAGS ?= $(INC_FLAGS) -fPIC -MMD -MP -pthread -frtti
CXXFLAGS := -std=c++14

LDLIBS = -lwebsockets -lwebrtc -ljansson -lglib-2.0 -lX11

$(TARGET): $(OBJS)
	$(CC)  $(LDFLAGS) $(OBJS) -o $@ $(LOADLIBES) $(LDLIBS) $(CXXFLAGS) -L/home/ubuntu/webrtc/webrtc-checkout/src/out/release1/obj -lpthread -ldl -lrt -lm -lGL -lstdc++

.PHONY: clean
clean:
	 $(RM) $(TARGET) $(OBJS) $(DEPS)

-include $(DEPS)
