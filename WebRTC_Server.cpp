// WebRTC_Server.cpp : This file contains the 'main' function. Program execution begins and ends there.
//


#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "CGS_WebRTC_Server"
#define CGS_LOG g_log

#include <iostream>
#include <string.h>
#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <jansson.h>



#include "Websockets.h"
#include "GoogleWebRTC.h"

# pragma comment(lib, "secur32.lib")
# pragma comment(lib, "winmm.lib")
# pragma comment(lib, "dmoguids.lib")
# pragma comment(lib, "wmcodecdspuuid.lib")
# pragma comment(lib, "msdmo.lib")
# pragma comment(lib, "Strmiids.lib")



enum cgs_tasklet_state {
	CGS_TASKLET_STATE_ZERO,
	CGS_TASKLET_STATE_INITIALISED,
	CGS_TASKLET_STATE_DEINITIALISED
};

enum cgs_event {
	CGS_EVENT_UNKNOWN,
	CGS_EVENT_WEBSOCKET_CONNECTED,
	CGS_EVENT_WEBSOCKET_DISCONNECTED,
	CGS_EVENT_WEBSOCKET_RECEIVED,
	CGS_EVENT_WEBSOCKET_SENT,
	CGS_EVENT_WEBSOCKET_SEND_ERROR,
	CGS_EVENT_WEBRTC_SIGNALING_CHANGE,
	CGS_EVENT_WEBRTC_ADD_STREAM,
	CGS_EVENT_WEBRTC_REMOVE_STREAM,
	CGS_EVENT_WEBRTC_DATA_CHANNEL,
	CGS_EVENT_WEBRTC_RENEGOTIATION_NEEDED,
	CGS_EVENT_WEBRTC_ICE_CONNECTION_CHANGE,
	CGS_EVENT_WEBRTC_PEER_CONNECTION_CHANGE,
	CGS_EVENT_WEBRTC_ICE_GATHERING_CHANGE,
	CGS_EVENT_WEBRTC_ICE_CANDIDATE,
	CGS_EVENT_WEBRTC_ICE_CANDIDATE_ERROR,
	CGS_EVENT_WEBRTC_ICE_CANDIDATES_REMOVED,
	CGS_EVENT_WEBRTC_ICE_CONNECTION_RECEIVING_CHANGE,
	CGS_EVENT_WEBRTC_ICE_SELECTED_CANDIDATE_PAIR_CHANGED,
	CGS_EVENT_WEBRTC_TRACK,
	CGS_EVENT_WEBRTC_REMOVE_TRACK,
	CGS_EVENT_WEBRTC_SET_REMOTE_SESSION_DESCRIPTION_DONE,
	CGS_EVENT_WEBRTC_SET_REMOTE_SESSION_DESCRIPTION_FAILED,
	CGS_EVENT_WEBRTC_SET_LOCAL_SESSION_DESCRIPTION_DONE,
	CGS_EVENT_WEBRTC_SET_LOCAL_SESSION_DESCRIPTION_FAILED,
	CGS_EVENT_WEBRTC_SET_SESSION_DESCRIPTION_FAILED,
	CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_OFFER_DONE,
	CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_OFFER_FAILED,
	CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_ANSWER_DONE,
	CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_ANSWER_FAILED,
};

struct cgs_tasklet_info {
	cgs_tasklet_state state;
	cgs_webrtc_instance *webrtc_instance;
	cgs_websockets_instance* websocket_instance;
	bool remote_description_set;
	bool local_description_set;
	GAsyncQueue* ice_candidate_queue;
};

struct cgs_task_info {
	bool exit;
	struct lws_context* context;
	GAsyncQueue* main_loop_queue;
	GHashTable* main_context_hash_table;
	struct cgs_websockets* pcgs_websockets;
	struct cgs_webrtc* pcgs_webrtc;
	struct cgs_webrtc_conference* pcgs_webrtc_conference;
};


struct cgs_queue_item {
	cgs_event event;
	char* context;
	void* in;
};


