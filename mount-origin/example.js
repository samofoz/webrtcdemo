var localVideo;
var remoteVideo1;
var remoteVideo2;
var remoteVideo3;
var head = 0, tail = 0, ring = new Array();
let localStream;
let pc1;
let sdpOffer;
const offerOptions = {
    offerToReceiveAudio: 1,
    offerToReceiveVideo: 1
};



function get_appropriate_ws_url(extra_url) {
    var pcol;
    var u = document.URL;

	/*
	 * We open the websocket encrypted if this page came on an
	 * https:// url itself, otherwise unencrypted
	 */

    if (u.substring(0, 5) === "https") {
        pcol = "wss://";
        u = u.substr(8);
    } else {
        pcol = "ws://";
        if (u.substring(0, 4) === "http")
            u = u.substr(7);
    }

    u = u.split("/");

    /* + "/xxx" bit is for IE10 workaround */

    return pcol + u[0] + "/" + extra_url;
}

function new_ws(urlpath, protocol) {
    if (typeof MozWebSocket != "undefined")
        return new MozWebSocket(urlpath, protocol);

    return new WebSocket(urlpath, protocol);
}

document.addEventListener("DOMContentLoaded", async function () {

    ws = new_ws(get_appropriate_ws_url(""), "lws-minimal");
    try {
        ws.onopen = function () {
            localVideo = document.getElementById('localVideo');
            remoteVideo1 = document.getElementById('remoteVideo1');
            remoteVideo2 = document.getElementById('remoteVideo2');
            remoteVideo3 = document.getElementById('remoteVideo3');

            localVideo.addEventListener('loadedmetadata', function () {
                console.log(`Local video videoWidth: ${this.videoWidth}px,  videoHeight: ${this.videoHeight}px`);
            });

            remoteVideo1.addEventListener('loadedmetadata', function () {
                console.log(`Remote video 1 videoWidth: ${this.videoWidth}px,  videoHeight: ${this.videoHeight}px`);
            });
            remoteVideo2.addEventListener('loadedmetadata', function () {
                console.log(`Remote video 2 videoWidth: ${this.videoWidth}px,  videoHeight: ${this.videoHeight}px`);
            });
            remoteVideo3.addEventListener('loadedmetadata', function () {
                console.log(`Remote video 3 videoWidth: ${this.videoWidth}px,  videoHeight: ${this.videoHeight}px`);
            });

            document.getElementById("r").disabled = 0;

            try {
                const stream = navigator.mediaDevices.getUserMedia({ audio: true, video: true, echoCancellation: false })
                    .then(function (stream) {
                        console.log('Received local stream');
                        localVideo.srcObject = stream;
                        localStream = stream;

                        const videoTracks = localStream.getVideoTracks();
                        const audioTracks = localStream.getAudioTracks();
                        if (videoTracks.length > 0) {
                            console.log(`Using video device: ${videoTracks[0].label}`);
                        }
                        if (audioTracks.length > 0) {
                            console.log(`Using audio device: ${audioTracks[0].label}`);
                        }
                        const configuration = {
                            sdpSemantics: "unified-plan", iceServers: [
                                {
                                    urls: "stun:stun.l.google.com:19302"
                                },
                                {
                                    url: 'turn:numb.viagenie.ca',
                                    credential: 'muazkh',
                                    username: 'webrtc@live.com'
                                }
                            ]
                        };
                        console.log('RTCPeerConnection configuration:', configuration);
                        pc1 = new RTCPeerConnection(configuration);
                        console.log('Created local peer connection object pc1');
                        pc1.addEventListener('icecandidate', e => onIceCandidate(pc1, e));
                        pc1.addEventListener('iceconnectionstatechange', e => onIceStateChange(pc1, e));
                        pc1.addEventListener('track', gotRemoteStream);
                        pc1.addEventListener('addstream', onAddStream);
                        localStream.getTracks().forEach(track => pc1.addTrack(track, localStream));
                        console.log('Added local stream to pc1');


                        console.log('pc1 createOffer start');
                        const offer = pc1.createOffer(offerOptions)
                            .then(function (offer) {
                                pc1.setLocalDescription(offer)
                                    .then(function () {
                                        console.log(`setLocalDescription complete ${sdpOffer.toString()}`);
                                    })
                                console.log(`createOffer complete ${offer.toString()}`);
                                sdpOffer = offer;

                                var n, s = "";
                                ring[head] = "Sent: [" + JSON.stringify(offer) + "]\n";
                                head = (head + 1) % 50;
                                if (tail === head)
                                    tail = (tail + 1) % 50;

                                n = tail;
                                do {
                                    s = s + ring[n];
                                    n = (n + 1) % 50;
                                } while (n !== head);

                                document.getElementById("r").value = s;
                                document.getElementById("r").scrollTop = document.getElementById("r").scrollHeight;
                                ws.send(JSON.stringify(offer));
                            })
                    })

            } catch (e) {
                alert(`getUserMedia() error: ${e.name} ${e.message}`);
            }
        };

        ws.onmessage = function got_packet(msg) {
            var n, s = "";

            ring[head] = "Recd: [" + msg.data + "]\n";
            head = (head + 1) % 50;
            if (tail === head)
                tail = (tail + 1) % 50;

            n = tail;
            do {
                s = s + ring[n];
                n = (n + 1) % 50;
            } while (n !== head);

            document.getElementById("r").value = s;
            document.getElementById("r").scrollTop = document.getElementById("r").scrollHeight;

            try {
                var message = JSON.parse(msg.data);
                if (message.sdp) {
                    console.log('Setting setremotedescription');
                    pc1.setRemoteDescription(new RTCSessionDescription(message), function () {
                        console.log('setremotedescription done');
                        if (message.type === "offer") {
                            console.log('createAnswer ...');
                            pc1.createAnswer().then(function (answer) {
                                console.log('createAnswer done');
                                var n, s = "";
                                ring[head] = "Sent: [" + JSON.stringify(answer) + "]\n";
                                head = (head + 1) % 50;
                                if (tail === head)
                                    tail = (tail + 1) % 50;

                                n = tail;
                                do {
                                    s = s + ring[n];
                                    n = (n + 1) % 50;
                                } while (n !== head);
                                ws.send(JSON.stringify(answer));
                                console.log('Answer sent');
                                pc1.setLocalDescription(answer, function () { console.log('setLocalDescription() done'); });

                            })
                                .catch(function (error) { console.log('createAnswer error'); });
                        }
                    });

                }
                else if (message.candidate) {
                    console.log('pc1.addIceCandidate');
                    pc1.addIceCandidate(new RTCIceCandidate(message))
                        .then(function (offer) {
                            console.log(`pc1.addIceCandidate complete`);
                        })
                }

            } catch (e) {
                console.log(`JSON.parse() error: ${e.name} ${e.message}`);
            }
        };

        ws.onclose = function () {
            document.getElementById("r").disabled = 1;
        };
    } catch (exception) {
        alert("<p>Error " + exception);
    }

}, false);


