
#include <libwebsockets.h>
#include <glib.h>

#include "Websockets.h"


struct cgs_websockets {
	struct lws_context* plws_context;
	GAsyncQueue* lws_writable_instances_queue;
	cgs_websockets_event_callback callback;
	GThread* pglib_thread;
	int quit;
};

struct cgs_websockets_instance {
	struct cgs_websockets *pcgs_websockets;
	void* user_context;
	struct lws* wsi;
	GAsyncQueue* lws_send_queue;
	struct cgs_websockets_buffer in_buf;
};


int cgs_websockets_lws_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);
gpointer cgs_websockets_lws_service_thread(gpointer threadid);


int cgs_websockets_init(struct cgs_websockets** pcgs_websockets, cgs_websockets_event_callback callback)
{
	int ret;

	*pcgs_websockets = (struct cgs_websockets*)calloc(1, sizeof(struct cgs_websockets));
	if (*pcgs_websockets == NULL) {
		ret = CGS_WEBSOCKETS_ERROR_NOMEM;
		goto GET_OUT;
	}

	static struct lws_context_creation_info info;
	static const struct lws_http_mount mount = {
		/* .mount_next */		NULL,		/* linked-list "next" */
		/* .mountpoint */		"/",		/* mountpoint URL */
		/* .origin */			"./mount-origin/apprtc", /* serve from dir */
		/* .def */			"index.html",	/* default filename */
		/* .protocol */			NULL,
		/* .cgienv */			NULL,
		/* .extra_mimetypes */		NULL,
		/* .interpret */		NULL,
		/* .cgi_timeout */		0,
		/* .cache_max_age */		0,
		/* .auth_mask */		0,
		/* .cache_reusable */		0,
		/* .cache_revalidate */		0,
		/* .cache_intermediaries */	0,
		/* .origin_protocol */		LWSMPRO_FILE,	/* files in a dir */
		/* .mountpoint_len */		1,		/* char count */
		/* .basic_auth_login_file */	NULL,
	};
	static struct lws_protocols protocols[] = {
		{ "http", lws_callback_http_dummy, 0, 0 },
		{ "https", lws_callback_http_dummy, 0, 0 },
		{"lws-minimal", cgs_websockets_lws_callback, sizeof(struct cgs_websockets_instance *)},
		{ NULL, NULL, 0, 0 } /* terminator */
	};
	protocols[2].user = *pcgs_websockets;

	/* Init an lws context */
	memset(&info, 0, sizeof info); /* otherwise uninitialized garbage */
	info.port = 80;
	info.mounts = &mount;
	info.protocols = protocols;
	//info.iface = "localhost";
	info.options = LWS_SERVER_OPTION_DISABLE_IPV6 | LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
	info.ssl_cert_filepath = "demo.cert";
	info.ssl_private_key_filepath = "demo.key";

	(*pcgs_websockets)->plws_context = lws_create_context(&info);
	if (!(*pcgs_websockets)->plws_context) {
		ret = CGS_WEBSOCKETS_ERROR_LWS;
		goto GET_OUT;
	}

	(*pcgs_websockets)->lws_writable_instances_queue = g_async_queue_new();
	if (!(*pcgs_websockets)->lws_writable_instances_queue) {
		ret = CGS_WEBSOCKETS_ERROR_NOMEM;
		goto GET_OUT;
	}

	/* Start threads to service events of the lws context */
	GError* error;
	(*pcgs_websockets)->pglib_thread = g_thread_try_new("cgs_websockets_lws_service_thread", cgs_websockets_lws_service_thread, *pcgs_websockets, &error);
	if ((*pcgs_websockets)->pglib_thread == NULL) {
		ret = CGS_WEBSOCKETS_ERROR_THREAD_CREATE;
		goto GET_OUT;
	}

	(*pcgs_websockets)->callback = callback;
	ret = CGS_WEBSOCKETS_ERROR_SUCCESS;

GET_OUT:
	if (ret) {
		if (*pcgs_websockets) {
			if ((*pcgs_websockets)->plws_context)
				lws_context_destroy((*pcgs_websockets)->plws_context);
			if ((*pcgs_websockets)->lws_writable_instances_queue)
				g_async_queue_unref((*pcgs_websockets)->lws_writable_instances_queue);
			free(*pcgs_websockets);
		}
	}
	return ret;
}

