var Call = function (params, onremotestreamremoved) {
    trace("Call()");
    this.params_ = params;
    this.roomServer_ = params.roomServer || "";
    this.channel_ = new SignalingChannel(params.wssUrl, params.wssPostUrl);
    this.channel_.onmessage = this.onRecvSignalingChannelMessage_.bind(this);
    this.onremotestreamremoved = onremotestreamremoved;
    this.pcClient_ = null;
    this.localStream_ = null;
    this.errorMessageQueue_ = [];
    this.startTime = null;
    this.oncallerstarted = null;
    this.onerror = null;
    this.oniceconnectionstatechange = null;
    this.onlocalstreamadded = null;
    this.onnewicecandidate = null;
    this.onremotehangup = null;
    this.onremotesdpset = null;
    this.onremotestreamadded = null;
    this.onsignalingstatechange = null;
    this.onstatusmessage = null;
    this.getMediaPromise_ = null;
    this.getIceServersPromise_ = null;
    this.requestMediaAndIceServers_();
};
Call.prototype.requestMediaAndIceServers_ = function () {
    trace("Call.prototype.requestMediaAndIceServers_()");
    this.getMediaPromise_ = this.maybeGetMedia_();
    this.getIceServersPromise_ = this.maybeGetIceServers_();
};
Call.prototype.isInitiator = function () {
    trace("Call.prototype.isInitiator()");
    return this.params_.isInitiator;
};
Call.prototype.start = function (roomId) {
    trace("Call.prototype.start()");
    this.connectToRoom_(roomId);
    if (this.params_.isLoopback) {
        setupLoopback(this.params_.wssUrl, roomId);
    }
};
Call.prototype.queueCleanupMessages_ = function () {
    trace("Call.prototype.queueCleanupMessages_()");
    apprtc.windowPort.sendMessage({ action: Constants.QUEUEADD_ACTION, queueMessage: { action: Constants.XHR_ACTION, method: "POST", url: this.getLeaveUrl_(), body: null } });
    apprtc.windowPort.sendMessage({ action: Constants.QUEUEADD_ACTION, queueMessage: { action: Constants.WS_ACTION, wsAction: Constants.WS_SEND_ACTION, data: JSON.stringify({ cmd: "send", msg: JSON.stringify({ type: "bye" }) }) } });
    apprtc.windowPort.sendMessage({ action: Constants.QUEUEADD_ACTION, queueMessage: { action: Constants.XHR_ACTION, method: "DELETE", url: this.channel_.getWssPostUrl(), body: null } });
};
Call.prototype.clearCleanupQueue_ = function () {
    trace("Call.prototype.clearCleanupQueue_()");
    apprtc.windowPort.sendMessage({ action: Constants.QUEUECLEAR_ACTION });
};
Call.prototype.restart = function () {
    trace("Call.prototype.restart()");
    this.requestMediaAndIceServers_();
    this.start(this.params_.previousRoomId);
};
Call.prototype.hangup = function (async) {
    trace("Call.prototype.hangup()");
    this.startTime = null;
    if (isChromeApp()) {
        this.clearCleanupQueue_();
    }
    if (this.localStream_) {
        if (typeof this.localStream_.getTracks === "undefined") {
            this.localStream_.stop();
        } else {
            this.localStream_.getTracks().forEach(function (track) {
                track.stop();
            });
        }
        this.localStream_ = null;
    }
    if (!this.params_.roomId) {
        return;
    }
    if (this.pcClient_) {
        this.pcClient_.close();
        this.pcClient_ = null;
    }
    var steps = [];
    steps.push({
        step: function () {
            trace("Call.prototype.step1()");
            var path = this.getLeaveUrl_();
            return sendUrlRequest("POST", path, async);
        }.bind(this), errorString: "Error sending /leave:"
    });
    steps.push({
        step: function () {
            trace("Call.prototype.step2()");
            this.channel_.send(JSON.stringify({ type: "bye" }));
        }.bind(this), errorString: "Error sending bye:"
    });
    steps.push({
        step: function () {
            trace("Call.prototype.step3()");
            return this.channel_.close(async);
        }.bind(this), errorString: "Error closing signaling channel:"
    });
    steps.push({
        step: function () {
            trace("Call.prototype.step4()");
            this.params_.previousRoomId = this.params_.roomId;
            this.params_.roomId = null;
            this.params_.clientId = null;
        }.bind(this), errorString: "Error setting params:"
    });
    if (async) {
        var errorHandler = function (errorString, error) {
            trace(errorString + " " + error.message);
        };
        var promise = Promise.resolve();
        for (var i = 0; i < steps.length; ++i) {
            promise = promise.then(steps[i].step).catch(errorHandler.bind(this, steps[i].errorString));
        }
        return promise;
    }
    var executeStep = function (executor, errorString) {
        trace("Call.prototype.executeStep()");
        try {
            executor();
        } catch (ex) {
            trace(errorString + " " + ex);
        }
    };
    for (var j = 0; j < steps.length; ++j) {
        executeStep(steps[j].step, steps[j].errorString);
    }
    if (this.params_.roomId !== null || this.params_.clientId !== null) {
        trace("ERROR: sync cleanup tasks did not complete successfully.");
    } else {
        trace("Cleanup completed.");
    }
    return Promise.resolve();
};
Call.prototype.getLeaveUrl_ = function () {
    trace("Call.prototype.getLeaveUrl_()");
    return this.roomServer_ + "/leave/" + this.params_.roomId + "/" + this.params_.clientId;
};
Call.prototype.onRemoteHangup = function () {
    trace("Call.prototype.onRemoteHangup()");
    this.startTime = null;
    this.params_.isInitiator = true;
    if (this.pcClient_) {
        this.pcClient_.close();
        this.pcClient_ = null;
    }
    this.startSignaling_();
};
Call.prototype.getPeerConnectionStates = function () {
    trace("Call.prototype.getPeerConnectionStates()");
    if (!this.pcClient_) {
        return null;
    }
    return this.pcClient_.getPeerConnectionStates();
};
Call.prototype.getPeerConnectionStats = function (callback) {
    trace("Call.prototype.getPeerConnectionStats()");
    if (!this.pcClient_) {
        return;
    }
    this.pcClient_.getPeerConnectionStats(callback);
};
Call.prototype.toggleVideoMute = function () {
    trace("Call.prototype.toggleVideoMute()");
    var videoTracks = this.localStream_.getVideoTracks();
    if (videoTracks.length === 0) {
        trace("No local video available.");
        return;
    }
    trace("Toggling video mute state.");
    for (var i = 0; i < videoTracks.length; ++i) {
        videoTracks[i].enabled = !videoTracks[i].enabled;
    }
    trace("Video " + (videoTracks[0].enabled ? "unmuted." : "muted."));
};
Call.prototype.toggleAudioMute = function () {
    trace("Call.prototype.toggleAudioMute()");
    var audioTracks = this.localStream_.getAudioTracks();
    if (audioTracks.length === 0) {
        trace("No local audio available.");
        return;
    }
    trace("Toggling audio mute state.");
    for (var i = 0; i < audioTracks.length; ++i) {
        audioTracks[i].enabled = !audioTracks[i].enabled;
    }
    trace("Audio " + (audioTracks[0].enabled ? "unmuted." : "muted."));
};
Call.prototype.connectToRoom_ = function (roomId) {
    trace("Call.prototype.connectToRoom_()");
    this.params_.roomId = roomId;
    var channelPromise = this.channel_.open().catch(function (error) {
        this.onError_("WebSocket open error: " + error.message);
        return Promise.reject(error);
    }.bind(this));
    var joinPromise = this.joinRoom_().then(function (roomParams) {
        trace("Call.prototype.joinRoom_().then()");
        this.params_.clientId = roomParams.client_id;
        this.params_.roomId = roomParams.room_id;
        this.params_.roomLink = roomParams.room_link;
        this.params_.isInitiator = roomParams.is_initiator === "true";
        this.params_.messages = roomParams.messages;
    }.bind(this)).catch(function (error) {
        this.onError_("Room server join error: " + error.message);
        return Promise.reject(error);
    }.bind(this));
    Promise.all([channelPromise, joinPromise]).then(function () {
        trace("Call.prototype.Promise.all([channelPromise, joinPromise]).then()");
        this.channel_.register(this.params_.roomId, this.params_.clientId);
        Promise.all([this.getIceServersPromise_, this.getMediaPromise_]).then(function () {
            this.startSignaling_();
            if (isChromeApp()) {
                this.queueCleanupMessages_();
            }
        }.bind(this)).catch(function (error) {
            this.onError_("Failed to start signaling: " + error.message);
        }.bind(this));
    }.bind(this)).catch(function (error) {
        this.onError_("WebSocket register error: " + error.message);
    }.bind(this));
};
Call.prototype.maybeGetMedia_ = function () {
    trace("Call.prototype.maybeGetMedia_()");
    var needStream = this.params_.mediaConstraints.audio !== false || this.params_.mediaConstraints.video !== false;
    var mediaPromise = null;
    if (needStream) {
        var mediaConstraints = this.params_.mediaConstraints;
        mediaPromise = navigator.mediaDevices.getUserMedia(mediaConstraints).catch(function (error) {
            if (error.name !== "NotFoundError") {
                throw error;
            }
            return navigator.mediaDevices.enumerateDevices().then(function (devices) {
                trace("Call.prototype.navigator.mediaDevices.enumerateDevices().then()");
                var cam = devices.find(function (device) {
                    return device.kind === "videoinput";
                });
                var mic = devices.find(function (device) {
                    return device.kind === "audioinput";
                });
                var constraints = { video: cam && mediaConstraints.video, audio: mic && mediaConstraints.audio };
                return navigator.mediaDevices.getUserMedia(constraints);
            });
        }).then(function (stream) {
            trace("Got access to local media with mediaConstraints:\n" + "  '" + JSON.stringify(mediaConstraints) + "'");
            this.onUserMediaSuccess_(stream);
        }.bind(this)).catch(function (error) {
            this.onError_("Error getting user media: " + error.message);
            this.onUserMediaError_(error);
        }.bind(this));
    } else {
        mediaPromise = Promise.resolve();
    }
    return mediaPromise;
};
Call.prototype.maybeGetIceServers_ = function () {
    trace("Call.prototype.maybeGetIceServers_()");
    var shouldRequestIceServers = this.params_.iceServerRequestUrl && this.params_.iceServerRequestUrl.length > 0 && this.params_.peerConnectionConfig.iceServers && this.params_.peerConnectionConfig.iceServers.length === 0;
    var iceServerPromise = null;
    if (shouldRequestIceServers) {
        var requestUrl = this.params_.iceServerRequestUrl;
        iceServerPromise = requestIceServers(requestUrl, this.params_.iceServerTransports).then(function (iceServers) {
            var servers = this.params_.peerConnectionConfig.iceServers;
            this.params_.peerConnectionConfig.iceServers = servers.concat(iceServers);
        }.bind(this)).catch(function (error) {
            if (this.onstatusmessage) {
                var subject = encodeURIComponent("AppRTC demo ICE servers not working");
                this.onstatusmessage("No TURN server; unlikely that media will traverse networks. " + "If this persists please " + '<a href="mailto:discuss-webrtc@googlegroups.com?' + "subject=" + subject + '">' + "report it to discuss-webrtc@googlegroups.com</a>.");
            }
            trace(error.message);
        }.bind(this));
    } else {
        iceServerPromise = Promise.resolve();
    }
    return iceServerPromise;
};
Call.prototype.onUserMediaSuccess_ = function (stream) {
    trace("Call.prototype.onUserMediaSuccess_()");
    this.localStream_ = stream;
    if (this.onlocalstreamadded) {
        this.onlocalstreamadded(stream);
    }
};
Call.prototype.onUserMediaError_ = function (error) {
    trace("Call.prototype.onUserMediaError_()");
    var errorMessage = "Failed to get access to local media. Error name was " + error.name + ". Continuing without sending a stream.";
    this.onError_("getUserMedia error: " + errorMessage);
    this.errorMessageQueue_.push(error);
    alert(errorMessage);
};
Call.prototype.maybeCreatePcClientAsync_ = function () {
    trace("Call.prototype.maybeCreatePcClientAsync_()");
    return new Promise(function (resolve, reject) {
        if (this.pcClient_) {
            resolve();
            return;
        }
        if (typeof RTCPeerConnection.generateCertificate === "function") {
            var certParams = { name: "ECDSA", namedCurve: "P-256" };
            RTCPeerConnection.generateCertificate(certParams).then(function (cert) {
                trace("ECDSA certificate generated successfully.");
                this.params_.peerConnectionConfig.certificates = [cert];
                this.createPcClient_();
                resolve();
            }.bind(this)).catch(function (error) {
                trace("ECDSA certificate generation failed.");
                reject(error);
            });
        } else {
            this.createPcClient_();
            resolve();
        }
    }.bind(this));
};
Call.prototype.createPcClient_ = function () {
    trace("Call.prototype.createPcClient_()");
    this.pcClient_ = new PeerConnectionClient(this.params_, this.startTime);
    this.pcClient_.onsignalingmessage = this.sendSignalingMessage_.bind(this);
    this.pcClient_.onremotehangup = this.onremotehangup;
    this.pcClient_.onremovetrack = this.onremovetrack;
    this.pcClient_.onremotesdpset = this.onremotesdpset;
    this.pcClient_.onremotestreamadded = this.onremotestreamadded;
    this.pcClient_.onsignalingstatechange = this.onsignalingstatechange;
    this.pcClient_.oniceconnectionstatechange = this.oniceconnectionstatechange;
    this.pcClient_.onnewicecandidate = this.onnewicecandidate;
    this.pcClient_.onerror = this.onerror;
    trace("Created PeerConnectionClient");
};
Call.prototype.startSignaling_ = function () {
    trace("Call.prototype.startSignaling_()");
    trace("Starting signaling.");
    if (this.oncallerstarted) {
        this.oncallerstarted(this.params_.roomId, this.params_.roomLink);
    }
    this.startTime = window.performance.now();
    this.maybeCreatePcClientAsync_().then(function () {
        trace("Call.prototype.maybeCreatePcClientAsync_().then()");
        if (this.localStream_) {
            trace("Adding local stream.");
            this.pcClient_.addStream(this.localStream_);
        }
        this.pcClient_.startAsCaller(this.params_.offerOptions);
    }.bind(this)).catch(function (e) {
        this.onError_("Create PeerConnection exception: " + e);
        alert("Cannot create RTCPeerConnection: " + e.message);
    }.bind(this));
};
Call.prototype.joinRoom_ = function () {
    trace("Call.prototype.joinRoom_()");
    return new Promise(function (resolve, reject) {
        if (!this.params_.roomId) {
            reject(Error("Missing room id."));
        }
        var roomParams = {
            client_id: randomId(),
            room_id: this.params_.roomId,
            room_link: '',
            is_initiator: 'true',
            messages: ''
        };
        resolve(roomParams);
    }.bind(this));
};
Call.prototype.onRecvSignalingChannelMessage_ = function (msg) {
    trace("Call.prototype.onRecvSignalingChannelMessage_()");
    this.maybeCreatePcClientAsync_().then(this.pcClient_.receiveSignalingMessage(msg));
};
Call.prototype.sendSignalingMessage_ = function (message) {
    trace("Call.prototype.sendSignalingMessage_()");
    var msgString = JSON.stringify(message);
    this.channel_.send(msgString);
};
Call.prototype.onError_ = function (message) {
    trace("Call.prototype.onError_()");
    if (this.onerror) {
        this.onerror(message);
    }
};