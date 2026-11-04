// Translation runtime.
//
// Two file:// constraints shape this unit:
//
//   1. fetch()/XHR against a file:// URL is blocked by both WKWebView and
//      WebView2, so locales cannot be JSON loaded at runtime. They ship as
//      classic scripts (locales/<code>.js) that call register(), injected on
//      demand - see tools/gen-locales.mjs.
//   2. Touching localStorage on a file:// document throws SecurityError in
//      WKWebView. Every access is guarded; language falls back to the URL
//      parameter the host already appends.
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});

  var STORAGE_KEY = 'BambuWebLang';
  var FALLBACK_LNG = 'en';

  var resources = {};
  var language = FALLBACK_LNG;

  function register(code, translation) {
    resources[code] = translation || {};
  }

  function safeStorageGet(key) {
    try {
      return global.localStorage.getItem(key);
    } catch (e) {
      return null;
    }
  }

  function safeStorageSet(key, value) {
    try {
      global.localStorage.setItem(key, value);
    } catch (e) {
      // file:// under WKWebView - language simply is not persisted.
    }
  }

  function detectLanguage() {
    var params = new URLSearchParams(global.location.search);
    var urlLang = params.get('lang');
    if (urlLang) {
      safeStorageSet(STORAGE_KEY, urlLang);
      return urlLang;
    }
    return safeStorageGet(STORAGE_KEY) || FALLBACK_LNG;
  }

  function interpolate(template, vars) {
    if (!vars) return template;
    return template.replace(/\{\{\s*([\w.]+)\s*\}\}/g, function (match, name) {
      var value = vars[name];
      return value === undefined || value === null ? match : String(value);
    });
  }

  // i18next-compatible lookup: current language, then English, then the key
  // itself (which is the English source sentence in this project). Empty strings
  // fall through exactly like returnEmptyString:false.
  function t(key, vars) {
    if (key === undefined || key === null) return '';
    var raw = resources[language] && resources[language][key];
    if (raw === undefined || raw === '') {
      raw = resources[FALLBACK_LNG] && resources[FALLBACK_LNG][key];
    }
    if (raw === undefined || raw === '') raw = key;
    return interpolate(String(raw), vars);
  }

  function loadScript(src) {
    return new Promise(function (resolve) {
      var script = document.createElement('script');
      script.src = src;
      // A missing locale must not be fatal: t() already degrades to English and
      // then to the key itself.
      script.onload = function () { resolve(true); };
      script.onerror = function () { resolve(false); };
      document.head.appendChild(script);
    });
  }

  // Loads the detected language plus the English fallback (~18-36 KB instead of
  // the 253 KB full set).
  function init() {
    language = detectLanguage();
    var wanted = [FALLBACK_LNG];
    if (language !== FALLBACK_LNG) wanted.push(language);

    return Promise.all(wanted.map(function (code) {
      return loadScript('./locales/' + code + '.js');
    })).then(function () {
      if (!resources[language]) language = FALLBACK_LNG;
    });
  }

  BBL.i18n = {
    init: init,
    register: register,
    t: t,
    get language() { return language; },
  };

  // Short alias; page modules call BBL.t(...) directly.
  BBL.t = t;
})(window);