cgs_task_info		g_task_info;

int cgs_system_initialise(void);
int cgs_system_deinitialise(void);
int cgs_read_config_file(void);

const char* cgs_get_event_str(cgs_event event){
	switch(event) {
	case CGS_EVENT_WEBSOCKET_CONNECTED:
		return "CGS_EVENT_WEBSOCKET_CONNECTED";
	case CGS_EVENT_WEBSOCKET_DISCONNECTED:
		return "CGS_EVENT_WEBSOCKET_DISCONNECTED";
	case CGS_EVENT_WEBSOCKET_RECEIVED:
		return "CGS_EVENT_WEBSOCKET_RECEIVED";
	case CGS_EVENT_WEBSOCKET_SENT:
		return "CGS_EVENT_WEBSOCKET_SENT";
	case CGS_EVENT_WEBSOCKET_SEND_ERROR:
		return "CGS_EVENT_WEBSOCKET_SEND_ERROR";
	case CGS_EVENT_WEBRTC_SIGNALING_CHANGE:
		return "CGS_EVENT_WEBRTC_SIGNALING_CHANGE";
	case CGS_EVENT_WEBRTC_ADD_STREAM:
		return "CGS_EVENT_WEBRTC_ADD_STREAM";
	case CGS_EVENT_WEBRTC_REMOVE_STREAM:
		return "CGS_EVENT_WEBRTC_REMOVE_STREAM";
	case CGS_EVENT_WEBRTC_DATA_CHANNEL:
		return "CGS_EVENT_WEBRTC_DATA_CHANNEL";
	case CGS_EVENT_WEBRTC_RENEGOTIATION_NEEDED:
		return "CGS_EVENT_WEBRTC_RENEGOTIATION_NEEDED";
	case CGS_EVENT_WEBRTC_ICE_CONNECTION_CHANGE:
		return "CGS_EVENT_WEBRTC_ICE_CONNECTION_CHANGE";
	case CGS_EVENT_WEBRTC_PEER_CONNECTION_CHANGE:
		return "CGS_EVENT_WEBRTC_PEER_CONNECTION_CHANGE";
	case CGS_EVENT_WEBRTC_ICE_GATHERING_CHANGE:
		return "CGS_EVENT_WEBRTC_ICE_GATHERING_CHANGE";
	case CGS_EVENT_WEBRTC_ICE_CANDIDATE:
		return "CGS_EVENT_WEBRTC_ICE_CANDIDATE";
	case CGS_EVENT_WEBRTC_ICE_CANDIDATE_ERROR:
		return "CGS_EVENT_WEBRTC_ICE_CANDIDATE_ERROR";
	case CGS_EVENT_WEBRTC_ICE_CANDIDATES_REMOVED:
		return "CGS_EVENT_WEBRTC_ICE_CANDIDATES_REMOVED";
	case CGS_EVENT_WEBRTC_ICE_CONNECTION_RECEIVING_CHANGE:
		return "CGS_EVENT_WEBRTC_ICE_CONNECTION_RECEIVING_CHANGE";
	case CGS_EVENT_WEBRTC_ICE_SELECTED_CANDIDATE_PAIR_CHANGED:
		return "CGS_EVENT_WEBRTC_ICE_SELECTED_CANDIDATE_PAIR_CHANGED";
	case CGS_EVENT_WEBRTC_TRACK:
		return "CGS_EVENT_WEBRTC_TRACK";
	case CGS_EVENT_WEBRTC_REMOVE_TRACK:
		return "CGS_EVENT_WEBRTC_REMOVE_TRACK";
	case CGS_EVENT_WEBRTC_SET_REMOTE_SESSION_DESCRIPTION_DONE:
		return "CGS_EVENT_WEBRTC_SET_REMOTE_SESSION_DESCRIPTION_DONE";
	case CGS_EVENT_WEBRTC_SET_REMOTE_SESSION_DESCRIPTION_FAILED:
		return "CGS_EVENT_WEBRTC_SET_REMOTE_SESSION_DESCRIPTION_FAILED";
	case CGS_EVENT_WEBRTC_SET_LOCAL_SESSION_DESCRIPTION_DONE:
		return "CGS_EVENT_WEBRTC_SET_LOCAL_SESSION_DESCRIPTION_DONE";
	case CGS_EVENT_WEBRTC_SET_LOCAL_SESSION_DESCRIPTION_FAILED:
		return "CGS_EVENT_WEBRTC_SET_LOCAL_SESSION_DESCRIPTION_FAILED";
	case CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_OFFER_DONE:
		return "CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_OFFER_DONE";
	case CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_OFFER_FAILED:
		return "CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_OFFER_FAILED";
	case CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_ANSWER_DONE:
		return "CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_ANSWER_DONE";
	case CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_ANSWER_FAILED:
		return "CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_ANSWER_FAILED";
	case CGS_EVENT_UNKNOWN:
	default:
		return "CGS_EVENT_UNKNOWN";

	}
}

