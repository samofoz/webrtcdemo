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

function mergeConstraints(cons1, cons2) {
    if (!cons1 || !cons2) {
        return cons1 || cons2;
    }
    var merged = cons1;
    for (var key in cons2) {
        merged[key] = cons2[key];
    }
    return merged;
}
function iceCandidateType(candidateStr) {
    return candidateStr.split(" ")[7];
}
function maybeSetOpusOptions(sdp, params) {
    if (params.opusStereo === "true") {
        sdp = setCodecParam(sdp, "opus/48000", "stereo", "1");
    } else {
        if (params.opusStereo === "false") {
            sdp = removeCodecParam(sdp, "opus/48000", "stereo");
        }
    }
    if (params.opusFec === "true") {
        sdp = setCodecParam(sdp, "opus/48000", "useinbandfec", "1");
    } else {
        if (params.opusFec === "false") {
            sdp = removeCodecParam(sdp, "opus/48000", "useinbandfec");
        }
    }
    if (params.opusDtx === "true") {
        sdp = setCodecParam(sdp, "opus/48000", "usedtx", "1");
    } else {
        if (params.opusDtx === "false") {
            sdp = removeCodecParam(sdp, "opus/48000", "usedtx");
        }
    }
    if (params.opusMaxPbr) {
        sdp = setCodecParam(sdp, "opus/48000", "maxplaybackrate", params.opusMaxPbr);
    }
    return sdp;
}
function maybeSetAudioSendBitRate(sdp, params) {
    if (!params.audioSendBitrate) {
        return sdp;
    }
    trace("Prefer audio send bitrate: " + params.audioSendBitrate);
    return preferBitRate(sdp, params.audioSendBitrate, "audio");
}
function maybeSetAudioReceiveBitRate(sdp, params) {
    if (!params.audioRecvBitrate) {
        return sdp;
    }
    trace("Prefer audio receive bitrate: " + params.audioRecvBitrate);
    return preferBitRate(sdp, params.audioRecvBitrate, "audio");
}
function maybeSetVideoSendBitRate(sdp, params) {
    if (!params.videoSendBitrate) {
        return sdp;
    }
    trace("Prefer video send bitrate: " + params.videoSendBitrate);
    return preferBitRate(sdp, params.videoSendBitrate, "video");
}
function maybeSetVideoReceiveBitRate(sdp, params) {
    if (!params.videoRecvBitrate) {
        return sdp;
    }
    trace("Prefer video receive bitrate: " + params.videoRecvBitrate);
    return preferBitRate(sdp, params.videoRecvBitrate, "video");
}
function preferBitRate(sdp, bitrate, mediaType) {
    var sdpLines = sdp.split("\r\n");
    var mLineIndex = findLine(sdpLines, "m=", mediaType);
    if (mLineIndex === null) {
        trace("Failed to add bandwidth line to sdp, as no m-line found");
        return sdp;
    }
    var nextMLineIndex = findLineInRange(sdpLines, mLineIndex + 1, -1, "m=");
    if (nextMLineIndex === null) {
        nextMLineIndex = sdpLines.length;
    }
    var cLineIndex = findLineInRange(sdpLines, mLineIndex + 1, nextMLineIndex, "c=");
    if (cLineIndex === null) {
        trace("Failed to add bandwidth line to sdp, as no c-line found");
        return sdp;
    }
    var bLineIndex = findLineInRange(sdpLines, cLineIndex + 1, nextMLineIndex, "b=AS");
    if (bLineIndex) {
        sdpLines.splice(bLineIndex, 1);
    }
    var bwLine = "b=AS:" + bitrate;
    sdpLines.splice(cLineIndex + 1, 0, bwLine);
    sdp = sdpLines.join("\r\n");
    return sdp;
}
function maybeSetVideoSendInitialBitRate(sdp, params) {
    var initialBitrate = parseInt(params.videoSendInitialBitrate);
    if (!initialBitrate) {
        return sdp;
    }
    var maxBitrate = parseInt(initialBitrate);
    var bitrate = parseInt(params.videoSendBitrate);
    if (bitrate) {
        if (initialBitrate > bitrate) {
            trace("Clamping initial bitrate to max bitrate of " + bitrate + " kbps.");
            initialBitrate = bitrate;
            params.videoSendInitialBitrate = initialBitrate;
        }
        maxBitrate = bitrate;
    }
    var sdpLines = sdp.split("\r\n");
    var mLineIndex = findLine(sdpLines, "m=", "video");
    if (mLineIndex === null) {
        trace("Failed to find video m-line");
        return sdp;
    }
    var videoMLine = sdpLines[mLineIndex];
    var pattern = new RegExp("m=video\\s\\d+\\s[A-Z/]+\\s");
    var sendPayloadType = videoMLine.split(pattern)[1].split(" ")[0];
    var fmtpLine = sdpLines[findLine(sdpLines, "a=rtpmap", sendPayloadType)];
    var codecName = fmtpLine.split("a=rtpmap:" + sendPayloadType)[1].split("/")[0];
    var codec = params.videoSendCodec || codecName;
    sdp = setCodecParam(sdp, codec, "x-google-min-bitrate", params.videoSendInitialBitrate.toString());
    sdp = setCodecParam(sdp, codec, "x-google-max-bitrate", maxBitrate.toString());
    return sdp;
}
function removePayloadTypeFromMline(mLine, payloadType) {
    mLine = mLine.split(" ");
    for (var i = 0; i < mLine.length; ++i) {
        if (mLine[i] === payloadType.toString()) {
            mLine.splice(i, 1);
        }
    }
    return mLine.join(" ");
}
function removeCodecByName(sdpLines, codec) {
    var index = findLine(sdpLines, "a=rtpmap", codec);
    if (index === null) {
        return sdpLines;
    }
    var payloadType = getCodecPayloadTypeFromLine(sdpLines[index]);
    sdpLines.splice(index, 1);
    var mLineIndex = findLine(sdpLines, "m=", "video");
    if (mLineIndex === null) {
        return sdpLines;
    }
    sdpLines[mLineIndex] = removePayloadTypeFromMline(sdpLines[mLineIndex], payloadType);
    return sdpLines;
}
function removeCodecByPayloadType(sdpLines, payloadType) {
    var index = findLine(sdpLines, "a=rtpmap", payloadType.toString());
    if (index === null) {
        return sdpLines;
    }
    sdpLines.splice(index, 1);
    var mLineIndex = findLine(sdpLines, "m=", "video");
    if (mLineIndex === null) {
        return sdpLines;
    }
    sdpLines[mLineIndex] = removePayloadTypeFromMline(sdpLines[mLineIndex], payloadType);
    return sdpLines;
}
function maybeRemoveVideoFec(sdp, params) {
    if (params.videoFec !== "false") {
        return sdp;
    }
    var sdpLines = sdp.split("\r\n");
    var index = findLine(sdpLines, "a=rtpmap", "red");
    if (index === null) {
        return sdp;
    }
    var redPayloadType = getCodecPayloadTypeFromLine(sdpLines[index]);
    sdpLines = removeCodecByPayloadType(sdpLines, redPayloadType);
    sdpLines = removeCodecByName(sdpLines, "ulpfec");
    index = findLine(sdpLines, "a=fmtp", redPayloadType.toString());
    if (index === null) {
        return sdp;
    }
    var fmtpLine = parseFmtpLine(sdpLines[index]);
    var rtxPayloadType = fmtpLine.pt;
    if (rtxPayloadType === null) {
        return sdp;
    }
    sdpLines.splice(index, 1);
    sdpLines = removeCodecByPayloadType(sdpLines, rtxPayloadType);
    return sdpLines.join("\r\n");
}
function maybePreferAudioSendCodec(sdp, params) {
    return maybePreferCodec(sdp, "audio", "send", params.audioSendCodec);
}
function maybePreferAudioReceiveCodec(sdp, params) {
    return maybePreferCodec(sdp, "audio", "receive", params.audioRecvCodec);
}
function maybePreferVideoSendCodec(sdp, params) {
    return maybePreferCodec(sdp, "video", "send", params.videoSendCodec);
}
function maybePreferVideoReceiveCodec(sdp, params) {
    return maybePreferCodec(sdp, "video", "receive", params.videoRecvCodec);
}
function maybePreferCodec(sdp, type, dir, codec) {
    var str = type + " " + dir + " codec";
    if (!codec) {
        trace("No preference on " + str + ".");
        return sdp;
    }
    trace("Prefer " + str + ": " + codec);
    var sdpLines = sdp.split("\r\n");
    var mLineIndex = findLine(sdpLines, "m=", type);
    if (mLineIndex === null) {
        return sdp;
    }
    var payload = null;
    for (var i = sdpLines.length - 1; i >= 0; --i) {
        var index = findLineInRange(sdpLines, i, 0, "a=rtpmap", codec, "desc");
        if (index !== null) {
            i = index;
            payload = getCodecPayloadTypeFromLine(sdpLines[index]);
            if (payload) {
                sdpLines[mLineIndex] = setDefaultCodec(sdpLines[mLineIndex], payload);
            }
        } else {
            break;
        }
    }
    sdp = sdpLines.join("\r\n");
    return sdp;
}
function setCodecParam(sdp, codec, param, value) {
    var sdpLines = sdp.split("\r\n");
    var fmtpLineIndex = findFmtpLine(sdpLines, codec);
    var fmtpObj = {};
    if (fmtpLineIndex === null) {
        var index = findLine(sdpLines, "a=rtpmap", codec);
        if (index === null) {
            return sdp;
        }
        var payload = getCodecPayloadTypeFromLine(sdpLines[index]);
        fmtpObj.pt = payload.toString();
        fmtpObj.params = {};
        fmtpObj.params[param] = value;
        sdpLines.splice(index + 1, 0, writeFmtpLine(fmtpObj));
    } else {
        fmtpObj = parseFmtpLine(sdpLines[fmtpLineIndex]);
        fmtpObj.params[param] = value;
        sdpLines[fmtpLineIndex] = writeFmtpLine(fmtpObj);
    }
    sdp = sdpLines.join("\r\n");
    return sdp;
}
function removeCodecParam(sdp, codec, param) {
    var sdpLines = sdp.split("\r\n");
    var fmtpLineIndex = findFmtpLine(sdpLines, codec);
    if (fmtpLineIndex === null) {
        return sdp;
    }
    var map = parseFmtpLine(sdpLines[fmtpLineIndex]);
    delete map.params[param];
    var newLine = writeFmtpLine(map);
    if (newLine === null) {
        sdpLines.splice(fmtpLineIndex, 1);
    } else {
        sdpLines[fmtpLineIndex] = newLine;
    }
    sdp = sdpLines.join("\r\n");
    return sdp;
}
function parseFmtpLine(fmtpLine) {
    var fmtpObj = {};
    var spacePos = fmtpLine.indexOf(" ");
    var keyValues = fmtpLine.substring(spacePos + 1).split(";");
    var pattern = new RegExp("a=fmtp:(\\d+)");
    var result = fmtpLine.match(pattern);
    if (result && result.length === 2) {
        fmtpObj.pt = result[1];
    } else {
        return null;
    }
    var params = {};
    for (var i = 0; i < keyValues.length; ++i) {
        var pair = keyValues[i].split("=");
        if (pair.length === 2) {
            params[pair[0]] = pair[1];
        }
    }
    fmtpObj.params = params;
    return fmtpObj;
}
function writeFmtpLine(fmtpObj) {
    if (!fmtpObj.hasOwnProperty("pt") || !fmtpObj.hasOwnProperty("params")) {
        return null;
    }
    var pt = fmtpObj.pt;
    var params = fmtpObj.params;
    var keyValues = [];
    var i = 0;
    for (var key in params) {
        keyValues[i] = key + "=" + params[key];
        ++i;
    }
    if (i === 0) {
        return null;
    }
    return "a=fmtp:" + pt.toString() + " " + keyValues.join(";");
}
function findFmtpLine(sdpLines, codec) {
    var payload = getCodecPayloadType(sdpLines, codec);
    return payload ? findLine(sdpLines, "a=fmtp:" + payload.toString()) : null;
}
function findLine(sdpLines, prefix, substr) {
    return findLineInRange(sdpLines, 0, -1, prefix, substr);
}
function findLineInRange(sdpLines, startLine, endLine, prefix, substr, direction) {
    if (direction === undefined) {
        direction = "asc";
    }
    direction = direction || "asc";
    if (direction === "asc") {
        var realEndLine = endLine !== -1 ? endLine : sdpLines.length;
        for (var i = startLine; i < realEndLine; ++i) {
            if (sdpLines[i].indexOf(prefix) === 0) {
                if (!substr || sdpLines[i].toLowerCase().indexOf(substr.toLowerCase()) !== -1) {
                    return i;
                }
            }
        }
    } else {
        var realStartLine = startLine !== -1 ? startLine : sdpLines.length - 1;
        for (var j = realStartLine; j >= 0; --j) {
            if (sdpLines[j].indexOf(prefix) === 0) {
                if (!substr || sdpLines[j].toLowerCase().indexOf(substr.toLowerCase()) !== -1) {
                    return j;
                }
            }
        }
    }
    return null;
}
function getCodecPayloadType(sdpLines, codec) {
    var index = findLine(sdpLines, "a=rtpmap", codec);
    return index ? getCodecPayloadTypeFromLine(sdpLines[index]) : null;
}
function getCodecPayloadTypeFromLine(sdpLine) {
    var pattern = new RegExp("a=rtpmap:(\\d+) [a-zA-Z0-9-]+\\/\\d+");
    var result = sdpLine.match(pattern);
    return result && result.length === 2 ? result[1] : null;
}
function setDefaultCodec(mLine, payload) {
    var elements = mLine.split(" ");
    var newLine = elements.slice(0, 3);
    newLine.push(payload);
    for (var i = 3; i < elements.length; i++) {
        if (elements[i] !== payload) {
            newLine.push(elements[i]);
        }
    }
    return newLine.join(" ");
}
;
function extractStatAsInt(stats, statObj, statName) {
    var str = extractStat(stats, statObj, statName);
    if (str) {
        var val = parseInt(str);
        if (val !== -1) {
            return val;
        }
    }
    return null;
}
function extractStat(stats, statObj, statName) {
    var report = getStatsReport(stats, statObj, statName);
    if (report && report[statName] !== -1) {
        return report[statName];
    }
    return null;
}
function getStatsReport(stats, statObj, statName, statVal) {
    var result = null;
    if (stats) {
        stats.forEach(function (report, stat) {
            if (report.type === statObj) {
                var found = true;
                if (statName) {
                    var val = statName === "id" ? report.id : report[statName];
                    found = statVal !== undefined ? val === statVal : val;
                }
                if (found) {
                    result = report;
                }
            }
        });
    }
    return result;
}
function enumerateStats(stats, localTrackIds, remoteTrackIds) {
    var statsObject = {
        audio: { local: { audioLevel: 0.0, bytesSent: 0, clockRate: 0, codecId: "", mimeType: "", packetsSent: 0, payloadType: 0, timestamp: 0.0, trackId: "", transportId: "" }, remote: { audioLevel: 0.0, bytesReceived: 0, clockRate: 0, codecId: "", fractionLost: 0, jitter: 0, mimeType: "", packetsLost: 0, packetsReceived: 0, payloadType: 0, timestamp: 0.0, trackId: "", transportId: "" } }, video: {
            local: {
                bytesSent: 0, clockRate: 0, codecId: "", firCount: 0, framesEncoded: 0, frameHeight: 0, framesSent: 0, frameWidth: 0, nackCount: 0,
                packetsSent: 0, payloadType: 0, pliCount: 0, qpSum: 0, timestamp: 0.0, trackId: "", transportId: ""
            }, remote: { bytesReceived: 0, clockRate: 0, codecId: "", firCount: 0, fractionLost: 0, frameHeight: 0, framesDecoded: 0, framesDropped: 0, framesReceived: 0, frameWidth: 0, nackCount: 0, packetsLost: 0, packetsReceived: 0, payloadType: 0, pliCount: 0, qpSum: 0, timestamp: 0.0, trackId: "", transportId: "" }
        }, connection: {
            availableOutgoingBitrate: 0, bytesReceived: 0, bytesSent: 0, consentRequestsSent: 0, currentRoundTripTime: 0.0,
            localCandidateId: "", localCandidateType: "", localIp: "", localPort: 0, localPriority: 0, localProtocol: "", localRelayProtocol: undefined, remoteCandidateId: "", remoteCandidateType: "", remoteIp: "", remotePort: 0, remotePriority: 0, remoteProtocol: "", requestsReceived: 0, requestsSent: 0, responsesReceived: 0, responsesSent: 0, timestamp: 0.0, totalRoundTripTime: 0.0
        }
    };
    if (stats) {
        stats.forEach(function (report, stat) {
            switch (report.type) {
                case "outbound-rtp":
                    if (report.hasOwnProperty("trackId")) {
                        if (report.trackId.indexOf(localTrackIds.audio) !== -1) {
                            statsObject.audio.local.bytesSent = report.bytesSent;
                            statsObject.audio.local.codecId = report.codecId;
                            statsObject.audio.local.packetsSent = report.packetsSent;
                            statsObject.audio.local.timestamp = report.timestamp;
                            statsObject.audio.local.trackId = report.trackId;
                            statsObject.audio.local.transportId = report.transportId;
                        }
                        if (report.trackId.indexOf(localTrackIds.video) !== -1) {
                            statsObject.video.local.bytesSent = report.bytesSent;
                            statsObject.video.local.codecId = report.codecId;
                            statsObject.video.local.firCount = report.firCount;
                            statsObject.video.local.framesEncoded = report.frameEncoded;
                            statsObject.video.local.framesSent = report.framesSent;
                            statsObject.video.local.packetsSent = report.packetsSent;
                            statsObject.video.local.pliCount = report.pliCount;
                            statsObject.video.local.qpSum = report.qpSum;
                            statsObject.video.local.timestamp = report.timestamp;
                            statsObject.video.local.trackId = report.trackId;
                            statsObject.video.local.transportId = report.transportId;
                        }
                    }
                    break;
                case "inbound-rtp":
                    if (report.hasOwnProperty("trackId")) {
                        if (report.trackId.indexOf(remoteTrackIds.audio) !== -1) {
                            statsObject.audio.remote.bytesReceived = report.bytesReceived;
                            statsObject.audio.remote.codecId = report.codecId;
                            statsObject.audio.remote.fractionLost = report.fractionLost;
                            statsObject.audio.remote.jitter = report.jitter;
                            statsObject.audio.remote.packetsLost = report.packetsLost;
                            statsObject.audio.remote.packetsReceived = report.packetsReceived;
                            statsObject.audio.remote.timestamp = report.timestamp;
                            statsObject.audio.remote.trackId = report.trackId;
                            statsObject.audio.remote.transportId = report.transportId;
                        }
                        if (report.trackId.indexOf(remoteTrackIds.video) !== -1) {
                            statsObject.video.remote.bytesReceived = report.bytesReceived;
                            statsObject.video.remote.codecId = report.codecId;
                            statsObject.video.remote.firCount = report.firCount;
                            statsObject.video.remote.fractionLost = report.fractionLost;
                            statsObject.video.remote.nackCount = report.nackCount;
                            statsObject.video.remote.packetsLost = report.patsLost;
                            statsObject.video.remote.packetsReceived = report.packetsReceived;
                            statsObject.video.remote.pliCount = report.pliCount;
                            statsObject.video.remote.qpSum = report.qpSum;
                            statsObject.video.remote.timestamp = report.timestamp;
                            statsObject.video.remote.trackId = report.trackId;
                            statsObject.video.remote.transportId = report.transportId;
                        }
                    }
                    break;
                case "candidate-pair":
                    if (report.hasOwnProperty("availableOutgoingBitrate")) {
                        statsObject.connection.availableOutgoingBitrate = report.availableOutgoingBitrate;
                        statsObject.connection.bytesReceived = report.bytesReceived;
                        statsObject.connection.bytesSent = report.bytesSent;
                        statsObject.connection.consentRequestsSent = report.consentRequestsSent;
                        statsObject.connection.currentRoundTripTime = report.currentRoundTripTime;
                        statsObject.connection.localCandidateId = report.localCandidateId;
                        statsObject.connection.remoteCandidateId = report.remoteCandidateId;
                        statsObject.connection.requestsReceived = report.requestsReceived;
                        statsObject.connection.requestsSent = report.requestsSent;
                        statsObject.connection.responsesReceived = report.responsesReceived;
                        statsObject.connection.responsesSent = report.responsesSent;
                        statsObject.connection.timestamp = report.timestamp;
                        statsObject.connection.totalRoundTripTime = report.totalRoundTripTime;
                    }
                    break;
                default:
                    return;
            }
        }.bind());
        stats.forEach(function (report) {
            switch (report.type) {
                case "track":
                    if (report.hasOwnProperty("trackIdentifier")) {
                        if (report.trackIdentifier.indexOf(localTrackIds.video) !== -1) {
                            statsObject.video.local.frameHeight = report.frameHeight;
                            statsObject.video.local.framesSent = report.framesSent;
                            statsObject.video.local.frameWidth = report.frameWidth;
                        }
                        if (report.trackIdentifier.indexOf(remoteTrackIds.video) !== -1) {
                            statsObject.video.remote.frameHeight = report.frameHeight;
                            statsObject.video.remote.framesDecoded = report.framesDecoded;
                            statsObject.video.remote.framesDropped = report.framesDropped;
                            statsObject.video.remote.framesReceived = report.framesReceived;
                            statsObject.video.remote.frameWidth = report.frameWidth;
                        }
                        if (report.trackIdentifier.indexOf(localTrackIds.audio) !== -1) {
                            statsObject.audio.local.audioLevel = report.audioLevel;
                        }
                        if (report.trackIdentifier.indexOf(remoteTrackIds.audio) !== -1) {
                            statsObject.audio.remote.audioLevel = report.audioLevel;
                        }
                    }
                    break;
                case "codec":
                    if (report.hasOwnProperty("id")) {
                        if (report.id.indexOf(statsObject.audio.local.codecId) !== -1) {
                            statsObject.audio.local.clockRate = report.clockRate;
                            statsObject.audio.local.mimeType = report.mimeType;
                            statsObject.audio.local.payloadType = report.payloadType;
                        }
                        if (report.id.indexOf(statsObject.audio.remote.codecId) !== -1) {
                            statsObject.audio.remote.clockRate = report.clockRate;
                            statsObject.audio.remote.mimeType = report.mimeType;
                            statsObject.audio.remote.payloadType = report.payloadType;
                        }
                        if (report.id.indexOf(statsObject.video.local.codecId) !== -1) {
                            statsObject.video.local.clockRate = report.clockRate;
                            statsObject.video.local.mimeType = report.mimeType;
                            statsObject.video.local.payloadType = report.payloadType;
                        }
                        if (report.id.indexOf(statsObject.video.remote.codecId) !== -1) {
                            statsObject.video.remote.clockRate = report.clockRate;
                            statsObject.video.remote.mimeType = report.mimeType;
                            statsObject.video.remote.payloadType = report.payloadType;
                        }
                    }
                    break;
                case "local-candidate":
                    if (report.hasOwnProperty("id")) {
                        if (report.id.indexOf(statsObject.connection.localCandidateId) !== -1) {
                            statsObject.connection.localIp = report.ip;
                            statsObject.connection.localPort = report.port;
                            statsObject.connection.localPriority = report.priority;
                            statsObject.connection.localProtocol = report.protocol;
                            statsObject.connection.localType = report.candidateType;
                            statsObject.connection.localRelayProtocol = report.relayProtocol;
                        }
                    }
                    break;
                case "remote-candidate":
                    if (report.hasOwnProperty("id")) {
                        if (report.id.indexOf(statsObject.connection.remoteCandidateId) !== -1) {
                            statsObject.connection.remoteIp = report.ip;
                            statsObject.connection.remotePort = report.port;
                            statsObject.connection.remotePriority = report.priority;
                            statsObject.connection.remoteProtocol = report.protocol;
                            statsObject.connection.remoteType = report.candidateType;
                        }
                    }
                    break;
                default:
                    return;
            }
        }.bind());
    }
    return statsObject;
}
function computeRate(newReport, oldReport, statName) {
    var newVal = newReport[statName];
    var oldVal = oldReport ? oldReport[statName] : null;
    if (newVal === null || oldVal === null) {
        return null;
    }
    return (newVal - oldVal) / (newReport.timestamp - oldReport.timestamp) * 1000;
}
function computeBitrate(newReport, oldReport, statName) {
    return computeRate(newReport, oldReport, statName) * 8;
}
function computeE2EDelay(captureStart, remoteVideoCurrentTime) {
    if (!captureStart) {
        return null;
    }
    var nowNTP = Date.now() + 2208988800000;
    return nowNTP - captureStart - remoteVideoCurrentTime * 1000;
}
;
function $(selector) {
    return document.querySelector(selector);
}
function queryStringToDictionary(queryString) {
    var pairs = queryString.slice(1).split("&");
    var result = {};
    pairs.forEach(function (pair) {
        if (pair) {
            pair = pair.split("=");
            if (pair[0]) {
                result[pair[0]] = decodeURIComponent(pair[1] || "");
            }
        }
    });
    return result;
}
function sendAsyncUrlRequest(method, url, body) {
    return sendUrlRequest(method, url, true, body);
}
function sendUrlRequest(method, url, async, body) {
    return new Promise(function (resolve, reject) {
        var xhr;
        var reportResults = function () {
            if (xhr.status !== 200) {
                reject(Error("Status=" + xhr.status + ", response=" + xhr.responseText));
                return;
            }
            resolve(xhr.responseText);
        };
        xhr = new XMLHttpRequest;
        if (async) {
            xhr.onreadystatechange = function () {
                if (xhr.readyState !== 4) {
                    return;
                }
                reportResults();
            };
        }
        xhr.open(method, url, async);
        xhr.send(body);
        if (!async) {
            reportResults();
        }
    });
}
function requestIceServers(iceServerRequestUrl, iceTransports) {
    return new Promise(function (resolve, reject) {
        sendAsyncUrlRequest("POST", iceServerRequestUrl).then(function (response) {
            var iceServerRequestResponse = parseJSON(response);
            if (!iceServerRequestResponse) {
                reject(Error("Error parsing response JSON: " + response));
                return;
            }
            if (iceTransports !== "") {
                filterIceServersUrls(iceServerRequestResponse, iceTransports);
            }
            trace("Retrieved ICE server information.");
            resolve(iceServerRequestResponse.iceServers);
        }).catch(function (error) {
            reject(Error("ICE server request error: " + error.message));
            return;
        });
    });
}
function parseJSON(json) {
    try {
        return JSON.parse(json);
    } catch (e) {
        trace("Error parsing json: " + json);
    }
    return null;
}
function filterIceServersUrls(config, protocol) {
    var transport = "transport=" + protocol;
    var newIceServers = [];
    for (var i = 0; i < config.iceServers.length; ++i) {
        var iceServer = config.iceServers[i];
        var newUrls = [];
        for (var j = 0; j < iceServer.urls.length; ++j) {
            var url = iceServer.urls[j];
            if (url.indexOf(transport) !== -1) {
                newUrls.push(url);
            } else {
                if (url.indexOf("?transport=") === -1) {
                    newUrls.push(url + "?" + transport);
                }
            }
        }
        if (newUrls.length !== 0) {
            iceServer.urls = newUrls;
            newIceServers.push(iceServer);
        }
    }
    config.iceServers = newIceServers;
}
function setUpFullScreen() {
    if (isChromeApp()) {
        document.cancelFullScreen = function () {
            chrome.app.window.current().restore();
        };
    } else {
        document.cancelFullScreen = document.webkitCancelFullScreen || document.mozCancelFullScreen || document.cancelFullScreen;
    }
    if (isChromeApp()) {
        document.body.requestFullScreen = function () {
            chrome.app.window.current().fullscreen();
        };
    } else {
        document.body.requestFullScreen = document.body.webkitRequestFullScreen || document.body.mozRequestFullScreen || document.body.requestFullScreen;
    }
    document.onfullscreenchange = document.onfullscreenchange || document.onwebkitfullscreenchange || document.onmozfullscreenchange;
}
function isFullScreen() {
    if (isChromeApp()) {
        return chrome.app.window.current().isFullscreen();
    }
    return !!(document.webkitIsFullScreen || document.mozFullScreen || document.isFullScreen);
}
function fullScreenElement() {
    return document.webkitFullScreenElement || document.webkitCurrentFullScreenElement || document.mozFullScreenElement || document.fullScreenElement;
}
function randomString(strLength) {
    var result = [];
    strLength = strLength || 5;
    var charSet = "0123456789";
    while (strLength--) {
        result.push(charSet.charAt(Math.floor(Math.random() * charSet.length)));
    }
    return result.join("");
}
function randomId(strLength) {
    var result = [];
    strLength = strLength || 10;
    var charSet = "abcdefghijklmnopqrstuvwxyz";
    while (strLength--) {
        result.push(charSet.charAt(Math.floor(Math.random() * charSet.length)));
    }
    return result.join("");
}
function isChromeApp() {
    return typeof chrome !== "undefined" && typeof chrome.storage !== "undefined" && typeof chrome.storage.local !== "undefined";
}
function calculateFps(videoElement, decodedFrames, startTime, remoteOrLocal, callback) {
    var fps = 0;
    if (videoElement && typeof videoElement.webkitDecodedFrameCount !== undefined) {
        if (videoElement.readyState >= videoElement.HAVE_CURRENT_DATA) {
            var currentTime = (new Date).getTime();
            var deltaTime = (currentTime - startTime) / 1000;
            var startTimeToReturn = currentTime;
            fps = (videoElement.webkitDecodedFrameCount - decodedFrames) / deltaTime;
            callback(videoElement.webkitDecodedFrameCount, startTimeToReturn, remoteOrLocal);
        }
    }
    return parseInt(fps);
}
function trace(text) {
    if (text[text.length - 1] === "\n") {
        text = text.substring(0, text.length - 1);
    }
    if (window.performance) {
        var now = (window.performance.now() / 1000).toFixed(3);
        console.log(now + ": " + text);
    } else {
        console.log(text);
    }
}
; var apprtc = apprtc || {};
apprtc.windowPort = apprtc.windowPort || {};
(function () {
    var port_;
    apprtc.windowPort.sendMessage = function (message) {
        var port = getPort_();
        try {
            port.postMessage(message);
        } catch (ex) {
            trace("Error sending message via port: " + ex);
        }
    };
    apprtc.windowPort.addMessageListener = function (listener) {
        var port = getPort_();
        port.onMessage.addListener(listener);
    };
    var getPort_ = function () {
        if (!port_) {
            port_ = chrome.runtime.connect();
        }
        return port_;
    };
})();

