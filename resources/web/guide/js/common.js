function ClosePage() {
	var tSend = {};
	tSend['sequence_id'] = Math.round(new Date() / 1000);
	tSend['command'] = "close_page";
	SendWXMessage(JSON.stringify(tSend));
}

document.onkeydown = function (event) {
    var e = event || window.event || arguments.callee.caller.arguments[0];
    var key = e.keyCode || e.which;

    // Allow navigation keys so screen readers (NVDA, Narrator) can operate:
    // Tab(9), Shift(16), Enter(13), Space(32), arrows(37-40), Escape(27),
    // PageUp(33), PageDown(34), Home(36), End(35)
    var allowedKeys = [9, 13, 16, 27, 32, 33, 34, 35, 36, 37, 38, 39, 40];
    if (allowedKeys.indexOf(key) !== -1) return;

    if (window.event) {
        try { e.keyCode = 0; } catch (e) { }
        e.returnValue = false;
    }
};

window.addEventListener('wheel', function (event) {
    if (event.ctrlKey === true || event.metaKey) {
        event.preventDefault();
    }
}, { passive: false });
