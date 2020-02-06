TARGET ?= webrtcdemo
SRC_DIRS ?= ./

SRCS := GoogleWebRTC.cpp WebRTC_Server.cpp Websockets.c
OBJS := $(addsuffix .o,$(basename $(SRCS)))
DEPS := $(OBJS:.o=.d)

INC_DIRS :=  /usr/include /usr/include/glib-2.0/ /usr/lib/glib-2.0/include/ /home/ubuntu/webrtc/webrtc-checkout/src /home/ubuntu/webrtc/webrtc-checkout/src/third_party/abseil-cpp
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

CPPFLAGS ?= $(INC_FLAGS) -MMD -MP -std=c++14 -pthread

LDLIBS = -lwebsockets -lwebrtc -ljansson -lglib-2.0 -lX11

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LOADLIBES) $(LDLIBS) -L/home/ubuntu/webrtc/webrtc-checkout/src/out/release1/obj -lpthread -ldl -lrt -lm -lGL -lstdc++

.PHONY: clean
clean:
	 $(RM) $(TARGET) $(OBJS) $(DEPS)

-include $(DEPS)
