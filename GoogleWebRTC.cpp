
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
	std::list<webrtc::MediaStreamTrackInterface*> tracks;
};


struct cgs_webrtc_conference {
	cgs_webrtc* pcgs_webrtc;
	cgs_webrtc_conference_callback callback;
	std::list<struct cgs_webrtc_instance*> pwebrtc_instance_list;
	void* user_context;
};

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
			return webrtc::AudioDeviceModule::Create(webrtc::AudioDeviceModule::kDummyAudio, webrtc::CreateDefaultTaskQueueFactory().get());
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
											(*pcgs_webrtc)->audio_device /* default_adm */,
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
		if (new_state == webrtc::PeerConnectionInterface::kIceGatheringComplete) {
			json_t* json_candidate = json_pack("{sssiss}", "sdpMid", "", "sdpMLineIndex", 0, "candidate", "");
			event.in = json_dumps(json_candidate, JSON_COMPACT);
			json_decref(json_candidate);
		}
		else {
			event.in = NULL;
		}
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override {
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_ICE_CANDIDATE;

		std::string sdp;
		candidate->ToString(&sdp);

		json_t* json_candidate = json_pack("{sssiss}", "sdpMid", candidate->sdp_mid().c_str(), "sdpMLineIndex", candidate->sdp_mline_index(), "candidate", sdp.c_str());
		event.in = json_dumps(json_candidate, JSON_COMPACT);
		json_decref(json_candidate);

		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	virtual void OnIceCandidateError(const std::string& host_candidate,const std::string& url,int error_code,const std::string& error_text) {
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
		pcgs_webrtc_instance_->tracks.push_back(transceiver->receiver()->track());
#if 1
//		if ((transceiver->receiver()->track()->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) && (transceiver->receiver()->track()->state() == webrtc::MediaStreamTrackInterface::kLive))
		{
			auto result_or_error = pcgs_webrtc_instance_->peer_connection->AddTrack(transceiver->receiver()->track().get(), transceiver->receiver()->stream_ids());
			if (!result_or_error.ok()) {
				printf("Failed to add audio track to PeerConnection: %s", result_or_error.error().message());
			}
		}
#endif // 1
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_TRACK;
		event.in = NULL;
		pcgs_webrtc_->callback(pcgs_webrtc_, pcgs_webrtc_instance_, &event, pcgs_webrtc_instance_->user_context);
	}
	virtual void OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {
		pcgs_webrtc_instance_->tracks.remove(receiver->track());
		cgs_webrtc_event event;
		event.code = CGS_WEBRTC_EVENT_REMOVE_TRACK;
		event.in = NULL;
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

		config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
		config.enable_dtls_srtp = true;
		//server.uri = "stun:stun.l.google.com:19302";
		//config.servers.push_back(server);
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

		event.in = NULL;
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

	(*pcgs_webrtc_conference)->pcgs_webrtc = pcgs_webrtc;
	(*pcgs_webrtc_conference)->callback = callback;
	ret = CGS_WEBRTC_ERROR_SUCCESS;

GET_OUT:
	if (ret) {
		if (*pcgs_webrtc_conference) {
			delete* pcgs_webrtc_conference;
		}
	}
	return ret;
}

int cgs_webrtc_add_to_conference(struct cgs_webrtc_instance* pcgs_webrtc_instance, struct cgs_webrtc_conference* pcgs_webrtc_conference) {
	pcgs_webrtc_conference->pwebrtc_instance_list.push_back(pcgs_webrtc_instance);
	for (auto const& pwi : pcgs_webrtc_conference->pwebrtc_instance_list) {
		if (pwi && (pwi != pcgs_webrtc_instance)) {
			for (auto const& track : pcgs_webrtc_instance->tracks) {
				std::ostringstream stream_id;
				stream_id << pcgs_webrtc_instance << pwi;
				auto result_or_error = pwi->peer_connection->AddTrack(track, { stream_id.str() });
				if (!result_or_error.ok()) {
					//result_or_error.error().message()
				}
			}
			for (auto const& track : pwi->tracks) {
				std::ostringstream stream_id;
				stream_id << pwi << pcgs_webrtc_instance;
				auto result_or_error = pcgs_webrtc_instance->peer_connection->AddTrack(track, { stream_id.str() });
				if (!result_or_error.ok()) {
					//result_or_error.error().message()
				}
			}
		}
	}
	return CGS_WEBRTC_ERROR_SUCCESS;
}

int cgs_webrtc_remove_from_conference(struct cgs_webrtc_instance* pcgs_webrtc_instance, struct cgs_webrtc_conference* pcgs_webrtc_conference) {
	for (auto const& pwi : pcgs_webrtc_conference->pwebrtc_instance_list) {
		if (pwi && pwi != pcgs_webrtc_instance) {
			for (int i = 0; i < pcgs_webrtc_instance->peer_connection->GetSenders().size(); ++i) {
				pwi->peer_connection->RemoveTrack(pcgs_webrtc_instance->peer_connection->GetSenders()[i]);
			}
			for (int i = 0; i < pwi->peer_connection->GetSenders().size(); ++i) {
				pcgs_webrtc_instance->peer_connection->RemoveTrack(pwi->peer_connection->GetSenders()[i]);
			}
		}
	}
	pcgs_webrtc_conference->pwebrtc_instance_list.remove(pcgs_webrtc_instance);
	return CGS_WEBRTC_ERROR_SUCCESS;
}



int cgs_webrtc_destroy_conference(struct cgs_webrtc_conference* pcgs_webrtc_conference) {
	pcgs_webrtc_conference->pwebrtc_instance_list.clear();
	delete pcgs_webrtc_conference;
	return CGS_WEBRTC_ERROR_SUCCESS;
}

int cgs_webrtc_destroy_instance(struct cgs_webrtc_instance* pcgs_webrtc_instance) {
	pcgs_webrtc_instance->tracks.clear();
	pcgs_webrtc_instance->peer_connection->Close();
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
