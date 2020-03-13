 var SignalingChannel = function (wssUrl, wssPostUrl) {
    trace("SignalingChannel()");
    this.wssUrl_ = wssUrl;
    this.wssPostUrl_ = wssPostUrl;
    this.roomId_ = null;
    this.clientId_ = null;
    this.websocket_ = null;
    this.registered_ = false;
    this.onerror = null;
    this.onmessage = null;
     window.onbeforeunload = function () { this.websocket_.close(); }.bind(this);
};
SignalingChannel.prototype.open = function () {
    trace("SignalingChannel.prototype.open()");
    if (this.websocket_) {
        trace("ERROR: SignalingChannel has already opened.");
        return;
    }
    trace("Opening signaling channel.");
    return new Promise(function (resolve, reject) {
        this.websocket_ = new WebSocket("wss://demo1.carriergrade.in", "lws-minimal");
        this.websocket_.onopen = function () {
            trace("SignalingChannel.websocket_.onopen()");
            trace("Signaling channel opened.");
            this.websocket_.onerror = function (event) {
                trace("Signaling channel error with code:" + event.code + " reason:" + event.reason);
            };
            this.websocket_.onclose = function (event) {
                trace("SignalingChannel.websocket_.onclose()");
                trace("Channel closed with code:" + event.code + " reason:" + event.reason);
                this.websocket_ = null;
                this.registered_ = false;
            };
            if (this.clientId_ && this.roomId_) {
                this.register(this.roomId_, this.clientId_);
            }
            resolve();
        }.bind(this);
        this.websocket_.onmessage = function (event) {
            trace("SignalingChannel.websocket_.onmessage()");
            trace("WSS->C: " + event.data);
            this.onmessage(event.data);
/*
            var message = parseJSON(event.data);
            if (!message) {
                trace("Failed to parse WSS message: " + event.data);
                return;
            }
            if (message.error) {
                trace("Signaling server error message: " + message.error);
                return;
            }
 */           
        }.bind(this);
        this.websocket_.onerror = function () {
            trace("SignalingChannel.websocket_.onerror()");
            reject(Error("WebSocket error."));
        };
    }.bind(this));
};
SignalingChannel.prototype.register = function (roomId, clientId) {
    trace("SignalingChannel.prototype.register()");
    if (this.registered_) {
        trace("ERROR: SignalingChannel has already registered.");
        return;
    }
    this.roomId_ = roomId;
    this.clientId_ = clientId;
    if (!this.roomId_) {
        trace("ERROR: missing roomId.");
    }
    if (!this.clientId_) {
        trace("ERROR: missing clientId.");
    }
    if (!this.websocket_ || this.websocket_.readyState !== WebSocket.OPEN) {
        trace("WebSocket not open yet; saving the IDs to register later.");
        return;
    }
    trace("Registering signaling channel.");
    var registerMessage = { cmd: "register", roomid: this.roomId_, clientid: this.clientId_ };
    this.websocket_.send(JSON.stringify(registerMessage));
    this.registered_ = true;
    trace("Signaling channel registered.");
};
SignalingChannel.prototype.close = function (async) {
    trace("SignalingChannel.prototype.close()");
    if (this.websocket_) {
        this.websocket_.close();
        this.websocket_ = null;
    }
    if (!this.clientId_ || !this.roomId_) {
        return;
    }
    var path = this.getWssPostUrl();
    return sendUrlRequest("DELETE", path, async).catch(function (error) {
        trace("Error deleting web socket connection: " + error.message);
    }.bind(this)).then(function () {
        this.clientId_ = null;
        this.roomId_ = null;
        this.registered_ = false;
    }.bind(this));
};
SignalingChannel.prototype.send = function (message) {
    trace("SignalingChannel.prototype.send()");
    if (!this.roomId_ || !this.clientId_) {
        trace("ERROR: SignalingChannel has not registered.");
        return;
    }
    trace("C->WSS: " + message);
    var wssMessage = { cmd: "send", msg: message };
    if (this.websocket_ && this.websocket_.readyState === WebSocket.OPEN) {
        this.websocket_.send(message);
    } else {
        var path = this.getWssPostUrl();
        var xhr = new XMLHttpRequest;
        xhr.open("POST", path, true);
        xhr.send(wssMessage.msg);
    }
};
SignalingChannel.prototype.getWssPostUrl = function () {
    trace("SignalingChannel.prototype.getWssPostUrl()");
    return this.wssPostUrl_ + "/" + this.roomId_ + "/" + this.clientId_;
};
