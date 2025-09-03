function IsInSlicer() {
    let bMatch = navigator.userAgent.match(RegExp('BBL-Slicer', 'i'));

    return bMatch;
}

function SendWXMessage(strMsg) {
    let bCheck = IsInSlicer();

    if (bCheck != null) {
        window.wx.postMessage(strMsg);
    }
}

function OpenPPLink() {
    SendWXMessage("helio_link_pp");
}

function OpenTouLink() {
    SendWXMessage("helio_link_tou");
}

function OpenHomeLink() {
    SendWXMessage("helio_link_home");
}