gpointer cgs_websockets_lws_service_thread(gpointer context)
{
	struct cgs_websockets* pcgs_websockets = (struct cgs_websockets*)context;
	while ((lws_service(pcgs_websockets->plws_context, 1000) >= 0) && !pcgs_websockets->quit)
		;

	int retval;
	g_thread_exit(&retval);

	return NULL;
}

int cgs_websockets_send(struct cgs_websockets_instance* pcgs_websockets_instance, const char* out, size_t len)
{
	int ret;
	struct cgs_websockets_buffer *buf;

	buf = (struct cgs_websockets_buffer*)malloc(sizeof(struct cgs_websockets_buffer));
	if (buf == NULL) {
		ret = CGS_WEBSOCKETS_ERROR_NOMEM;
		goto GET_OUT;
	}

	buf->buf = (char*)malloc(len + LWS_PRE + 1);
	if (buf->buf == NULL) {
		free(buf);
		ret = CGS_WEBSOCKETS_ERROR_NOMEM;
		goto GET_OUT;
	}
	buf->len = len + LWS_PRE + 1;

	memcpy(buf->buf + LWS_PRE, out, len);
	buf->buf[LWS_PRE + len] = 0;

	g_async_queue_push(pcgs_websockets_instance->lws_send_queue, buf);
	g_async_queue_push(pcgs_websockets_instance->pcgs_websockets->lws_writable_instances_queue, pcgs_websockets_instance);
	lws_cancel_service(pcgs_websockets_instance->pcgs_websockets->plws_context);

	ret = CGS_WEBSOCKETS_ERROR_SUCCESS;

GET_OUT:
	return ret;
}


int cgs_websockets_set_user_context(struct cgs_websockets_instance* pcgs_websockets_instance, void* user_context) {
	pcgs_websockets_instance->user_context = user_context;
	return CGS_WEBSOCKETS_ERROR_SUCCESS;
}

int cgs_websockets_set_system_context(struct cgs_websockets_instance* pcgs_websockets_instance, struct cgs_websockets* pcgs_websockets) {
	pcgs_websockets_instance->pcgs_websockets = pcgs_websockets;
	return CGS_WEBSOCKETS_ERROR_SUCCESS;
}

void lws_send_queue_item_free(gpointer data) {

	if (data)
		free(data);
}

int websockets_instance_create(struct cgs_websockets* pcgs_websockets, struct cgs_websockets_instance** pwebsockets_instance, struct lws* wsi) {
	int ret;

	*pwebsockets_instance = (struct cgs_websockets_instance*)malloc(sizeof(struct cgs_websockets_instance));
	if (*pwebsockets_instance == NULL) {
		ret = CGS_WEBSOCKETS_ERROR_NOMEM;
		goto GET_OUT;
	}

	(*pwebsockets_instance)->lws_send_queue = g_async_queue_new_full(lws_send_queue_item_free);
	if (!(*pwebsockets_instance)->lws_send_queue) {
		free(*pwebsockets_instance);
		ret = CGS_WEBSOCKETS_ERROR_NOMEM;
		goto GET_OUT;
	}

	(*pwebsockets_instance)->wsi = wsi;
	(*pwebsockets_instance)->pcgs_websockets = pcgs_websockets;
	(*pwebsockets_instance)->in_buf.buf = NULL;
	(*pwebsockets_instance)->in_buf.len = 0;
	ret = CGS_WEBSOCKETS_ERROR_SUCCESS;

GET_OUT:
	return ret;
}

