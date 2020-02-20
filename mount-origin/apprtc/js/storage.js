var Storage = function () {
    trace("Storage()");
};
Storage.prototype.getStorage = function (key, callback) {
    trace("Storage.prototype.getStorage()");
    if (isChromeApp()) {
        chrome.storage.local.get(key, function (values) {
            if (callback) {
                window.setTimeout(function () {
                    callback(values[key]);
                }, 0);
            }
        });
    } else {
        var value = localStorage.getItem(key);
        if (callback) {
            window.setTimeout(function () {
                callback(value);
            }, 0);
        }
    }
};
Storage.prototype.setStorage = function (key, value, callback) {
    trace("Storage.prototype.getStorage()");
    if (isChromeApp()) {
        var data = {};
        data[key] = value;
        chrome.storage.local.set(data, callback);
    } else {
        localStorage.setItem(key, value);
        if (callback) {
            window.setTimeout(callback, 0);
        }
    }
};