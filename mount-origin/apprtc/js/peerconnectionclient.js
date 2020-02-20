var PeerConnectionClient = function (params, startTime) {
    trace("PeerConnectionClient()");
    this.params_ = params;
    this.startTime_ = startTime;
    trace("Creating RTCPeerConnnection with:\n" + "  config: '" + JSON.stringify(params.peerConnectionConfig) + "';\n" + "  constraints: '" + JSON.stringify(params.peerConnectionConstraints) + "'.");
    this.pc_ = new RTCPeerConnection(params.peerConnectionConfig, params.peerConnectionConstraints);
    this.pc_.onicecandidate = this.onIceCandidate_.bind(this);
    this.pc_.ontrack = this.onRemoteStreamAdded_.bind(this);
    this.pc_.onremovestream = trace.bind(null, "Remote stream removed.");
    this.pc_.onsignalingstatechange = this.onSignalingStateChanged_.bind(this);
    this.pc_.oniceconnectionstatechange = this.onIceConnectionStateChanged_.bind(this);
    window.dispatchEvent(new CustomEvent("pccreated", { detail: { pc: this, time: new Date, userId: this.params_.roomId + (this.isInitiator_ ? "-0" : "-1"), sessionId: this.params_.roomId } }));
    this.hasRemoteSdp_ = false;
    this.messageQueue_ = [];
    this.isInitiator_ = false;
    this.started_ = false;
    this.onerror = null;
    this.oniceconnectionstatechange = null;
    this.onnewicecandidate = null;
    this.onremotehangup = null;
    this.onremotesdpset = null;
    this.onremotestreamadded = null;
    this.onsignalingmessage = null;
    this.onsignalingstatechange = null;
};
PeerConnectionClient.DEFAULT_SDP_OFFER_OPTIONS_ = { offerToReceiveAudio: 1, offerToReceiveVideo: 1, voiceActivityDetection: false };
PeerConnectionClient.prototype.addStream = function (stream) {
    trace("PeerConnectionClient.prototype.addStream()");
    if (!this.pc_) {
        return;
    }
    this.pc_.addStream(stream);
};
PeerConnectionClient.prototype.startAsCaller = function (offerOptions) {
    trace("PeerConnectionClient.prototype.startAsCaller()");
    if (!this.pc_) {
        return false;
    }
    if (this.started_) {
        return false;
    }
    this.isInitiator_ = true;
    this.started_ = true;
    var constraints = mergeConstraints(PeerConnectionClient.DEFAULT_SDP_OFFER_OPTIONS_, offerOptions);
    trace("Sending offer to peer, with constraints: \n'" + JSON.stringify(constraints) + "'.");
    this.pc_.createOffer(constraints).then(this.setLocalSdpAndNotify_.bind(this)).catch(this.onError_.bind(this, "createOffer"));
    return true;
};
PeerConnectionClient.prototype.startAsCallee = function (initialMessages) {
    trace("PeerConnectionClient.prototype.startAsCallee()");
    if (!this.pc_) {
        return false;
    }
    if (this.started_) {
        return false;
    }
    this.isInitiator_ = false;
    this.started_ = true;
    if (initialMessages && initialMessages.length > 0) {
        for (var i = 0, len = initialMessages.length; i < len; i++) {
            this.receiveSignalingMessage(initialMessages[i]);
        }
        return true;
    }
    if (this.messageQueue_.length > 0) {
        this.drainMessageQueue_();
    }
    return true;
};
PeerConnectionClient.prototype.receiveSignalingMessage = function (message) {
    trace("PeerConnectionClient.prototype.receiveSignalingMessage()");
    var messageObj = parseJSON(message);
    if (!messageObj) {
        return;
    }
    if (messageObj.type === "answer" || messageObj.type === "offer") {
        this.hasRemoteSdp_ = true;
        this.messageQueue_.unshift(messageObj);
    } else {
        if (messageObj.type === "candidate") {
            this.messageQueue_.push(messageObj);
        } else {
            if (messageObj.type === "bye") {
                if (this.onremotehangup) {
                    this.onremotehangup();
                }
            }
        }
    }
    this.drainMessageQueue_();
};
PeerConnectionClient.prototype.close = function () {
    trace("PeerConnectionClient.prototype.close()");
    if (!this.pc_) {
        return;
    }
    this.pc_.close();
    window.dispatchEvent(new CustomEvent("pcclosed", { detail: { pc: this, time: new Date } }));
    this.pc_ = null;
};
PeerConnectionClient.prototype.getPeerConnectionStates = function () {
    trace("PeerConnectionClient.prototype.getPeerConnectionStates()");
    if (!this.pc_) {
        return null;
    }
    return { "signalingState": this.pc_.signalingState, "iceGatheringState": this.pc_.iceGatheringState, "iceConnectionState": this.pc_.iceConnectionState };
};
PeerConnectionClient.prototype.getPeerConnectionStats = function (callback) {
    trace("PeerConnectionClient.prototype.getPeerConnectionStats()");
    if (!this.pc_) {
        return;
    }
    this.pc_.getStats(null).then(callback);
};
PeerConnectionClient.prototype.doAnswer_ = function () {
    trace("PeerConnectionClient.prototype.doAnswer_()");
    trace("Sending answer to peer.");
    this.pc_.createAnswer().then(this.setLocalSdpAndNotify_.bind(this)).catch(this.onError_.bind(this, "createAnswer"));
};
PeerConnectionClient.prototype.setLocalSdpAndNotify_ = function (sessionDescription) {
    trace("PeerConnectionClient.prototype.setLocalSdpAndNotify_()");
    sessionDescription.sdp = maybeSetOpusOptions(sessionDescription.sdp, this.params_);
    sessionDescription.sdp = maybePreferAudioReceiveCodec(sessionDescription.sdp, this.params_);
    sessionDescription.sdp = maybePreferVideoReceiveCodec(sessionDescription.sdp, this.params_);
    sessionDescription.sdp = maybeSetAudioReceiveBitRate(sessionDescription.sdp, this.params_);
    sessionDescription.sdp = maybeSetVideoReceiveBitRate(sessionDescription.sdp, this.params_);
    sessionDescription.sdp = maybeRemoveVideoFec(sessionDescription.sdp, this.params_);
    this.pc_.setLocalDescription(sessionDescription).then(trace.bind(null, "Set session description success.")).catch(this.onError_.bind(this, "setLocalDescription"));
    if (this.onsignalingmessage) {
        this.onsignalingmessage({ sdp: sessionDescription.sdp, type: sessionDescription.type });
    }
};
PeerConnectionClient.prototype.setRemoteSdp_ = function (message) {
    trace("PeerConnectionClient.prototype.setRemoteSdp_()");
    message.sdp = maybeSetOpusOptions(message.sdp, this.params_);
    message.sdp = maybePreferAudioSendCodec(message.sdp, this.params_);
    message.sdp = maybePreferVideoSendCodec(message.sdp, this.params_);
    message.sdp = maybeSetAudioSendBitRate(message.sdp, this.params_);
    message.sdp = maybeSetVideoSendBitRate(message.sdp, this.params_);
    message.sdp = maybeSetVideoSendInitialBitRate(message.sdp, this.params_);
    message.sdp = maybeRemoveVideoFec(message.sdp, this.params_);
    this.pc_.setRemoteDescription(new RTCSessionDescription(message)).then(this.onSetRemoteDescriptionSuccess_.bind(this)).catch(this.onError_.bind(this, "setRemoteDescription"));
};
PeerConnectionClient.prototype.onSetRemoteDescriptionSuccess_ = function () {
    trace("PeerConnectionClient.prototype.onSetRemoteDescriptionSuccess_()");
    trace("Set remote session description success.");
    var remoteStreams = this.pc_.getRemoteStreams();
    if (this.onremotesdpset) {
        this.onremotesdpset(remoteStreams.length > 0 && remoteStreams[0].getVideoTracks().length > 0);
    }
};
PeerConnectionClient.prototype.processSignalingMessage_ = function (message) {
    trace("PeerConnectionClient.prototype.processSignalingMessage_()");
    if (message.type === "offer") {
        if (this.pc_.signalingState !== "stable") {
            trace("ERROR: remote offer received in unexpected state: " + this.pc_.signalingState);
            return;
        }
        this.setRemoteSdp_(message);
        this.doAnswer_();
    } else {
        if (message.type === "answer") {
            if (this.pc_.signalingState !== "have-local-offer") {
                trace("ERROR: remote answer received in unexpected state: " + this.pc_.signalingState);
                return;
            }
            this.setRemoteSdp_(message);
        } else {
            if (message.type === "candidate") {
                var candidate = new RTCIceCandidate({ sdpMLineIndex: message.sdpMLineIndex, candidate: message.candidate });
                this.recordIceCandidate_("Remote", candidate);
                this.pc_.addIceCandidate(candidate).then(trace.bind(null, "Remote candidate added successfully.")).catch(this.onError_.bind(this, "addIceCandidate"));
            } else {
                trace("WARNING: unexpected message: " + JSON.stringify(message));
            }
        }
    }
};
PeerConnectionClient.prototype.drainMessageQueue_ = function () {
    trace("PeerConnectionClient.prototype.drainMessageQueue_()");
    if (!this.pc_ || !this.started_ || !this.hasRemoteSdp_) {
        return;
    }
    for (var i = 0, len = this.messageQueue_.length; i < len; i++) {
        this.processSignalingMessage_(this.messageQueue_[i]);
    }
    this.messageQueue_ = [];
};
PeerConnectionClient.prototype.onIceCandidate_ = function (event) {
    trace("PeerConnectionClient.prototype.onIceCandidate_()");
    if (event.candidate) {
        if (this.filterIceCandidate_(event.candidate)) {
            var message = { type: "candidate", sdpMLineIndex: event.candidate.sdpMLineIndex, sdpMid: event.candidate.sdpMid, candidate: event.candidate.candidate };
            if (this.onsignalingmessage) {
                this.onsignalingmessage(message);
            }
            this.recordIceCandidate_("Local", event.candidate);
        }
    } else {
        trace("End of candidates.");
    }
};
PeerConnectionClient.prototype.onSignalingStateChanged_ = function () {
    trace("PeerConnectionClient.prototype.onSignalingStateChanged_()");
    if (!this.pc_) {
        return;
    }
    trace("Signaling state changed to: " + this.pc_.signalingState);
    if (this.onsignalingstatechange) {
        this.onsignalingstatechange();
    }
};
PeerConnectionClient.prototype.onIceConnectionStateChanged_ = function () {
    trace("PeerConnectionClient.prototype.onIceConnectionStateChanged_()");
    if (!this.pc_) {
        return;
    }
    trace("ICE connection state changed to: " + this.pc_.iceConnectionState);
    if (this.pc_.iceConnectionState === "completed") {
        trace("ICE complete time: " + (window.performance.now() - this.startTime_).toFixed(0) + "ms.");
    }
    if (this.oniceconnectionstatechange) {
        this.oniceconnectionstatechange();
    }
};
PeerConnectionClient.prototype.filterIceCandidate_ = function (candidateObj) {
    trace("PeerConnectionClient.prototype.filterIceCandidate_()");
    var candidateStr = candidateObj.candidate;
    if (candidateStr.indexOf("tcp") !== -1) {
        return false;
    }
    if (this.params_.peerConnectionConfig.iceTransports === "relay" && iceCandidateType(candidateStr) !== "relay") {
        return false;
    }
    return true;
};
PeerConnectionClient.prototype.recordIceCandidate_ = function (location, candidateObj) {
    trace("PeerConnectionClient.prototype.recordIceCandidate_()");
    if (this.onnewicecandidate) {
        this.onnewicecandidate(location, candidateObj.candidate);
    }
};
PeerConnectionClient.prototype.onRemoteStreamAdded_ = function (event) {
    trace("PeerConnectionClient.prototype.onRemoteStreamAdded_()");
    if (this.onremotestreamadded) {
        this.onremotestreamadded(event.streams[0]);
    }
};
PeerConnectionClient.prototype.onError_ = function (tag, error) {
    trace("PeerConnectionClient.prototype.onError_()");
    if (this.onerror) {
        this.onerror(tag + ": " + error.toString());
    }
};