int cgs_websockets_lws_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len)
{
	struct cgs_websockets_instance**ppwebsockets_instance = (struct cgs_websockets_instance**)user;
	struct cgs_websockets_instance* pwebsockets_instance;
	struct cgs_websocket_event event;

	switch (reason) {
	case LWS_CALLBACK_PROTOCOL_INIT:
		return 0;
		break;

	case LWS_CALLBACK_PROTOCOL_DESTROY:
		return 0;
		break;

	case LWS_CALLBACK_ESTABLISHED:
		if (CGS_WEBSOCKETS_ERROR_SUCCESS != websockets_instance_create((struct cgs_websockets*)lws_get_protocol(wsi)->user, ppwebsockets_instance, wsi))
			return 1;
		pwebsockets_instance = *ppwebsockets_instance;
		event.code = CGS_WEBSOCKET_EVENT_CONNECTED;
		break;

	case LWS_CALLBACK_CLOSED:
		printf("\n\nLWS_CALLBACK_CLOSED\n\n");
		pwebsockets_instance = *ppwebsockets_instance;
		event.code = CGS_WEBSOCKET_EVENT_DISCONNECTED;
		break;

	case LWS_CALLBACK_RECEIVE:
		pwebsockets_instance = *ppwebsockets_instance;
		if (lws_is_first_fragment(wsi)) {
			pwebsockets_instance->in_buf.buf = (char*)malloc(len + 1);
			if (!pwebsockets_instance->in_buf.buf) {
				return 1;
			}
			memcpy(pwebsockets_instance->in_buf.buf, in, len);
			if (lws_is_final_fragment(wsi)) {
				pwebsockets_instance->in_buf.buf[len] = 0;
				event.in = pwebsockets_instance->in_buf.buf;
				pwebsockets_instance->in_buf.buf = NULL;
				pwebsockets_instance->in_buf.len = 0;
				event.code = CGS_WEBSOCKET_EVENT_RECEIVED;
			}
			else {
				pwebsockets_instance->in_buf.len = len;
				return 0;
			}
		}
		else {
			char *buf = (char*)realloc(pwebsockets_instance->in_buf.buf, pwebsockets_instance->in_buf.len + len + 1);
			if (!buf) {
				free(pwebsockets_instance->in_buf.buf);
				return 1;
			}
			memcpy(buf + pwebsockets_instance->in_buf.len, in, len);
			if (lws_is_final_fragment(wsi)) {
				buf[pwebsockets_instance->in_buf.len + len] = 0;
				event.in = buf;
				pwebsockets_instance->in_buf.buf = NULL;
				pwebsockets_instance->in_buf.len = 0;
				event.code = CGS_WEBSOCKET_EVENT_RECEIVED;
			}
			else {
				pwebsockets_instance->in_buf.buf = buf;
				pwebsockets_instance->in_buf.len += len;
				return 0;
			}
		}
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE: {
		pwebsockets_instance = *ppwebsockets_instance;
		if (g_async_queue_length(pwebsockets_instance->lws_send_queue)) {
			struct cgs_websockets_buffer* buf = (struct cgs_websockets_buffer*)g_async_queue_pop(pwebsockets_instance->lws_send_queue);
			if (lws_write(wsi, LWS_PRE + (unsigned char*)buf->buf, buf->len - LWS_PRE - 1, LWS_WRITE_TEXT) >= 0) {
				event.code = CGS_WEBSOCKET_EVENT_SENT;
				event.in = buf;
			}
			else {
				event.code = CGS_WEBSOCKET_EVENT_SEND_ERROR;
				event.in = buf;
			}

			if (g_async_queue_length(pwebsockets_instance->lws_send_queue))
				lws_callback_on_writable(wsi);
		}
		else {
			return 0;
		}
		break;
	}

	case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
		printf("\n\nLWS_CALLBACK_EVENT_WAIT_CANCELLED %p\n\n", wsi);
		struct cgs_websockets* pwebsockets = (struct cgs_websockets*)lws_get_protocol(wsi)->user;
		while(1) {
			pwebsockets_instance = (struct cgs_websockets_instance*)g_async_queue_try_pop(pwebsockets->lws_writable_instances_queue);
			if (pwebsockets_instance)
				lws_callback_on_writable(pwebsockets_instance->wsi);
			else
				break;
		}
		return 0;
		break;
	}

	default:
		return 0;
		break;
	}

	return pwebsockets_instance->pcgs_websockets->callback(pwebsockets_instance->pcgs_websockets, pwebsockets_instance, &event, pwebsockets_instance ? pwebsockets_instance->user_context : NULL);
}

int cgs_websockets_instance_destroy(struct cgs_websockets_instance* pcgs_websockets_instance) {

	g_async_queue_unref(pcgs_websockets_instance->lws_send_queue);
	g_async_queue_remove(pcgs_websockets_instance->pcgs_websockets->lws_writable_instances_queue, pcgs_websockets_instance);
	free(pcgs_websockets_instance);
	return 0;
}

int cgs_websockets_deinit(struct cgs_websockets* pcgs_websockets) {

	pcgs_websockets->quit = 1;
	lws_cancel_service(pcgs_websockets->plws_context);

	g_thread_join(pcgs_websockets->pglib_thread);

	free(pcgs_websockets);
	return 0;
}