int cgs_websockets_callback(cgs_websockets* pcgs_websockets, cgs_websockets_instance* pcgs_websockets_instance, cgs_websocket_event* pevent, void* user_context);
int cgs_webrtc_callback(cgs_webrtc* pcgs_webrtc, cgs_webrtc_instance* pcgs_webrtc_instance, cgs_webrtc_event* pevent, void* user_context);


#ifdef _WIN32
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType)
	{
		// Handle the CTRL-C signal. 
	case CTRL_C_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		g_task_info.exit = true;
		return TRUE;

		// Pass other signals to the next handler. 
	case CTRL_BREAK_EVENT:
		return FALSE;

	case CTRL_LOGOFF_EVENT:
		return FALSE;

	default:
		return FALSE;
	}
}
#endif



void ice_candidate_queue_item_free(gpointer data) {
	if (data)
		json_decref((json_t*)data);
}


int main()
{
#ifdef _WIN32
	SetConsoleCtrlHandler(CtrlHandler, TRUE);
#endif
	printf("Starting...");
	
	cgs_system_initialise();

	/* Event loop here */
	while (!g_task_info.exit) {
		cgs_queue_item *event = (cgs_queue_item *)g_async_queue_timeout_pop(g_task_info.main_loop_queue, 1000000);
		if (event) {
			cgs_tasklet_info* pcgs_tasklet_info = (cgs_tasklet_info*)g_hash_table_lookup(g_task_info.main_context_hash_table, event->context);
			if (pcgs_tasklet_info) {
				printf("\n\n[%s] Got event %s\n\n", event->context, cgs_get_event_str(event->event));
				switch (event->event) {
				case CGS_EVENT_WEBSOCKET_CONNECTED: {
					//cgs_websockets_send(pcgs_tasklet_info->websocket_instance, (char *)calloc(4096,1), 4096);
					cgs_webrtc_create_instance(g_task_info.pcgs_webrtc, &pcgs_tasklet_info->webrtc_instance, event->context);
					pcgs_tasklet_info->remote_description_set = false;
					pcgs_tasklet_info->local_description_set = false;
					pcgs_tasklet_info->ice_candidate_queue = g_async_queue_new_full(ice_candidate_queue_item_free);
					break;
				}
				case CGS_EVENT_WEBSOCKET_RECEIVED: {
					json_t* root;
					json_error_t error;

					printf("\n\n\nReceived [%s]\n\n\n", (char*)event->in);
					root = json_loads((char*)event->in, 0, &error);
					if (!root) {
						printf("Error parsing incoming message, line %d: %s\n", error.line, error.text);
						break;
					}
					free(event->in); //Must not be freed using delete
					json_t* type = json_object_get(root, "type");					
					if (type) {
						const char* typestr = json_string_value(type);
						if (0 == strcmp(typestr, "offer")) {
							json_t* sdp = json_object_get(root, "sdp");
							cgs_webrtc_set_remote_description(pcgs_tasklet_info->webrtc_instance, json_string_value(sdp), true);
						}
						else if (0 == strcmp(typestr, "answer")) {
							json_t* sdp = json_object_get(root, "sdp");
							cgs_webrtc_set_remote_description(pcgs_tasklet_info->webrtc_instance, json_string_value(sdp), false);
						}
						else if (0 == strcmp(typestr, "removetrack")) {
							cgs_webrtc_on_remove_track(pcgs_tasklet_info->webrtc_instance, json_string_value(json_object_get(root, "trackid")));
						}
						else if (0 == strcmp(typestr, "candidate")) {
							json_t* candidate = json_object_get(root, "candidate");
							if (!candidate)
								printf("Wrong incoming message [candidate]\n");
							else {
								if (pcgs_tasklet_info->local_description_set && pcgs_tasklet_info->remote_description_set) {
									cgs_webrtc_add_ice_candiate(pcgs_tasklet_info->webrtc_instance,
										json_string_value(json_object_get(root, "sdpMid")),
										json_integer_value(json_object_get(root, "sdpMLineIndex")),
										json_string_value(candidate));
								}
								else {
									g_async_queue_push(pcgs_tasklet_info->ice_candidate_queue, json_incref(root));
								}
							}
						}
						else {
							printf("Unknown incoming message [%s]\n", typestr);
						}
					}
					else {
						printf("Unknown incoming message\n");
					}
					json_decref(root);
					break;
				}

				case CGS_EVENT_WEBSOCKET_DISCONNECTED:
					cgs_webrtc_remove_from_conference(pcgs_tasklet_info->webrtc_instance, g_task_info.pcgs_webrtc_conference);
					cgs_webrtc_destroy_instance(pcgs_tasklet_info->webrtc_instance);
					cgs_websockets_instance_destroy(pcgs_tasklet_info->websocket_instance);
					g_async_queue_unref(pcgs_tasklet_info->ice_candidate_queue);
					g_hash_table_remove(g_task_info.main_context_hash_table, event->context);
					delete pcgs_tasklet_info;
					free(event->context);
					break;
				case CGS_EVENT_WEBSOCKET_SENT:
				case CGS_EVENT_WEBSOCKET_SEND_ERROR:
					free(((struct cgs_websockets_buffer*)(event->in))->buf);//Not to be deleted using delete
					free(event->in);//Not to be deleted using delete
					break;
				case CGS_EVENT_WEBRTC_RENEGOTIATION_NEEDED:
					pcgs_tasklet_info->remote_description_set = pcgs_tasklet_info->local_description_set = false;
					cgs_webrtc_create_offer(pcgs_tasklet_info->webrtc_instance);
					break;
				case CGS_EVENT_WEBRTC_SET_REMOTE_SESSION_DESCRIPTION_DONE:
					pcgs_tasklet_info->remote_description_set = true;
					if (!pcgs_tasklet_info->local_description_set) {
						cgs_webrtc_add_to_conference(pcgs_tasklet_info->webrtc_instance, g_task_info.pcgs_webrtc_conference);
						cgs_webrtc_create_answer(pcgs_tasklet_info->webrtc_instance);
					}
					break;
				case CGS_EVENT_WEBRTC_SET_REMOTE_SESSION_DESCRIPTION_FAILED:
					break;
				case CGS_EVENT_WEBRTC_SET_LOCAL_SESSION_DESCRIPTION_DONE: {
					json_t* ice_candidate;
					pcgs_tasklet_info->local_description_set = true;
					if (pcgs_tasklet_info->local_description_set && pcgs_tasklet_info->remote_description_set) {
						cgs_webrtc_add_to_conference(pcgs_tasklet_info->webrtc_instance, g_task_info.pcgs_webrtc_conference);
						while (ice_candidate = (json_t*)g_async_queue_try_pop(pcgs_tasklet_info->ice_candidate_queue)) {
							cgs_webrtc_add_ice_candiate(pcgs_tasklet_info->webrtc_instance,
								json_string_value(json_object_get(ice_candidate, "sdpMid")),
								json_integer_value(json_object_get(ice_candidate, "sdpMLineIndex")),
								json_string_value(json_object_get(ice_candidate, "candidate")));
						}
					}
					break;
				}
				case CGS_EVENT_WEBRTC_SET_LOCAL_SESSION_DESCRIPTION_FAILED:
					break;
				case CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_OFFER_DONE: {
					cgs_websockets_send(pcgs_tasklet_info->websocket_instance, (char*)event->in, strlen((char*)event->in));
					break;
				}
				case CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_OFFER_FAILED:
					printf("\n\n[%s] Event message [%s]\n\n", event->context, (char*)event->in);
					g_free(event->in);
					break;
				case CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_ANSWER_DONE: {
					cgs_websockets_send(pcgs_tasklet_info->websocket_instance, (char*)event->in, strlen((char*)event->in));
					break;
				}
				case CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_ANSWER_FAILED:
					printf("\n\n[%s] Event message [%s]\n\n", event->context, (char *)event->in);
					g_free(event->in);
					break;
				case CGS_EVENT_WEBRTC_ICE_CANDIDATE:
					cgs_websockets_send(pcgs_tasklet_info->websocket_instance, (char*)event->in, strlen((char*)event->in));
					break;
				case CGS_EVENT_WEBRTC_ICE_GATHERING_CHANGE:
					if (event->in) {
						cgs_websockets_send(pcgs_tasklet_info->websocket_instance, (char*)event->in, strlen((char*)event->in));
					}
					break;
				case CGS_EVENT_WEBRTC_TRACK:
					if (event->in) {
						cgs_webrtc_on_add_track(pcgs_tasklet_info->webrtc_instance, (char*)event->in);
						g_free(event->in);
					}
					break;
				case CGS_EVENT_WEBRTC_REMOVE_TRACK:
					if (event->in) {
						json_t* json_candidate = json_pack("{ssss}", "type", "removetrack", "streamid", (char*)event->in);
						char* json = json_dumps(json_candidate, JSON_COMPACT);
						cgs_websockets_send(pcgs_tasklet_info->websocket_instance, json, strlen(json));
						json_decref(json_candidate);
						g_free(event->in);
					}
					break;
				default:
					break;
				}
			}
			else {
				printf("\n\nStray event %s\n\n", cgs_get_event_str(event->event));
			}
			delete event;
		}
	}

	cgs_system_deinitialise();
	printf("Exiting...");
}

