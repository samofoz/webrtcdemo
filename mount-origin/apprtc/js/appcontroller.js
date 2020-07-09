var UI_CONSTANTS = {
    confirmJoinButton: "#confirm-join-button", confirmJoinDiv: "#confirm-join-div", confirmJoinRoomSpan: "#confirm-join-room-span", fullscreenSvg: "#fullscreen", hangupSvg: "#hangup", icons: "#icons", infoDiv: "#info-div", videosDiv: "#videos", muteAudioSvg: "#mute-audio", muteVideoSvg: "#mute-video", shareScreenSvg: "#share-screen", newRoomButton: "#new-room-button", newRoomLink: "#new-room-link", rejoinButton: "#rejoin-button", rejoinDiv: "#rejoin-div",
    rejoinLink: "#rejoin-link", roomLinkHref: "#room-link-href", roomSelectionDiv: "#room-selection", roomSelectionInput: "#room-id-input", roomSelectionInputLabel: "#room-id-input-label", roomSelectionJoinButton: "#join-button", roomSelectionRandomButton: "#random-button", roomSelectionRecentList: "#recent-rooms-list", sharingDiv: "#sharing-div", statusDiv: "#status-div", videosDiv: "#videos"
};


var AppController = function (loadingParams) {
    trace("AppController()");
    trace("Initializing; server= " + loadingParams.roomServer + ".");
    trace("Initializing; room=" + loadingParams.roomId + ".");
    this.remoteVideos = new Map();
    this.remoteVideosDiv = $(UI_CONSTANTS.videosDiv);
    this.sharedView_ = true;
    this.hangupSvg_ = $(UI_CONSTANTS.hangupSvg);
    this.icons_ = $(UI_CONSTANTS.icons);
    this.sharingDiv_ = $(UI_CONSTANTS.sharingDiv);
    this.statusDiv_ = $(UI_CONSTANTS.statusDiv);
    this.videosDiv_ = $(UI_CONSTANTS.videosDiv);
    this.roomLinkHref_ = $(UI_CONSTANTS.roomLinkHref);
    this.rejoinDiv_ = $(UI_CONSTANTS.rejoinDiv);
    this.rejoinLink_ = $(UI_CONSTANTS.rejoinLink);
    this.newRoomLink_ = $(UI_CONSTANTS.newRoomLink);
    this.rejoinButton_ = $(UI_CONSTANTS.rejoinButton);
    this.newRoomButton_ = $(UI_CONSTANTS.newRoomButton);
    this.newRoomButton_.addEventListener("click", this.onNewRoomClick_.bind(this), false);
    this.rejoinButton_.addEventListener("click", this.onRejoinClick_.bind(this), false);
    this.muteAudioIconSet_ = new AppController.IconSet_(UI_CONSTANTS.muteAudioSvg);
    this.muteVideoIconSet_ = new AppController.IconSet_(UI_CONSTANTS.muteVideoSvg);
    this.shareScreenIconSet_ = new AppController.IconSet_(UI_CONSTANTS.shareScreenSvg);
    this.fullscreenIconSet_ = new AppController.IconSet_(UI_CONSTANTS.fullscreenSvg);
    this.loadingParams_ = loadingParams;
    this.loadUrlParams_();
    var paramsPromise = Promise.resolve({});
    if (this.loadingParams_.paramsFunction) {
        paramsPromise = this.loadingParams_.paramsFunction();
    }
    Promise.resolve(paramsPromise).then(function (newParams) {
        if (newParams) {
            Object.keys(newParams).forEach(function (key) {
                this.loadingParams_[key] = newParams[key];
            }.bind(this));
        }
        this.roomLink_ = "";
        this.roomSelection_ = null;
        this.localStream_ = null;
        this.remoteVideoResetTimer_ = null;
        if (this.loadingParams_.roomId) {
            this.createCall_();
            if (!RoomSelection.matchRandomRoomPattern(this.loadingParams_.roomId)) {
                $(UI_CONSTANTS.confirmJoinRoomSpan).textContent = ' "' + this.loadingParams_.roomId + '"';
            }
            var confirmJoinDiv = $(UI_CONSTANTS.confirmJoinDiv);
            this.show_(confirmJoinDiv);
            $(UI_CONSTANTS.confirmJoinButton).onclick = function () {
                this.hide_(confirmJoinDiv);
                var recentlyUsedList = new RoomSelection.RecentlyUsedList;
                recentlyUsedList.pushRecentRoom(this.loadingParams_.roomId);
                this.finishCallSetup_(this.loadingParams_.roomId);
            }.bind(this);
            if (this.loadingParams_.bypassJoinConfirmation) {
                $(UI_CONSTANTS.confirmJoinButton).onclick();
            }
        } else {
            this.showRoomSelection_();
        }
    }.bind(this)).catch(function (error) {
        trace("Error initializing: " + error.message);
    }.bind(this));
};
AppController.prototype.createCall_ = function () {
    trace("AppController.prototype.createCall_()");
    this.call_ = new Call(this.loadingParams_);
    this.call_.onremotehangup = this.onRemoteHangup_.bind(this);
    this.call_.onremotesdpset = this.onRemoteSdpSet_.bind(this);
    this.call_.onremotestreamadded = this.onRemoteStreamAdded_.bind(this);
    this.call_.onremovetrack = this.onRemoteStreamRemoved_.bind(this);
    this.call_.onerror = this.displayError_.bind(this);
    this.call_.onstatusmessage = this.displayStatus_.bind(this);
    this.call_.oncallerstarted = this.displaySharingInfo_.bind(this);
    document.onresize = this.onResize_.bind(this);
    window.onresize = this.onResize_.bind(this);
};
AppController.prototype.showRoomSelection_ = function () {
    trace("AppController.prototype.showRoomSelection_()");
/*
    var roomSelectionDiv = $(UI_CONSTANTS.roomSelectionDiv);
    this.roomSelection_ = new RoomSelection(roomSelectionDiv, UI_CONSTANTS);
    this.show_(roomSelectionDiv);
    this.roomSelection_.onRoomSelected = function (roomName) {
        trace("this.roomSelection_.onRoomSelected()");
        this.hide_(roomSelectionDiv);
        this.createCall_();
        this.finishCallSetup_(roomName);
        this.roomSelection_.removeEventListeners();
        this.roomSelection_ = null;
    }.bind(this);
*/
    trace("this.roomSelection_.onRoomSelected()");
    var roomSelectionDiv = $(UI_CONSTANTS.roomSelectionDiv);
    this.hide_(roomSelectionDiv);
    this.createCall_();
    this.finishCallSetup_("000000001");
    this.roomSelection_ = null;
};
AppController.prototype.setupUi_ = function () {
    trace("AppController.prototype.setupUi_()");
    this.iconEventSetup_();
    document.onkeypress = this.onKeyPress_.bind(this);
    window.onmousemove = this.showIcons_.bind(this);
    window.ontouchstart = this.showIcons_.bind(this);
    $(UI_CONSTANTS.muteAudioSvg).onclick = this.toggleAudioMute_.bind(this);
    $(UI_CONSTANTS.muteVideoSvg).onclick = this.toggleVideoMute_.bind(this);
    $(UI_CONSTANTS.shareScreenSvg).onclick = this.toggleShareScreen_.bind(this);
    $(UI_CONSTANTS.fullscreenSvg).onclick = this.toggleFullScreen_.bind(this);
    $(UI_CONSTANTS.hangupSvg).onclick = this.hangup_.bind(this);
    setUpFullScreen();
};
AppController.prototype.finishCallSetup_ = function (roomId) {
    trace("AppController.prototype.finishCallSetup_()");
    this.call_.start(roomId);
    this.setupUi_();
    if (!isChromeApp()) {
        window.onbeforeunload = function () {
            this.call_.hangup(false);
        }.bind(this);
        window.onpopstate = function (event) {
            if (!event.state) {
                trace("Reloading main page.");
                location.href = location.origin;
            } else {
                if (event.state.roomLink) {
                    location.href = event.state.roomLink;
                }
            }
        };
    }
};
AppController.prototype.hangup_ = function () {
    trace("AppController.prototype.hangup_()");
    trace("Hanging up.");
    this.hide_(this.icons_);
    this.displayStatus_("Hanging up");
    this.transitionToDone_();
    this.call_.hangup(true);
    document.onkeypress = null;
    window.onmousemove = null;
};
AppController.prototype.onRemoteHangup_ = function () {
    trace("AppController.prototype.onRemoteHangup_()");
    this.displayStatus_("The remote side hung up.");
    this.transitionToWaiting_();
    this.call_.onRemoteHangup();
};
AppController.prototype.onRemoteSdpSet_ = function (hasRemoteVideo) {
    trace("AppController.prototype.onRemoteSdpSet_()");
    if (hasRemoteVideo) {
        trace("Waiting for remote video.");
        this.waitForRemoteVideo_();
    } else {
        trace("No remote video stream; not waiting for media to arrive.");
        this.transitionToActive_();
    }
};
AppController.prototype.waitForRemoteVideo_ = function () {
    trace("AppController.prototype.waitForRemoteVideo_()");
    for (let remoteVideo of this.remoteVideos.values()) {
        if (remoteVideo.readyState >= 2) {
            trace("Remote video started; currentTime: " + remoteVideo.currentTime);
            this.transitionToActive_(remoteVideo);
        } else {
            remoteVideo.oncanplay = this.waitForRemoteVideo_.bind(this);
        }
    }
};
AppController.prototype.onResize_ = function () {

    /* Recalculate video positions */
    var i = 0;
    var screensharepresent = 0;

    /* Count number of screenshares 
    var screensharecount = 0;
    remoteVideos.forEach(function (value, key, map) {
        if (key.id.includes("screenshare")) {
            ++screensharecount;
        }
    });

    if (screensharecount) {
        remoteVideos.forEach(function (value, key, map) {
            if (key.id.includes("screenshare")) {
                value.style.height = '100%';
                value.style.width = 100 / screensharecount + '%';
                value.style.maxheight = '100%';
                value.style.maxwidth = 100 / screensharecount + '%';
                value.style.left = Math.floor(parseFloat(i * (window.innerWidth / screensharecount))) + 'px';
                value.style.top = '0px';
                value.style.border = '10px';
                ++i;
                this.transitionToActive_(value);
            }
        }.bind(this));
    } else {
        remoteVideos.forEach(function (value, key, map) {
            value.style.height = '100%';
            value.style.width = 100 / remoteVideos.size + '%';
            value.style.maxheight = '100%';
            value.style.maxwidth = 100 / remoteVideos.size + '%';
            value.style.left = Math.floor(parseFloat(i * (window.innerWidth / remoteVideos.size))) + 'px';
            value.style.top = '0px';
            value.style.border = '10px';
            ++i;
            this.transitionToActive_(value);
        }.bind(this));
    }
    */
    if (this.sharedView_ === false) {
        this.remoteVideos.forEach(function (value, key, map) {
            if (this.sharedViewStream_ === value) {
                value.style.height = '100%';
                value.style.width = '100%';
                value.style.maxheight = '100%';
                value.style.maxwidth = '100%';
                value.style.left = '0px';
                value.style.top = '0px';
                value.style.border = '10px';
            } else {
                value.style.height = '0px';
                value.style.width = '0px';
                value.style.left = '0px';
                value.style.top = '0px';
            }
        }.bind(this));
    } else {
        this.remoteVideos.forEach(function (value, key, map) {
            if (key.id.includes("screenshare")) {
                this.sharedViewStream_ = value;
                ++screensharepresent;
            }
            if (window.innerWidth > window.innerHeight) {
                value.style.height = '100%';
                value.style.width = 100 / this.remoteVideos.size + '%';
                value.style.maxheight = '100%';
                value.style.maxwidth = 100 / this.remoteVideos.size + '%';
                value.style.left = Math.floor(parseFloat(i * (window.innerWidth / this.remoteVideos.size))) + 'px';
                value.style.top = '0px';
                value.style.border = '10px';
                value.style.zindex = 100;
            } else {
                value.style.width = '100%';
                value.style.height = 100 / this.remoteVideos.size + '%';
                value.style.maxwidth = '100%';
                value.style.maxheight = 100 / this.remoteVideos.size + '%';
                value.style.top = Math.floor(parseFloat(i * (window.innerHeight / this.remoteVideos.size))) + 'px';
                value.style.left = '0px';
                value.style.border = '10px';
            }
            ++i;
            this.transitionToActive_(value);

        }.bind(this));
    }
    if (screensharepresent === 0 && this.remoteVideos.size === 1) {
        this.displaySharingInfo_();
    } else {
        this.deactivate_(this.sharingDiv_);
    }
    return;
};

