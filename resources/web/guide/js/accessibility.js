// accessibility.js - ARIA and keyboard support for guide pages
// Loaded on every guide page to make them usable with NVDA / Narrator.

(function () {
    "use strict";

    // Selectors for elements that act as buttons but are plain <div>s.
    var BUTTON_SELECTORS = [
        '.NormalBtn', '.GrayBtn', '.SmallBtn', '.SmallBtn_Green',
        '#AcceptBtn', '#PreBtn', '#StartBtn', '.RegionItem'
    ].join(',');

    function applyARIA() {
        // Make div-buttons focusable and announce them as buttons.
        var buttons = document.querySelectorAll(BUTTON_SELECTORS);
        for (var i = 0; i < buttons.length; i++) {
            var el = buttons[i];
            if (el.tagName.toLowerCase() === 'a' || el.tagName.toLowerCase() === 'button') continue;
            if (!el.getAttribute('role')) el.setAttribute('role', 'button');
            if (!el.getAttribute('tabindex')) el.setAttribute('tabindex', '0');
            // Space/Enter trigger click for keyboard users
            el.addEventListener('keydown', function (e) {
                if (e.keyCode === 13 || e.keyCode === 32) {
                    e.preventDefault();
                    this.click();
                }
            });
        }

        // Mark title divs as headings.
        var titleDivs = document.querySelectorAll('#Title > div, #Title > span');
        for (var j = 0; j < titleDivs.length; j++) {
            if (!titleDivs[j].getAttribute('role')) {
                titleDivs[j].setAttribute('role', 'heading');
                titleDivs[j].setAttribute('aria-level', '1');
            }
        }

        // Set focus to the first interactive element so NVDA starts reading immediately.
        var firstFocus = document.querySelector(
            'a[href], button, [role="button"][tabindex="0"], input, select');
        if (firstFocus) firstFocus.focus();
    }

    // Run after the page has loaded (onLoad may have already fired).
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', applyARIA);
    } else {
        applyARIA();
    }
    // Also re-run after a short delay to catch dynamically added buttons.
    setTimeout(applyARIA, 500);
})();
