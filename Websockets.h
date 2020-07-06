
#ifndef __CGS_WEBSOCKETS__
#define __CGS_WEBSOCKETS__

#ifdef __cplusplus
extern "C" {
#endif

struct cgs_websockets;
struct cgs_websockets_instance;

struct cgs_websockets_buffer {
	char* buf;
	size_t len;
	int flag;
};

enum cgs_websocket_event_code {
	CGS_WEBSOCKET_EVENT_UNKNOWN,
	CGS_WEBSOCKET_EVENT_CONNECTED,
	CGS_WEBSOCKET_EVENT_RECEIVED,
	CGS_WEBSOCKET_EVENT_DISCONNECTED,
	CGS_WEBSOCKET_EVENT_SENT,
	CGS_WEBSOCKET_EVENT_SEND_ERROR
};

struct cgs_websocket_event {
	enum cgs_websocket_event_code code;
	void* in;
};

enum cgs_websocket_error {
	CGS_WEBSOCKETS_ERROR_SUCCESS,
	CGS_WEBSOCKETS_ERROR_NOMEM,
	CGS_WEBSOCKETS_ERROR_LWS,
	CGS_WEBSOCKETS_ERROR_BAD_ARG,
	CGS_WEBSOCKETS_ERROR_THREAD_CREATE
};



typedef int (*cgs_websockets_event_callback)(struct cgs_websockets* pcgs_websockets, struct cgs_websockets_instance* pcgs_websockets_instance, struct cgs_websocket_event* pevent, void* user_context);

int cgs_websockets_init(struct cgs_websockets** pcgs_websockets, cgs_websockets_event_callback callback);
int cgs_websockets_set_user_context(struct cgs_websockets_instance* pcgs_websockets_instance, void* user_context);
int cgs_websockets_send(struct cgs_websockets_instance* pcgs_websockets_instance, const char* out, size_t len);
int cgs_websockets_instance_destroy(struct cgs_websockets_instance* pcgs_websockets_instance);
int cgs_websockets_deinit(struct cgs_websockets* pcgs_websockets);

#ifdef __cplusplus
}
#endif
#endif