async function onIceCandidate(pc, event) {
    try {
        if (event.candidate) {
            var n, s = "";
            ring[head] = "Sent: [" + JSON.stringify(event.candidate) + "]\n";
            head = (head + 1) % 50;
            if (tail === head)
                tail = (tail + 1) % 50;

            n = tail;
            do {
                s = s + ring[n];
                n = (n + 1) % 50;
            } while (n !== head);
            ws.send(JSON.stringify(event.candidate));
        } else {
            ws.send('{"sdpMid":"", "sdpMLineIndex":0, "candidate":""}');
        }
        //await (getOtherPc(pc).addIceCandidate(event.candidate));
        console.log(`addIceCandidate success`);
    } catch (e) {
        console.log(`failed to add ICE Candidate: ${error.toString()}`);
    }
    console.log(`ICE candidate:\n${event.candidate ? event.candidate.sdpMLineIndex : '(null)'}`);
}

function onIceStateChange(pc, event) {
    if (pc) {
        console.log(`ICE state: ${pc.iceConnectionState}`);
        console.log('ICE state change event: ', event);
    }
}

function gotRemoteStream(e) {
    console.log('gotRemoteStream ' + e.streams[0] + e.streams[1] + e.streams[2] + e.streams[3]);
    if (!remoteVideo1.srcObject) {
        remoteVideo1.srcObject = e.streams[0];
        console.log('pc1 received remote stream 1');
        return;
    }
    if (!remoteVideo2.srcObject && remoteVideo1.srcObject !== e.streams[0]) {
        remoteVideo2.srcObject = e.streams[0];
        console.log('pc1 received remote stream 2');
        return;
    }

    if (!remoteVideo3.srcObject && remoteVideo1.srcObject !== e.streams[0] && remoteVideo2.srcObject !== e.streams[0]) {
        remoteVideo3.srcObject = e.streams[0];
        console.log('pc1 received remote stream 3');
        return;
    }

}

function onAddStream(stream) {
    console.log('onAddStream ' + stream);
}