void main_loop_queue_item_free(gpointer data) {

	if (data)
		delete data;
}

int cgs_system_initialise(void) {
	cgs_read_config_file();

	g_task_info.main_loop_queue = g_async_queue_new_full(main_loop_queue_item_free);
	if (!g_task_info.main_loop_queue)
	{
		return 1;
	}

	g_task_info.main_context_hash_table = g_hash_table_new(NULL, g_str_equal);
	if (!g_task_info.main_context_hash_table)
	{
		g_async_queue_unref(g_task_info.main_loop_queue);
		return 1;
	}

	cgs_websockets_init(&g_task_info.pcgs_websockets, cgs_websockets_callback);

	cgs_webrtc_init(&g_task_info.pcgs_webrtc, cgs_webrtc_callback);

	cgs_webrtc_create_conference(g_task_info.pcgs_webrtc, &g_task_info.pcgs_webrtc_conference, NULL, NULL);

	return 0;
}

char* cgs_get_next_crn()
{
	static int crn;
	size_t count = 0;
	int n = INT_MAX;

	/* Count the number of digits in the max integer */
	while (n != 0) {
		n = n / 10;
		++count;
	}

	char* str_crn = (char *)calloc(count + 1, sizeof(char));
	if(str_crn) {
		sprintf(str_crn, "%d", ++crn);
	}
	return str_crn;
}