AppController.prototype.dblClickStream_ = function (stream) {
    trace("AppController.prototype.dblClickStream_()");
    this.sharedViewStream_ = stream;
    this.sharedView_ = !this.sharedView_;
    this.onResize_.call(this);
}

AppController.prototype.addStream_ = function (stream) {
    trace("AppController.prototype.addStream_()");
    if (this.remoteVideos.has(stream) === true) {
        return false;
    }

    var divto = document.getElementById("videos");
    var obj = document.createElement("video");
    if (obj !== null) {
        obj.id = stream.id;
        obj.autoplay = true;
        obj.setAttribute('playsinline', 'playsinline');
        obj.classList = "remote-video";
        if (stream.id.includes("loopback")) {
            obj.muted = true;
        } else {
            obj.muted = false;
        }
        divto.appendChild(obj);
        this.remoteVideos.set(stream, obj);
        obj.srcObject = stream;
        obj.ondblclick = this.dblClickStream_.bind(this, obj);

        /* Recalculate video positions */
        this.onResize_.call(this);
        return true;
    } else {
        return false;
    }
};
AppController.prototype.removeStream_ = function (stream) {
    trace("AppController.prototype.removeStream_()");
    var divto = document.getElementById("videos");
    var obj = this.remoteVideos.get(stream);
    if (obj !== null) {
        divto.removeChild(obj);
        this.remoteVideos.delete(stream);

        /* Recalculate video positions */
        this.onResize_.call(this);
        return true;
    } else {
        return false;
    }
};

