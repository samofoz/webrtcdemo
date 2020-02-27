
#ifndef __CGS_WEBSRTC__
#define __CGS_WEBSRTC__

struct cgs_webrtc;
struct cgs_webrtc_instance;
struct cgs_webrtc_conference;
struct cgs_webrtc_track_list_entry;


enum cgs_webrtc_event_code {
	CGS_WEBRTC_EVENT_UNKNOWN,
	CGS_WEBRTC_EVENT_SIGNALING_CHANGE,
	CGS_WEBRTC_EVENT_ADD_STREAM,
	CGS_WEBRTC_EVENT_REMOVE_STREAM,
	CGS_WEBRTC_EVENT_DATA_CHANNEL,
	CGS_WEBRTC_EVENT_RENEGOTIATION_NEEDED,
	CGS_WEBRTC_EVENT_ICE_CONNECTION_CHANGE,
	CGS_WEBRTC_EVENT_PEER_CONNECTION_CHANGE,
	CGS_WEBRTC_EVENT_ICE_GATHERING_CHANGE,
	CGS_WEBRTC_EVENT_ICE_CANDIDATE,
	CGS_WEBRTC_EVENT_ICE_CANDIDATE_ERROR,
	CGS_WEBRTC_EVENT_ICE_CANDIDATES_REMOVED,
	CGS_WEBRTC_EVENT_ICE_CONNECTION_RECEIVING_CHANGE,
	CGS_WEBRTC_EVENT_ICE_SELECTED_CANDIDATE_PAIR_CHANGED,
	CGS_WEBRTC_EVENT_TRACK,
	CGS_WEBRTC_EVENT_REMOVE_TRACK,
	CGS_WEBRTC_EVENT_SET_REMOTE_SESSION_DESCRIPTION_DONE,
	CGS_WEBRTC_EVENT_SET_REMOTE_SESSION_DESCRIPTION_FAILED,
	CGS_WEBRTC_EVENT_SET_LOCAL_SESSION_DESCRIPTION_DONE,
	CGS_WEBRTC_EVENT_SET_LOCAL_SESSION_DESCRIPTION_FAILED,
	CGS_WEBRTC_EVENT_CREATE_SESSION_DESCRIPTION_OFFER_DONE,
	CGS_WEBRTC_EVENT_CREATE_SESSION_DESCRIPTION_OFFER_FAILED,
	CGS_WEBRTC_EVENT_CREATE_SESSION_DESCRIPTION_ANSWER_DONE,
	CGS_WEBRTC_EVENT_CREATE_SESSION_DESCRIPTION_ANSWER_FAILED,
};

struct cgs_webrtc_event {
	cgs_webrtc_event_code code;
	void* in;
};

enum cgs_webrtc_error {
	CGS_WEBRTC_ERROR_SUCCESS,
	CGS_WEBRTC_ERROR_NOMEM,
	CGS_WEBRTC_ERROR_WEBRTC,
	CGS_WEBRTC_ERROR_BAD_ARG,
	CGS_WEBRTC_ERROR_THREAD_CREATE,
	CGS_WEBRTC_ERROR_SDP_PARSE
};

typedef int (*cgs_webrtc_event_callback)(cgs_webrtc* pcgs_webrtc, cgs_webrtc_instance* pcgs_webrtc_instance, cgs_webrtc_event* pevent, void* user_context);
typedef int (*cgs_webrtc_conference_callback)(cgs_webrtc* pcgs_webrtc, cgs_webrtc_conference* pcgs_webrtc_instance, cgs_webrtc_event* pevent, void* user_context);

int cgs_webrtc_init(struct cgs_webrtc** pcgs_webrtc, cgs_webrtc_event_callback callback);

int cgs_webrtc_create_instance(struct cgs_webrtc* pcgs_webrtc, struct cgs_webrtc_instance** pcgs_webrtc_instance, void* user_context);
int cgs_webrtc_add_ice_candiate(struct cgs_webrtc_instance* pcgs_webrtc_instance, const char *sdp_mid, int sdp_mlineindex, const char *sdp);
int cgs_webrtc_set_remote_description(struct cgs_webrtc_instance* pcgs_webrtc_instance, const char* sdp, bool offer);
int cgs_webrtc_create_offer(struct cgs_webrtc_instance* pcgs_webrtc_instance);
int cgs_webrtc_create_answer(struct cgs_webrtc_instance* pcgs_webrtc_instance);
int cgs_webrtc_set_local_description(struct cgs_webrtc_instance* pcgs_webrtc_instance, void* desc, bool offer);
int cgs_webrtc_destroy_instance(struct cgs_webrtc_instance* pcgs_webrtc_instance);

int cgs_webrtc_create_conference(struct cgs_webrtc* pcgs_webrtc, struct cgs_webrtc_conference** pcgs_webrtc_conference, cgs_webrtc_conference_callback callback, void* user_context);
int cgs_webrtc_add_to_conference(struct cgs_webrtc_instance* pcgs_webrtc_instance, struct cgs_webrtc_conference* pcgs_webrtc_conference);
int cgs_webrtc_remove_from_conference(struct cgs_webrtc_instance* pcgs_webrtc_instance, struct cgs_webrtc_conference* pcgs_webrtc_conference);
int cgs_webrtc_destroy_conference(struct cgs_webrtc_conference* pcgs_webrtc_conference);

int cgs_webrtc_deinit(struct cgs_webrtc* pcgs_webrtc);

#endif