int cgs_websockets_callback(cgs_websockets* pcgs_websockets, cgs_websockets_instance* pcgs_websockets_instance, cgs_websocket_event* pevent, void* user_context) {
	cgs_queue_item* event = new cgs_queue_item;
	if (!event)
		return 1;
	event->context = NULL;

	switch (pevent->code) {
	case CGS_WEBSOCKET_EVENT_CONNECTED: {
		event->event = CGS_EVENT_WEBSOCKET_CONNECTED;
		cgs_tasklet_info *pcgs_tasklet_info = new cgs_tasklet_info;
		if (!pcgs_tasklet_info) {
			delete event;
			return 1;
		}
		
		do {
			if (event->context) free(event->context);

			event->context = cgs_get_next_crn();
			if (!event->context) {
				delete pcgs_tasklet_info;
				delete event;
				return 1;
			}
		} while (g_hash_table_contains(g_task_info.main_context_hash_table, event->context));

		if (!g_hash_table_insert(g_task_info.main_context_hash_table, event->context, pcgs_tasklet_info)) {
			free(event->context);
			delete pcgs_tasklet_info;
			delete event;
			return 1;
		}
		cgs_websockets_set_user_context(pcgs_websockets_instance, event->context);
		pcgs_tasklet_info->state = CGS_TASKLET_STATE_ZERO;
		pcgs_tasklet_info->websocket_instance = pcgs_websockets_instance;
	}
		break;
	case CGS_WEBSOCKET_EVENT_RECEIVED:
		event->event = CGS_EVENT_WEBSOCKET_RECEIVED;
		event->context = (char*)user_context;
		event->in = pevent->in;
		break;
	case CGS_WEBSOCKET_EVENT_DISCONNECTED:
		event->event = CGS_EVENT_WEBSOCKET_DISCONNECTED;
		event->context = (char*)user_context;
		break;
	case CGS_WEBSOCKET_EVENT_SENT:
		event->in = pevent->in;
		event->event = CGS_EVENT_WEBSOCKET_SENT;
		event->context = (char*)user_context;
		break;
	case CGS_WEBSOCKET_EVENT_SEND_ERROR:
		event->in = pevent->in;
		event->event = CGS_EVENT_WEBSOCKET_SEND_ERROR;
		event->context = (char*)user_context;
		break;
	default:
		break;
	}

	g_async_queue_push(g_task_info.main_loop_queue, event);
	return 0;
}

