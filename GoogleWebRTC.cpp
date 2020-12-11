
#ifdef _WIN32
#define WEBRTC_WIN
#else
#define WEBRTC_POSIX
#endif

#define NOMINMAX
#define WEBRTC_INCLUDE_INTERNAL_AUDIO_DEVICE

#include "GoogleWebRTC.h"

#include <iostream>
#include <list>

#include <glib.h>
#include <jansson.h>
#include <stdio.h>
#include <stdint.h>

#include "api/create_peerconnection_factory.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_sender_interface.h"
#include "rtc_base/ref_counted_object.h"
#include "modules/audio_device/include/audio_device.h"
#include "modules/audio_device/audio_device_impl.h"

#include "modules/video_capture/video_capture.h"
#include "modules/video_capture/video_capture_factory.h"
#include "api/task_queue/default_task_queue_factory.h"

#include "modules/audio_device/include/audio_device_data_observer.h"
#include "modules/audio_device/include/audio_device_defines.h"

//#include "third_party/blink/renderer/modules/mediarecorder/media_recorder.h"

extern "C" {
#include "media_file_writer.h"
}

#include "fake_audio_capture_module.h"

#ifdef _DEBUG
#pragma comment(lib, "C:\\webrtc\\webrtc-checkout\\src\\out\\debug\\obj\\webrtc.lib")
#else
#pragma comment(lib, "C:\\webrtc\\webrtc-checkout\\src\\out\\release\\obj\\webrtc.lib")
#endif

class cgs_webrtc_peer_connection_observer;

struct cgs_webrtc {
	cgs_webrtc_event_callback callback;
	std::unique_ptr<rtc::Thread> signaling_thread;
	std::unique_ptr<rtc::Thread> network_thread;
	std::unique_ptr<rtc::Thread> worker_thread;
	rtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device;
	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory;
};

struct cgs_webrtc_instance {
	cgs_webrtc* pcgs_webrtc;
	void* user_context;
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection;
	cgs_webrtc_peer_connection_observer* peer_connection_observer;
	std::list<std::pair<webrtc::MediaStreamTrackInterface*, struct cgs_webrtc_track_list_entry*>> tracks;
	struct cgs_webrtc_conference* pcgs_webrtc_conference;
	struct file_writer_instance_t* pmedia_file_writer_instance;
};


struct cgs_webrtc_conference {
	cgs_webrtc* pcgs_webrtc;
	cgs_webrtc_conference_callback callback;
	std::list<struct cgs_webrtc_instance*> pwebrtc_instance_list;
	struct file_writer_t* pmedia_file_writer;
	void* user_context;
};


struct cgs_webrtc_track_list_entry {
	union {
		class cgs_webrtc_audio_track_sink* psink_audio;
		class cgs_webrtc_video_track_sink* psink_video;

	};
	std::string stream_id;
};



static int webrtc_remove_track_from_conference(struct cgs_webrtc_instance* pcgs_webrtc_instance, webrtc::MediaStreamTrackInterface* track, const char* streamid);