AppController.prototype.onRemoteStreamAdded_ = function (stream) {
    trace("AppController.prototype.onRemoteStreamAdded_()");
    trace("Remote stream added.");
    this.addStream_(stream);
 
    if (this.remoteVideoResetTimer_) {
        clearTimeout(this.remoteVideoResetTimer_);
        this.remoteVideoResetTimer_ = null;
    }
};
AppController.prototype.onRemoteStreamRemoved_ = function (streamid) {
    trace("AppController.prototype.onRemoteStreamRemoved_()");
    trace("Remote stream removed:" + streamid);
    for (let stream of this.remoteVideos.keys()) {
        if (stream.id === streamid) {
            this.removeStream_(stream);
            return;
        }
    }
};
AppController.prototype.transitionToActive_ = function (video) {
    trace("AppController.prototype.transitionToActive_()" + video);

    if (video !== undefined) {
        video.oncanplay = undefined;
        this.activate_(video);
    } else {
        var connectTime = window.performance.now();
        trace("Call setup time: " + (connectTime - this.call_.startTime).toFixed(0) + "ms.");
        for (let remoteVideo of this.remoteVideos.values()) {
            remoteVideo.oncanplay = undefined;
            this.activate_(remoteVideo);
        }
    }
    this.activate_(this.videosDiv_);
    this.show_(this.icons_);
    this.show_(this.hangupSvg_);
    this.displayStatus_("");
};
AppController.prototype.transitionToWaiting_ = function () {
    trace("AppController.prototype.transitionToWaiting_()");
    this.hide_(this.hangupSvg_);
    this.deactivate_(this.videosDiv_);
    if (!this.remoteVideoResetTimer_) {
        this.remoteVideoResetTimer_ = setTimeout(function () {
            this.remoteVideoResetTimer_ = null;
            trace("Resetting remoteVideo src after transitioning to waiting.");
            for (let remoteVideo of this.remoteVideos.values()) {
                remoteVideo.srcObject = null;
            }
        }.bind(this), 800);
    }
    this.remoteVideos.forEach(function (value2, value, set) {
        value2.oncanplay = undefined;
        this.deactivate_(value2);
    }, this);
};
AppController.prototype.transitionToDone_ = function () {
    trace("AppController.prototype.transitionToDone_()");
    for (let remoteVideo of this.remoteVideos.values()) {
        remoteVideo.oncanplay = undefined;
        this.deactivate_(remoteVideo);
    }
    this.hide_(this.hangupSvg_);
    this.activate_(this.rejoinDiv_);
    this.show_(this.rejoinDiv_);
    this.videosDiv_.innerHTML = '';
    this.remoteVideos.clear();
    this.displayStatus_("");
};
AppController.prototype.onRejoinClick_ = function () {
    trace("AppController.prototype.onRejoinClick_()");
    this.deactivate_(this.rejoinDiv_);
    this.hide_(this.rejoinDiv_);
    this.call_.restart();
    this.setupUi_();
};
AppController.prototype.onNewRoomClick_ = function () {
    trace("AppController.prototype.onNewRoomClick_()");
    this.deactivate_(this.rejoinDiv_);
    this.hide_(this.rejoinDiv_);
    this.showRoomSelection_();
};
AppController.prototype.onKeyPress_ = function (event) {
    trace("AppController.prototype.onKeyPress_()");
    switch (String.fromCharCode(event.charCode)) {
        case " ":
        case "m":
            if (this.call_) {
                this.call_.toggleAudioMute();
                this.muteAudioIconSet_.toggle();
            }
            return false;
        case "c":
            if (this.call_) {
                this.call_.toggleVideoMute();
                this.muteVideoIconSet_.toggle();
            }
            return false;
        case "s":
            if (this.call_) {
                this.call_.toggleShareScreen(this.shareScreenIconSet_);
            }
            return false;
        case "f":
            this.toggleFullScreen_();
            return false;
        case "q":
            this.hangup_();
            return false;
        default:
            return;
    }
};
AppController.prototype.pushCallNavigation_ = function (roomId, roomLink) {
    trace("AppController.prototype.pushCallNavigation_()");
    if (!isChromeApp()) {
        window.history.pushState({ "roomId": roomId, "roomLink": roomLink }, roomId, roomLink);
    }
};
AppController.prototype.displaySharingInfo_ = function (roomId, roomLink) {
    trace("AppController.prototype.displaySharingInfo_()");
    this.roomLinkHref_.href = document.URL;
    this.roomLinkHref_.text = "Open Another Window";
    this.roomLink_ = roomLink;
    //this.pushCallNavigation_(roomId, roomLink);
    this.activate_(this.sharingDiv_);
};
AppController.prototype.displayStatus_ = function (status) {
    trace("AppController.prototype.displayStatus_()");
    if (status === "") {
        this.deactivate_(this.statusDiv_);
    } else {
        this.activate_(this.statusDiv_);
    }
    this.statusDiv_.innerHTML = status;
};
AppController.prototype.displayError_ = function (error) {
    trace("AppController.prototype.displayError_()");
    trace(error);
};
AppController.prototype.toggleAudioMute_ = function () {
    trace("AppController.prototype.toggleAudioMute_()");
    this.call_.toggleAudioMute();
    this.muteAudioIconSet_.toggle();
};
AppController.prototype.toggleVideoMute_ = function () {
    trace("AppController.prototype.toggleVideoMute_()");
    this.call_.toggleVideoMute();
    this.muteVideoIconSet_.toggle();
};
AppController.prototype.toggleShareScreen_ = function () {
    trace("AppController.prototype.toggleShareScreen_()");
    this.call_.toggleShareScreen(this.shareScreenIconSet_).then(function () { console.log("toggleShareScreen success"); }.bind(this)).catch(function (e) { console.log(e); });
};
AppController.prototype.toggleFullScreen_ = function () {
    trace("AppController.prototype.toggleFullScreen_()");
    if (isFullScreen()) {
        trace("Exiting fullscreen.");
        document.querySelector("svg#fullscreen title").textContent = "Enter fullscreen";
        document.cancelFullScreen();
    } else {
        trace("Entering fullscreen.");
        document.querySelector("svg#fullscreen title").textContent = "Exit fullscreen";
        document.body.requestFullScreen();
    }
    this.fullscreenIconSet_.toggle();
};
AppController.prototype.hide_ = function (element) {
    trace("AppController.prototype.hide_()");
    element.classList.add("hidden");
};
AppController.prototype.show_ = function (element) {
    trace("AppController.prototype.show_()");
    element.classList.remove("hidden");
};
AppController.prototype.activate_ = function (element) {
    trace("AppController.prototype.activate_()");
    element.classList.add("active");
};
AppController.prototype.deactivate_ = function (element) {
    trace("AppController.prototype.deactivate_()");
    element.classList.remove("active");
};
AppController.prototype.showIcons_ = function () {
    trace("AppController.prototype.showIcons_()");
    if (!this.icons_.classList.contains("active")) {
        this.activate_(this.icons_);
        this.setIconTimeout_();
    }
};
AppController.prototype.hideIcons_ = function () {
    trace("AppController.prototype.hideIcons_()");
    if (this.icons_.classList.contains("active")) {
        this.deactivate_(this.icons_);
    }
};
AppController.prototype.setIconTimeout_ = function () {
    trace("AppController.prototype.setIconTimeout_()");
    if (this.hideIconsAfterTimeout) {
        window.clearTimeout.bind(this, this.hideIconsAfterTimeout);
    }
    this.hideIconsAfterTimeout = window.setTimeout(function () {
        this.hideIcons_();
    }.bind(this), 5000);
};
AppController.prototype.iconEventSetup_ = function () {
    trace("AppController.prototype.iconEventSetup_()");
    this.icons_.onmouseenter = function () {
        window.clearTimeout(this.hideIconsAfterTimeout);
    }.bind(this);
    this.icons_.onmouseleave = function () {
        this.setIconTimeout_();
    }.bind(this);
};
AppController.prototype.loadUrlParams_ = function () {
    trace("AppController.prototype.loadUrlParams_()");
    var DEFAULT_VIDEO_CODEC = "VP9";
    var urlParams = queryStringToDictionary(window.location.search);
    this.loadingParams_.audioSendBitrate = urlParams["asbr"];
    this.loadingParams_.audioSendCodec = urlParams["asc"];
    this.loadingParams_.audioRecvBitrate = urlParams["arbr"];
    this.loadingParams_.audioRecvCodec = urlParams["arc"];
    this.loadingParams_.opusMaxPbr = urlParams["opusmaxpbr"];
    this.loadingParams_.opusFec = urlParams["opusfec"];
    this.loadingParams_.opusDtx = urlParams["opusdtx"];
    this.loadingParams_.opusStereo = urlParams["stereo"];
    this.loadingParams_.videoSendBitrate = urlParams["vsbr"];
    this.loadingParams_.videoSendInitialBitrate = urlParams["vsibr"];
    this.loadingParams_.videoSendCodec = urlParams["vsc"];
    this.loadingParams_.videoRecvBitrate = urlParams["vrbr"];
    this.loadingParams_.videoRecvCodec = urlParams["vrc"] || DEFAULT_VIDEO_CODEC;
    this.loadingParams_.videoFec = urlParams["videofec"];
};
AppController.IconSet_ = function (iconSelector) {
    trace("AppController.IconSet_()");
    this.iconElement = document.querySelector(iconSelector);
};
AppController.IconSet_.prototype.toggle = function () {
    trace("AppController.IconSet_.prototype.toggle()");
    if (this.iconElement.classList.contains("on")) {
        this.iconElement.classList.remove("on");
    } else {
        this.iconElement.classList.add("on");
    }
};