int cgs_webrtc_callback(cgs_webrtc* pcgs_webrtc, cgs_webrtc_instance* pcgs_webrtc_instance, cgs_webrtc_event* pevent, void* user_context) {
	cgs_queue_item* event = new cgs_queue_item;
	if (!event)
		return 1;
	event->context = (char*)user_context;
	switch (pevent->code) {
	case CGS_WEBRTC_EVENT_SIGNALING_CHANGE:
		event->event = CGS_EVENT_WEBRTC_SIGNALING_CHANGE;
		break;
	case CGS_WEBRTC_EVENT_ADD_STREAM:
		event->event = CGS_EVENT_WEBRTC_ADD_STREAM;
		break;
	case CGS_WEBRTC_EVENT_REMOVE_STREAM:
		event->event = CGS_EVENT_WEBRTC_REMOVE_STREAM;
		break;
	case CGS_WEBRTC_EVENT_DATA_CHANNEL:
		event->event = CGS_EVENT_WEBRTC_DATA_CHANNEL;
		break;
	case CGS_WEBRTC_EVENT_RENEGOTIATION_NEEDED:
		event->event = CGS_EVENT_WEBRTC_RENEGOTIATION_NEEDED;
		break;
	case CGS_WEBRTC_EVENT_ICE_CONNECTION_CHANGE:
		event->event = CGS_EVENT_WEBRTC_ICE_CONNECTION_CHANGE;
		break;
	case CGS_WEBRTC_EVENT_PEER_CONNECTION_CHANGE:
		event->event = CGS_EVENT_WEBRTC_PEER_CONNECTION_CHANGE;
		break;
	case CGS_WEBRTC_EVENT_ICE_GATHERING_CHANGE:
		event->event = CGS_EVENT_WEBRTC_ICE_GATHERING_CHANGE;
		event->in = pevent->in;
		break;
	case CGS_WEBRTC_EVENT_ICE_CANDIDATE:
		event->event = CGS_EVENT_WEBRTC_ICE_CANDIDATE;
		event->in = pevent->in;
		break;
	case CGS_WEBRTC_EVENT_ICE_CANDIDATE_ERROR:
		event->event = CGS_EVENT_WEBRTC_ICE_CANDIDATE_ERROR;
		break;
	case CGS_WEBRTC_EVENT_ICE_CANDIDATES_REMOVED:
		event->event = CGS_EVENT_WEBRTC_ICE_CANDIDATES_REMOVED;
		break;
	case CGS_WEBRTC_EVENT_ICE_CONNECTION_RECEIVING_CHANGE:
		event->event = CGS_EVENT_WEBRTC_ICE_CONNECTION_RECEIVING_CHANGE;
		break;
	case CGS_WEBRTC_EVENT_ICE_SELECTED_CANDIDATE_PAIR_CHANGED:
		event->event = CGS_EVENT_WEBRTC_ICE_SELECTED_CANDIDATE_PAIR_CHANGED;
		break;
	case CGS_WEBRTC_EVENT_TRACK:
		event->in = pevent->in;
		event->event = CGS_EVENT_WEBRTC_TRACK;
		break;
	case CGS_WEBRTC_EVENT_REMOVE_TRACK:
		event->in = pevent->in;
		event->event = CGS_EVENT_WEBRTC_REMOVE_TRACK;
		break;
	case CGS_WEBRTC_EVENT_SET_REMOTE_SESSION_DESCRIPTION_DONE:
		event->event = CGS_EVENT_WEBRTC_SET_REMOTE_SESSION_DESCRIPTION_DONE;
		break;
	case CGS_WEBRTC_EVENT_SET_REMOTE_SESSION_DESCRIPTION_FAILED:
		event->event = CGS_EVENT_WEBRTC_SET_REMOTE_SESSION_DESCRIPTION_FAILED;
		break;
	case CGS_WEBRTC_EVENT_SET_LOCAL_SESSION_DESCRIPTION_DONE:
		event->event = CGS_EVENT_WEBRTC_SET_LOCAL_SESSION_DESCRIPTION_DONE;
		break;
	case CGS_WEBRTC_EVENT_SET_LOCAL_SESSION_DESCRIPTION_FAILED:
		event->event = CGS_EVENT_WEBRTC_SET_LOCAL_SESSION_DESCRIPTION_FAILED;
		break;
	case CGS_WEBRTC_EVENT_CREATE_SESSION_DESCRIPTION_ANSWER_DONE:
		event->event = CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_ANSWER_DONE;
		event->in = pevent->in;
		break;
	case CGS_WEBRTC_EVENT_CREATE_SESSION_DESCRIPTION_ANSWER_FAILED:
		event->event = CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_ANSWER_FAILED;
		event->in = pevent->in;
		break;
	case CGS_WEBRTC_EVENT_CREATE_SESSION_DESCRIPTION_OFFER_DONE:
		event->event = CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_OFFER_DONE;
		event->in = pevent->in;
		break;
	case CGS_WEBRTC_EVENT_CREATE_SESSION_DESCRIPTION_OFFER_FAILED:
		event->event = CGS_EVENT_WEBRTC_CREATE_SESSION_DESCRIPTION_OFFER_FAILED;
		event->in = pevent->in;
		break;
	default:
		break;
	}

	g_async_queue_push(g_task_info.main_loop_queue, event);
	return 0;
}


int cgs_system_deinitialise(void) {
	cgs_webrtc_destroy_conference(g_task_info.pcgs_webrtc_conference);
	cgs_webrtc_deinit(g_task_info.pcgs_webrtc);
	cgs_websockets_deinit(g_task_info.pcgs_websockets);
	g_hash_table_destroy(g_task_info.main_context_hash_table);
	g_async_queue_unref(g_task_info.main_loop_queue);
	return 0;
}

int cgs_read_config_file(void) {
	return 0;
}