int cgs_webrtc_init(struct cgs_webrtc** pcgs_webrtc, cgs_webrtc_event_callback callback) {
	int ret;

	*pcgs_webrtc = new cgs_webrtc;
	if (*pcgs_webrtc == NULL) {
		ret = CGS_WEBRTC_ERROR_NOMEM;
		goto GET_OUT;
	}

	/* Start threads */
	(*pcgs_webrtc)->signaling_thread = rtc::Thread::Create();
	if (!(*pcgs_webrtc)->signaling_thread) {
		ret = CGS_WEBRTC_ERROR_WEBRTC;
		goto GET_OUT;
	}
	(*pcgs_webrtc)->signaling_thread->SetName("cgs_webrtc_signaling_thread", nullptr);
	if(!(*pcgs_webrtc)->signaling_thread->Start()) {
		ret = CGS_WEBRTC_ERROR_WEBRTC;
		goto GET_OUT;
	}

	(*pcgs_webrtc)->network_thread = rtc::Thread::CreateWithSocketServer();
	if (!(*pcgs_webrtc)->network_thread) {
		ret = CGS_WEBRTC_ERROR_WEBRTC;
		goto GET_OUT;
	}
	(*pcgs_webrtc)->network_thread->SetName("cgs_webrtc_network_thread", nullptr);
	if (!(*pcgs_webrtc)->network_thread->Start()) {
		ret = CGS_WEBRTC_ERROR_WEBRTC;
		goto GET_OUT;
	}

	(*pcgs_webrtc)->worker_thread = rtc::Thread::Create();
	if (!(*pcgs_webrtc)->worker_thread) {
		ret = CGS_WEBRTC_ERROR_WEBRTC;
		goto GET_OUT;
	}
	(*pcgs_webrtc)->worker_thread->SetName("cgs_webrtc_worker_thread", nullptr);
	if (!(*pcgs_webrtc)->worker_thread->Start()) {
		ret = CGS_WEBRTC_ERROR_WEBRTC;
		goto GET_OUT;
	}

	/* Create dummy audio device so incoming audio tracks are sent to it */
	(*pcgs_webrtc)->audio_device = (*pcgs_webrtc)->worker_thread->Invoke<rtc::scoped_refptr<webrtc::AudioDeviceModule>>(RTC_FROM_HERE,
		[]()
		{
#if 1
			return FakeAudioCaptureModule::Create();
#else
			return webrtc::AudioDeviceModule::Create(webrtc::AudioDeviceModule::kPlatformDefaultAudio, webrtc::CreateDefaultTaskQueueFactory().get());
#endif
		});
	if (!(*pcgs_webrtc)->audio_device) {
		ret = CGS_WEBRTC_ERROR_WEBRTC;
		goto GET_OUT;
	}
	(*pcgs_webrtc)->audio_device->Init();

	(*pcgs_webrtc)->peer_connection_factory = webrtc::CreatePeerConnectionFactory(
											(*pcgs_webrtc)->network_thread.get() /* network_thread */, 
											(*pcgs_webrtc)->worker_thread.get() /* worker_thread */,
											(*pcgs_webrtc)->signaling_thread.get() /* signaling_thread */, 
#if 0//ndef _DEBUG
											nullptr,/* default_adm */
#else
											(*pcgs_webrtc)->audio_device ,////
#endif
											webrtc::CreateBuiltinAudioEncoderFactory(),
											webrtc::CreateBuiltinAudioDecoderFactory(),
											webrtc::CreateBuiltinVideoEncoderFactory(),
											webrtc::CreateBuiltinVideoDecoderFactory(),
											nullptr /* audio_mixer */,
											nullptr /* audio_processing */);
	if (!(*pcgs_webrtc)->peer_connection_factory) {
		ret = CGS_WEBRTC_ERROR_WEBRTC;
		goto GET_OUT;
	}

	(*pcgs_webrtc)->callback = callback;
	ret = CGS_WEBRTC_ERROR_SUCCESS;

GET_OUT:
	if (ret) {
		if (*pcgs_webrtc) {
			if ((*pcgs_webrtc)->signaling_thread)
				(*pcgs_webrtc)->signaling_thread->Stop();
			if ((*pcgs_webrtc)->network_thread)
				(*pcgs_webrtc)->network_thread->Stop();
			if ((*pcgs_webrtc)->worker_thread)
				(*pcgs_webrtc)->worker_thread->Stop();
			if ((*pcgs_webrtc)->audio_device)
				(*pcgs_webrtc)->audio_device->Terminate();
			delete* pcgs_webrtc;
		}
	}
	return ret;
}


#define REC_AUDIO_TO_WAV 0

static int64_t pts = 0;
class cgs_webrtc_audio_track_sink : public webrtc::AudioTrackSinkInterface {
private:
	struct cgs_webrtc_instance* pcgs_webrtc_instance_;
	struct cgs_webrtc* pcgs_webrtc_;

public:
	cgs_webrtc_audio_track_sink(struct cgs_webrtc* pcgs_webrtc, struct cgs_webrtc_instance* pcgs_webrtc_instance)
		: pcgs_webrtc_(pcgs_webrtc), pcgs_webrtc_instance_(pcgs_webrtc_instance) {
	}

	virtual void OnData(const void* audio_data,
						int bits_per_sample,
						int sample_rate,
						size_t number_of_channels,
						size_t number_of_frames) {
		if(pcgs_webrtc_instance_->pmedia_file_writer_instance)
			file_writer_push_audio_data(pcgs_webrtc_instance_->pmedia_file_writer_instance, audio_data, bits_per_sample, sample_rate, number_of_channels, number_of_frames);
	}
};

class cgs_webrtc_video_track_sink : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
private:
	struct cgs_webrtc_instance* pcgs_webrtc_instance_;
	struct cgs_webrtc* pcgs_webrtc_;

public:
	cgs_webrtc_video_track_sink(struct cgs_webrtc* pcgs_webrtc, struct cgs_webrtc_instance* pcgs_webrtc_instance)
		: pcgs_webrtc_(pcgs_webrtc), pcgs_webrtc_instance_(pcgs_webrtc_instance)
	{

	}

	// VideoSinkInterface implementation
	void OnFrame(const webrtc::VideoFrame& frame) override {
		const webrtc::I420BufferInterface* i420 = frame.video_frame_buffer()->GetI420();
		uint8_t* y = (uint8_t*)i420->DataY();
		uint8_t* u = (uint8_t*)i420->DataU();
		uint8_t* v = (uint8_t*)i420->DataV();

		uint32_t pitchY = i420->StrideY();
		uint32_t pitchU = i420->StrideU();
		uint32_t pitchV = i420->StrideV();

		if (pcgs_webrtc_instance_->pmedia_file_writer_instance)
			file_writer_push_video_frame(pcgs_webrtc_instance_->pmedia_file_writer_instance, frame.timestamp_us(), frame.video_frame_buffer()->width(), frame.video_frame_buffer()->height(), y, u, v, pitchY, pitchU, pitchV);
	}
};

