var loadingParams = {
    errorMessages: [],
    isLoopback: false,
    warningMessages: [],

    mediaConstraints: { "audio": true, "video": { "optional": [{ "minWidth": "1280" }, { "minHeight": "720" }], "mandatory": {} } },
    offerOptions: {},
        peerConnectionConfig: { 

		                                "rtcpMuxPolicy": "require", 

		                                "bundlePolicy": 

		                                "max-bundle", 

		                                "iceServers": [ 

							                                                {"urls":"stun:stun.l.google.com:19302"},

							                                                {"urls": "turn:13.232.126.19:3478","credential": "test", "username": "test" }]},


    peerConnectionConstraints: { "optional": [] },
    iceServerRequestUrl: '',
    iceServerTransports: '',
    wssUrl: 'wss://localhost:80',
    wssPostUrl: 'wss://localhost:80',
    bypassJoinConfirmation: false,
    versionInfo: { "gitHash": "7341b731567cfcda05079363fb27de88c22059cf", "branch": "master", "time": "Mon Sep 23 10:45:26 2019 +0200" },
};

var appController;

function initialize() {
    // We don't want to continue if this is triggered from Chrome prerendering,
    // since it will register the user to GAE without cleaning it up, causing
    // the real navigation to get a "full room" error. Instead we'll initialize
    // once the visibility state changes to non-prerender.
    if (document.visibilityState === 'prerender') {
        document.addEventListener('visibilitychange', onVisibilityChange);
        return;
    }
    appController = new AppController(loadingParams);
}

function onVisibilityChange() {
    if (document.visibilityState === 'prerender') {
        return;
    }
    document.removeEventListener('visibilitychange', onVisibilityChange);
    initialize();
}

initialize();