class cgs_webrtc_peer_connection_observer : public webrtc::PeerConnectionObserver {
private:
	struct cgs_webrtc_instance* pcgs_webrtc_instance_;
	struct cgs_webrtc* pcgs_webrtc_;

public:
	cgs_webrtc_peer_connection_observer(struct cgs_webrtc_instance* pcgs_webrtc_instance)
		: pcgs_webrtc_instance_(pcgs_webrtc_instance), pcgs_webrtc_(pcgs_webrtc_instance->pcgs_webrtc){}
	~cgs_webrtc_peer_connection_observer() {}

	void OnSignalingChange(
		webrtc::PeerConnectionInterface::SignalingState new_state) override {
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_SIGNALING_CHANGE;
		event.in = NULL;
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	// Triggered when media is received on a new stream from remote peer.
	void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_ADD_STREAM;
		event.in = (void *)stream->id().c_str();
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	// Triggered when a remote peer closes a stream.
	void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_REMOVE_STREAM;
		event.in = (void*)stream->id().c_str();
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> channel) override {
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_DATA_CHANNEL;
		event.in = NULL;
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	void OnRenegotiationNeeded() override {
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_RENEGOTIATION_NEEDED;
		event.in = NULL;
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	// Called any time the standards-compliant IceConnectionState changes.
	virtual void OnStandardizedIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) {
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_ICE_CONNECTION_CHANGE;
		event.in = NULL;
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	// Called any time the PeerConnectionState changes.
	virtual void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState new_state) {
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_PEER_CONNECTION_CHANGE;
		event.in = NULL;
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) override {
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_ICE_GATHERING_CHANGE;
#if 1
		event.in = NULL;
#else
		if (new_state == webrtc::PeerConnectionInterface::kIceGatheringComplete) {
			json_t* json_candidate = json_pack("{sssiss}", "sdpMid", "", "sdpMLineIndex", 0, "candidate", "");
			event.in = json_dumps(json_candidate, JSON_COMPACT);
			json_decref(json_candidate);
		}
		else {
			event.in = NULL;
		}
#endif
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override {
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_ICE_CANDIDATE;

		std::string sdp;
		candidate->ToString(&sdp);

		json_t* json_candidate = json_pack("{sssssiss}", "type", "candidate", "sdpMid", candidate->sdp_mid().c_str(), "sdpMLineIndex", candidate->sdp_mline_index(), "candidate", sdp.c_str());
		event.in = json_dumps(json_candidate, JSON_COMPACT);
		json_decref(json_candidate);

		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	virtual void OnIceCandidateError(const std::string& host_candidate,const std::string& url,int error_code,const std::string& error_text) {
		std::cout << "OnIceCandidateError: " << host_candidate << "," << url << "," << error_code<< "," << error_text;
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_ICE_CANDIDATE_ERROR;
		event.in = NULL;
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	virtual void OnIceCandidatesRemoved(const std::vector<cricket::Candidate>& candidates) {
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_ICE_CANDIDATES_REMOVED;
		event.in = NULL;
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	void OnIceConnectionReceivingChange(bool receiving) override {
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_ICE_CONNECTION_RECEIVING_CHANGE;
		event.in = NULL;
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	virtual void OnIceSelectedCandidatePairChanged(const cricket::CandidatePairChangeEvent& evt) {
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_ICE_SELECTED_CANDIDATE_PAIR_CHANGED;
		event.in = NULL;
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	virtual void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_TRACK;
		event.in = (void*)g_strdup(transceiver->receiver()->track()->id().c_str());
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
};


int cgs_webrtc_create_instance(struct cgs_webrtc* pcgs_webrtc, struct cgs_webrtc_instance** pcgs_webrtc_instance, void* user_context) {
	int ret;

	*pcgs_webrtc_instance = new cgs_webrtc_instance;
	if (*pcgs_webrtc_instance == NULL) {
		ret = CGS_WEBRTC_ERROR_NOMEM;
	}
	else {
		webrtc::PeerConnectionInterface::RTCConfiguration config;
		webrtc::PeerConnectionInterface::IceServer server;
		webrtc::PeerConnectionInterface::IceServer server2;

		config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
		config.enable_dtls_srtp = true;
		server.uri = "stun:stun.l.google.com:19302";
		config.servers.push_back(server);
		server2.uri = "turn:13.232.126.19:3478";
		server2.username = "test";
		server2.password = "test";
		config.servers.push_back(server2);

		(*pcgs_webrtc_instance)->pcgs_webrtc = pcgs_webrtc;
		(*pcgs_webrtc_instance)->user_context = user_context;

		(*pcgs_webrtc_instance)->peer_connection_observer = new cgs_webrtc_peer_connection_observer(*pcgs_webrtc_instance);
		if ((*pcgs_webrtc_instance)->peer_connection_observer == NULL) {
			ret = CGS_WEBRTC_ERROR_NOMEM;
			goto GET_OUT;
		}

		(*pcgs_webrtc_instance)->peer_connection = pcgs_webrtc->peer_connection_factory->CreatePeerConnection(config, nullptr, nullptr, (*pcgs_webrtc_instance)->peer_connection_observer);
		if ((*pcgs_webrtc_instance)->peer_connection == NULL) {
			delete (*pcgs_webrtc_instance)->peer_connection_observer;
			ret = CGS_WEBRTC_ERROR_WEBRTC;
			goto GET_OUT;
		}

#if 0
		rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track(pcgs_webrtc->peer_connection_factory->CreateAudioTrack(
				"audio_label", pcgs_webrtc->peer_connection_factory->CreateAudioSource(cricket::AudioOptions())));
		auto result_or_error = (*pcgs_webrtc_instance)->peer_connection->AddTrack(audio_track, { "stream_id" });
		if (!result_or_error.ok()) {
			printf("Failed to add audio track to PeerConnection: %s", result_or_error.error().message());
		}
#endif
		ret = CGS_WEBRTC_ERROR_SUCCESS;
	}

GET_OUT:
	if (ret) {
		if (*pcgs_webrtc_instance)
			delete* pcgs_webrtc_instance;
	}
	return ret;
}


class cgs_SetSessionDescriptionObserver;

class cgs_CreateSessionDescriptionObserver : public webrtc::CreateSessionDescriptionObserver {
private:
	struct cgs_webrtc_instance* pcgs_webrtc_instance_;
	struct cgs_webrtc* pcgs_webrtc_;
	bool offer_;

public:
	cgs_CreateSessionDescriptionObserver(struct cgs_webrtc_instance* pcgs_webrtc_instance, bool offer)
		: pcgs_webrtc_instance_(pcgs_webrtc_instance), pcgs_webrtc_(pcgs_webrtc_instance->pcgs_webrtc), offer_(offer){}
	~cgs_CreateSessionDescriptionObserver() {}

	// CreateSessionDescriptionObserver implementation.
	virtual void OnSuccess(webrtc::SessionDescriptionInterface* desc) override {
		cgs_webrtc_event event;
		event.code = offer_ ? CGS_WEBRTC_EVENT_CREATE_SESSION_DESCRIPTION_OFFER_DONE : CGS_WEBRTC_EVENT_CREATE_SESSION_DESCRIPTION_ANSWER_DONE;

		std::string sdp;
		desc->ToString(&sdp);

		json_t* json_answer = json_pack("{ssss}", "type", webrtc::SdpTypeToString(desc->GetType()), "sdp", sdp.c_str());

		event.in = json_dumps(json_answer, JSON_COMPACT);
		json_decref(json_answer);

		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
		cgs_webrtc_set_local_description(pcgs_webrtc_instance_, desc, offer_);
	}
	virtual void OnFailure(webrtc::RTCError error) override {
		cgs_webrtc_event event;
		event.code = offer_ ? CGS_WEBRTC_EVENT_CREATE_SESSION_DESCRIPTION_OFFER_FAILED : CGS_WEBRTC_EVENT_CREATE_SESSION_DESCRIPTION_ANSWER_FAILED;

		event.in = g_strdup(error.message());
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	static cgs_CreateSessionDescriptionObserver* Create(struct cgs_webrtc_instance* pcgs_webrtc_instance, bool offer) {
		return new rtc::RefCountedObject<cgs_CreateSessionDescriptionObserver>(pcgs_webrtc_instance, offer);
	}
};

class cgs_SetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver {
private:
	struct cgs_webrtc_instance* pcgs_webrtc_instance_;
	struct cgs_webrtc* pcgs_webrtc_;
	bool remote_;

public:
	cgs_SetSessionDescriptionObserver(struct cgs_webrtc_instance* pcgs_webrtc_instance, bool remote)
		: pcgs_webrtc_instance_(pcgs_webrtc_instance), pcgs_webrtc_(pcgs_webrtc_instance->pcgs_webrtc), remote_(remote){}
	~cgs_SetSessionDescriptionObserver() {}

	virtual void OnSuccess() override { 
		cgs_webrtc_event event;
		event.code = remote_? CGS_WEBRTC_EVENT_SET_REMOTE_SESSION_DESCRIPTION_DONE : CGS_WEBRTC_EVENT_SET_LOCAL_SESSION_DESCRIPTION_DONE;
		event.in = NULL;
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	virtual void OnFailure(webrtc::RTCError error) override {
		cgs_webrtc_event event;
		event.code = remote_ ? CGS_WEBRTC_EVENT_SET_REMOTE_SESSION_DESCRIPTION_FAILED : CGS_WEBRTC_EVENT_SET_LOCAL_SESSION_DESCRIPTION_FAILED;
		event.in = NULL;
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	static cgs_SetSessionDescriptionObserver* Create(struct cgs_webrtc_instance* pcgs_webrtc_instance, bool remote) {
		return new rtc::RefCountedObject<cgs_SetSessionDescriptionObserver>(pcgs_webrtc_instance, remote);
	}
};




int cgs_webrtc_set_remote_description(struct cgs_webrtc_instance* pcgs_webrtc_instance, const char* sdp, bool offer) {
	std::unique_ptr<webrtc::SessionDescriptionInterface> session_description = webrtc::CreateSessionDescription(offer ? webrtc::SdpType::kOffer : webrtc::SdpType::kAnswer, sdp, nullptr);

	if (!session_description) {
		printf("\n\n\nCannot create session description rtc object\n\n\n");
		return CGS_WEBRTC_ERROR_WEBRTC;
	}

	pcgs_webrtc_instance->peer_connection->SetRemoteDescription(cgs_SetSessionDescriptionObserver::Create(pcgs_webrtc_instance, true), session_description.release());
	return CGS_WEBRTC_ERROR_SUCCESS;
}

int cgs_webrtc_create_offer(struct cgs_webrtc_instance* pcgs_webrtc_instance) {
	pcgs_webrtc_instance->peer_connection->CreateOffer(cgs_CreateSessionDescriptionObserver::Create(pcgs_webrtc_instance, true), webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
	return CGS_WEBRTC_ERROR_SUCCESS;
}

int cgs_webrtc_create_answer(struct cgs_webrtc_instance* pcgs_webrtc_instance) {
	pcgs_webrtc_instance->peer_connection->CreateAnswer(cgs_CreateSessionDescriptionObserver::Create(pcgs_webrtc_instance, false), webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
	return CGS_WEBRTC_ERROR_SUCCESS;
}

int cgs_webrtc_set_local_description(struct cgs_webrtc_instance* pcgs_webrtc_instance, void* desc, bool offer) {
	pcgs_webrtc_instance->peer_connection->SetLocalDescription(cgs_SetSessionDescriptionObserver::Create(pcgs_webrtc_instance, false), (webrtc::SessionDescriptionInterface*)desc);
	return CGS_WEBRTC_ERROR_SUCCESS;
}

int cgs_webrtc_add_ice_candiate(struct cgs_webrtc_instance* pcgs_webrtc_instance, const char* sdp_mid, int sdp_mlineindex, const char* sdp) {
	if (!sdp_mid || (sdp_mid[0] == 0)) {
		return CGS_WEBRTC_ERROR_SUCCESS;
	}

	webrtc::SdpParseError error;
	std::unique_ptr<webrtc::IceCandidateInterface> candidate(webrtc::CreateIceCandidate(sdp_mid, sdp_mlineindex, sdp, &error));

	if (!candidate.get()) {
		return CGS_WEBRTC_ERROR_SDP_PARSE;
	}

	if (!pcgs_webrtc_instance->peer_connection->AddIceCandidate(candidate.get())) {
		return CGS_WEBRTC_ERROR_WEBRTC;
	}
	return CGS_WEBRTC_ERROR_SUCCESS;
}


int cgs_webrtc_create_conference(struct cgs_webrtc* pcgs_webrtc, struct cgs_webrtc_conference** pcgs_webrtc_conference, cgs_webrtc_conference_callback callback, void*user_context) {
	int ret;

	*pcgs_webrtc_conference = new cgs_webrtc_conference;
	if (*pcgs_webrtc_conference == NULL) {
		ret = CGS_WEBRTC_ERROR_NOMEM;
		goto GET_OUT;
	}

	if (file_writer_alloc(&(*pcgs_webrtc_conference)->pmedia_file_writer)) {
		ret = CGS_WEBRTC_ERROR_FFMPEG;
		goto GET_OUT;
	}

	(*pcgs_webrtc_conference)->pcgs_webrtc = pcgs_webrtc;
	(*pcgs_webrtc_conference)->callback = callback;
	ret = CGS_WEBRTC_ERROR_SUCCESS;

GET_OUT:
	if (ret) {
		if (*pcgs_webrtc_conference) {
			if ((*pcgs_webrtc_conference)->pmedia_file_writer)
				file_writer_free((*pcgs_webrtc_conference)->pmedia_file_writer);
			delete* pcgs_webrtc_conference;
		}
	}
	return ret;
}

int cgs_webrtc_add_to_conference(struct cgs_webrtc_instance* pcgs_webrtc_instance, struct cgs_webrtc_conference* pcgs_webrtc_conference) {
	auto it = std::find(pcgs_webrtc_conference->pwebrtc_instance_list.begin(), pcgs_webrtc_conference->pwebrtc_instance_list.end(), pcgs_webrtc_instance);
	if (it != pcgs_webrtc_conference->pwebrtc_instance_list.end())
		return CGS_WEBRTC_ERROR_SUCCESS;

	/*
	if (0 != file_writer_create_context(pcgs_webrtc_conference->pmedia_file_writer, &pcgs_webrtc_instance->pmedia_file_writer_instance)) {
		return CGS_WEBRTC_ERROR_FFMPEG;
	}
	*/

	for (auto const& receiver : pcgs_webrtc_instance->peer_connection->GetReceivers()) {
		if (receiver->track()) {
			if (receiver->track()->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
				for (auto const& track : pcgs_webrtc_instance->tracks) {
					if (track.first == receiver->track()) {
						((webrtc::AudioTrackInterface*)receiver->track().get())->AddSink(track.second->psink_audio);
						break;
					}
				}
			}
			else {
				for (auto const& track : pcgs_webrtc_instance->tracks) {
					if (track.first == receiver->track()) {
						((webrtc::VideoTrackInterface*)receiver->track().get())->AddOrUpdateSink(track.second->psink_video, rtc::VideoSinkWants());
						break;
					}
				}
			}
		}
	}

	pcgs_webrtc_instance->pcgs_webrtc_conference = pcgs_webrtc_conference;
	pcgs_webrtc_conference->pwebrtc_instance_list.push_back(pcgs_webrtc_instance);

	for (auto const& pwi : pcgs_webrtc_conference->pwebrtc_instance_list) {
		if (pwi && (pwi != pcgs_webrtc_instance)) {
			for (auto const& track : pcgs_webrtc_instance->tracks) {
				auto result_or_error = pwi->peer_connection->AddTrack(track.first, { track.second->stream_id });
				if (!result_or_error.ok()) {
					//std::cout << result_or_error.error().message();
				}
			}
			for (auto const& track : pwi->tracks) {
				auto result_or_error = pcgs_webrtc_instance->peer_connection->AddTrack(track.first, { track.second->stream_id });
				if (!result_or_error.ok()) {
					//std::cout << result_or_error.error().message();
				}
			}
		}
	}
	return CGS_WEBRTC_ERROR_SUCCESS;
}

static int webrtc_remove_track_from_conference(struct cgs_webrtc_instance* pcgs_webrtc_instance, webrtc::MediaStreamTrackInterface* track, const char* streamid) {
	if (pcgs_webrtc_instance->pcgs_webrtc_conference) {
		for (auto const& pwi : pcgs_webrtc_instance->pcgs_webrtc_conference->pwebrtc_instance_list) {
			if (pwi) {
				for (auto const& pwi_sender : pwi->peer_connection->GetSenders()) {
					if (pwi_sender->track() && track->id() == pwi_sender->track()->id()) {
						pwi->peer_connection->RemoveTrack(pwi_sender);

						/* Fire a remove track event*/
						cgs_webrtc_event event;
						event.code = CGS_WEBRTC_EVENT_REMOVE_TRACK;
						event.in = (void*)g_strdup(streamid);
						pwi->pcgs_webrtc->callback(pwi->pcgs_webrtc, pwi, &event, pwi->user_context);
						break;
					}
				}
			}
		}
		pcgs_webrtc_instance->pcgs_webrtc_conference->pwebrtc_instance_list.remove(pcgs_webrtc_instance);
		pcgs_webrtc_instance->pcgs_webrtc_conference = NULL;
	}
	return CGS_WEBRTC_ERROR_SUCCESS;
}


struct wavfile_header {

	char riff_tag[4];
	int32_t riff_length;
	char wave_tag[4];
	char fmt_tag[4];
	int32_t fmt_length;
	int16_t audio_format;
	int16_t num_channels;
	int32_t sample_rate;
	int32_t byte_rate;
	int16_t block_align;
	int16_t bits_per_sample;
	char data_tag[4];
	int32_t data_length;
};

int cgs_webrtc_on_add_track(struct cgs_webrtc_instance* pcgs_webrtc_instance, const char* trackid) {
	for (auto const& receiver : pcgs_webrtc_instance->peer_connection->GetReceivers()) {
		if (receiver->track() && receiver->track()->id().compare(trackid) == 0) {
			/* Add this track to the list */
			std::ostringstream local_stream_id, remote_stream_id;
			if (pcgs_webrtc_instance->tracks.size() < 2) {
				local_stream_id << "loopback" << "-" << pcgs_webrtc_instance;
				remote_stream_id << "stream" << "-" << pcgs_webrtc_instance;
			}
			else {
				local_stream_id << "screenshare" << "-" << pcgs_webrtc_instance;
				remote_stream_id << "screenshare" << "-" << pcgs_webrtc_instance;
			}
			if (receiver->track()->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
				pcgs_webrtc_instance->peer_connection->AddTrack(receiver->track(), { local_stream_id.str() });
				cgs_webrtc_track_list_entry* entry = new cgs_webrtc_track_list_entry;
				entry->stream_id = remote_stream_id.str();
#if REC_AUDIO_TO_WAV
				char szFile[MAX_PATH];
				sprintf(szFile, "c:\\Temp\\audio_%p.wav", pcgs_webrtc_instance);
				entry->fp_rec = fopen(szFile, "wb");

				struct wavfile_header header;

				int samples_per_second = 48000;
				int bits_per_sample = 16;
				int num_channels = 1;

				strncpy(header.riff_tag, "RIFF", 4);
				strncpy(header.wave_tag, "WAVE", 4);
				strncpy(header.fmt_tag, "fmt ", 4);
				strncpy(header.data_tag, "data", 4);

				header.riff_length = 0;
				header.fmt_length = 16;
				header.audio_format = 1;
				header.num_channels = num_channels;
				header.sample_rate = samples_per_second;
				header.byte_rate = samples_per_second * (bits_per_sample / 8);
				header.block_align = bits_per_sample / 8;
				header.bits_per_sample = bits_per_sample;
				header.data_length = 0;

				fwrite(&header, sizeof(header), 1, entry->fp_rec);
				fflush(entry->fp_rec);
#endif
				entry->psink_audio = new cgs_webrtc_audio_track_sink(pcgs_webrtc_instance->pcgs_webrtc, pcgs_webrtc_instance);
				pcgs_webrtc_instance->tracks.push_back(std::make_pair(receiver->track(), entry));
			}
			else {
				pcgs_webrtc_instance->peer_connection->AddTrack(receiver->track(), { local_stream_id.str() });
				cgs_webrtc_track_list_entry* entry = new cgs_webrtc_track_list_entry;
				entry->stream_id = remote_stream_id.str();
#if 0
				char szFile[MAX_PATH];
				sprintf(szFile, "c:\\Temp\\video_%p.yuv", pcgs_webrtc_instance);
				entry->fp_rec = fopen(szFile, "wb");
#endif
				entry->psink_video = new cgs_webrtc_video_track_sink(pcgs_webrtc_instance->pcgs_webrtc, pcgs_webrtc_instance);
				pcgs_webrtc_instance->tracks.push_back(std::make_pair(receiver->track(), entry));
			}
			break;
		}
	}

	return CGS_WEBRTC_ERROR_SUCCESS;
}

int cgs_webrtc_on_remove_track(struct cgs_webrtc_instance* pcgs_webrtc_instance, const char *trackid) {
	/* Remove the track from whom it belongs*/
	for (auto const& track : pcgs_webrtc_instance->tracks) {
		if (track.first->id().compare(trackid) == 0) {
			/* Remove this track from same conference participants */
			webrtc_remove_track_from_conference(pcgs_webrtc_instance, track.first, track.second->stream_id.c_str());


			/* Fire a remove track event, to be sent to the other side */
			cgs_webrtc_event event;
			event.code = CGS_WEBRTC_EVENT_REMOVE_TRACK;
			event.in = (void*)g_strdup(track.second->stream_id.c_str());
			pcgs_webrtc_instance->tracks.remove(track);
			pcgs_webrtc_instance->pcgs_webrtc->callback(pcgs_webrtc_instance->pcgs_webrtc, pcgs_webrtc_instance, &event, pcgs_webrtc_instance->user_context);
			break;
		}
	}
	return CGS_WEBRTC_ERROR_SUCCESS;
}




int cgs_webrtc_remove_from_conference(struct cgs_webrtc_instance* pcgs_webrtc_instance, struct cgs_webrtc_conference* pcgs_webrtc_conference) {
	for (auto const& pwi : pcgs_webrtc_conference->pwebrtc_instance_list) {
		if (pwi && pwi != pcgs_webrtc_instance) {
			for (auto const& track : pcgs_webrtc_instance->tracks) {
				for (auto const& pwi_sender : pwi->peer_connection->GetSenders()) {
					if(pwi_sender->track() && track.first->id() == pwi_sender->track()->id()){
						pwi->peer_connection->RemoveTrack(pwi_sender);

						/* Fire a remove track event*/
						cgs_webrtc_event event;
						event.code = CGS_WEBRTC_EVENT_REMOVE_TRACK;
						event.in = (void*)g_strdup(track.second->stream_id.c_str());
						pwi->pcgs_webrtc->callback(pwi->pcgs_webrtc, pwi, &event, pwi->user_context);
						break;
					}
				}
			}
			for (auto const& track : pwi->tracks) {
				for (auto const& this_sender : pcgs_webrtc_instance->peer_connection->GetSenders()) {
					if (this_sender->track() && track.first->id() == this_sender->track()->id()) {
						pcgs_webrtc_instance->peer_connection->RemoveTrack(this_sender);

						/* Fire a remove track event, to be sent to the other side */
						cgs_webrtc_event event;
						event.code = CGS_WEBRTC_EVENT_REMOVE_TRACK;
						event.in = (void*)g_strdup(track.second->stream_id.c_str());
						pcgs_webrtc_instance->pcgs_webrtc->callback(pcgs_webrtc_instance->pcgs_webrtc, pcgs_webrtc_instance, &event, pcgs_webrtc_instance->user_context);
						break;
					}
				}
			}
		}
	}

	for (auto const& receiver : pcgs_webrtc_instance->peer_connection->GetReceivers()) {
		if (receiver->track()) {
			if (receiver->track()->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
				for (auto const& track : pcgs_webrtc_instance->tracks) {
					if (track.first == receiver->track()) {
						((webrtc::AudioTrackInterface*)receiver->track().get())->RemoveSink(track.second->psink_audio);
						break;
					}
				}
			}
			else {
				for (auto const& track : pcgs_webrtc_instance->tracks) {
					if (track.first == receiver->track()) {
						((webrtc::VideoTrackInterface*)receiver->track().get())->RemoveSink(track.second->psink_video);
						break;
					}
				}
			}
		}
	}

	file_writer_destroy_context(pcgs_webrtc_instance->pmedia_file_writer_instance);
	pcgs_webrtc_conference->pwebrtc_instance_list.remove(pcgs_webrtc_instance);
	pcgs_webrtc_instance->pcgs_webrtc_conference = NULL;
	return CGS_WEBRTC_ERROR_SUCCESS;
}

int cgs_webrtc_size_conference(struct cgs_webrtc_conference* pcgs_webrtc_conference) {
	return pcgs_webrtc_conference->pwebrtc_instance_list.size();
}

int cgs_webrtc_destroy_conference(struct cgs_webrtc_conference* pcgs_webrtc_conference) {
	file_writer_free(pcgs_webrtc_conference->pmedia_file_writer);
	pcgs_webrtc_conference->pwebrtc_instance_list.clear();
	delete pcgs_webrtc_conference;
	return CGS_WEBRTC_ERROR_SUCCESS;
}

int cgs_webrtc_destroy_instance(struct cgs_webrtc_instance* pcgs_webrtc_instance) {
	pcgs_webrtc_instance->peer_connection->Close();
	for (auto const& track : pcgs_webrtc_instance->tracks) {
		if (track.first->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
#if REC_AUDIO_TO_WAV
			int32_t file_length = ftell(track.second->fp_rec);
			int32_t data_length = file_length - sizeof(struct wavfile_header);
			fseek(track.second->fp_rec, sizeof(struct wavfile_header) - sizeof(int32_t), SEEK_SET);
			fwrite(&data_length, sizeof(data_length), 1, track.second->fp_rec);

			int32_t riff_length = file_length - 8;
			fseek(track.second->fp_rec, 4, SEEK_SET);
			fwrite(&riff_length, sizeof(riff_length), 1, track.second->fp_rec);

			fclose(track.second->fp_rec);
#endif
			delete track.second->psink_audio;
		}
		else {
#if 0
			fclose(track.second->fp_rec);
#endif
			delete track.second->psink_video;
		}

		delete track.second;
	}

	pcgs_webrtc_instance->tracks.clear();
	delete pcgs_webrtc_instance->peer_connection_observer;
	delete pcgs_webrtc_instance; //This will delete the peer connection
	return CGS_WEBRTC_ERROR_SUCCESS;
}

int cgs_webrtc_deinit(struct cgs_webrtc* pcgs_webrtc) {
	pcgs_webrtc->signaling_thread->Stop();
	pcgs_webrtc->network_thread->Stop();
	pcgs_webrtc->worker_thread->Stop();
	delete pcgs_webrtc; //this will call peer_connection_factory destructor

	return CGS_WEBRTC_ERROR_SUCCESS;
}
