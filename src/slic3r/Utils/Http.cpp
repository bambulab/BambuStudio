#include "Http.hpp"

#include <cstdlib>
#include <functional>
#include <thread>
#include <deque>
#include <mutex>
#include <sstream>
#include <exception>
#include <atomic>
#include <array>
#include <chrono>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include <curl/curl.h>

#ifdef OPENSSL_CERT_OVERRIDE
#include <openssl/x509.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace fs = boost::filesystem;

namespace Slic3r {

#ifdef _WIN32
// Query Windows system proxy configuration for a given URL. Returns empty if no proxy
// should be used, or a string like "http://proxy.corp:8080" otherwise. Handles both
// static proxy config and WPAD/PAC auto-configuration.
//
// Why this exists: libcurl does not consult Windows' IE/WinInet proxy settings. In
// corporate environments where the browser obeys WPAD/PAC and the registry, curl
// requests otherwise go direct and get dropped by the perimeter firewall without ever
// reaching the upstream server.
static std::string detect_windows_system_proxy(const std::string &url)
{
    std::string result;

    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ie_cfg = {};
    if (!::WinHttpGetIEProxyConfigForCurrentUser(&ie_cfg)) {
        return result;
    }

    auto wide_to_utf8 = [](LPCWSTR wstr) -> std::string {
        if (!wstr) return std::string();
        int len = ::WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
        if (len <= 1) return std::string(); // len includes the null terminator
        std::string out(static_cast<size_t>(len), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &out[0], len, nullptr, nullptr);
        out.resize(static_cast<size_t>(len - 1)); // drop the trailing null from content
        return out;
    };

    // Helper: pick the HTTPS proxy out of a possibly-multi-protocol proxy string.
    // IE proxy strings can be either "host:port" (applies to all) or
    // "http=host:port;https=host:port;ftp=host:port".
    auto pick_proxy = [&url](const std::string &proxies) -> std::string {
        if (proxies.empty()) return std::string();
        if (proxies.find('=') == std::string::npos) {
            return proxies;
        }
        const bool want_https = (url.rfind("https://", 0) == 0);
        const std::string key = want_https ? "https=" : "http=";
        size_t pos = proxies.find(key);
        if (pos == std::string::npos) return std::string();
        pos += key.size();
        size_t end = proxies.find(';', pos);
        return proxies.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };

    // Case 1: auto-detect (WPAD) or explicit PAC URL — resolve to a concrete proxy.
    if (ie_cfg.fAutoDetect || ie_cfg.lpszAutoConfigUrl) {
        HINTERNET session = ::WinHttpOpen(L"BambuStudio/Helio-proxy-detect",
                                          WINHTTP_ACCESS_TYPE_NO_PROXY,
                                          WINHTTP_NO_PROXY_NAME,
                                          WINHTTP_NO_PROXY_BYPASS,
                                          0);
        if (session) {
            WINHTTP_AUTOPROXY_OPTIONS opts = {};
            if (ie_cfg.fAutoDetect) {
                opts.dwFlags            = WINHTTP_AUTOPROXY_AUTO_DETECT;
                opts.dwAutoDetectFlags  = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
            }
            if (ie_cfg.lpszAutoConfigUrl) {
                opts.dwFlags           |= WINHTTP_AUTOPROXY_CONFIG_URL;
                opts.lpszAutoConfigUrl  = ie_cfg.lpszAutoConfigUrl;
            }
            opts.fAutoLogonIfChallenged = TRUE;

            std::wstring wurl(url.begin(), url.end());
            WINHTTP_PROXY_INFO proxy_info = {};
            if (::WinHttpGetProxyForUrl(session, wurl.c_str(), &opts, &proxy_info)) {
                if (proxy_info.dwAccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY && proxy_info.lpszProxy) {
                    std::string resolved = wide_to_utf8(proxy_info.lpszProxy);
                    // WinHttp can return multiple proxies separated by ';' or whitespace; take the first.
                    size_t sep = resolved.find_first_of("; \t");
                    if (sep != std::string::npos) resolved = resolved.substr(0, sep);
                    if (!resolved.empty()) result = resolved;
                }
                if (proxy_info.lpszProxy)        ::GlobalFree(proxy_info.lpszProxy);
                if (proxy_info.lpszProxyBypass)  ::GlobalFree(proxy_info.lpszProxyBypass);
            }
            ::WinHttpCloseHandle(session);
        }
    }

    // Case 2: static proxy configured in IE/WinInet.
    if (result.empty() && ie_cfg.lpszProxy) {
        std::string proxies = wide_to_utf8(ie_cfg.lpszProxy);
        result = pick_proxy(proxies);
    }

    if (ie_cfg.lpszProxy)         ::GlobalFree(ie_cfg.lpszProxy);
    if (ie_cfg.lpszProxyBypass)   ::GlobalFree(ie_cfg.lpszProxyBypass);
    if (ie_cfg.lpszAutoConfigUrl) ::GlobalFree(ie_cfg.lpszAutoConfigUrl);

    return result;
}
#endif // _WIN32

// Private

struct CurlGlobalInit
{
    static std::unique_ptr<CurlGlobalInit> instance;
    std::string message;

    // Process-global curl_share object. Lets every curl_easy handle pool its DNS
    // cache, TLS session tickets, and connection cache, so successive Helio polls
    // against the same host avoid re-resolving DNS, re-doing a full TLS handshake,
    // and (where HTTP/2 is available) reuse the existing TCP connection. Crucial
    // for the optimization-wait polling loop which can fire ~60 requests in 2 min.
    CURLSH *share = nullptr;

    // One mutex per curl_lock_data enum value (curl serializes access to the shared
    // caches via the lock/unlock callbacks). CURL_LOCK_DATA_LAST is the sentinel so
    // sizing by it gives one slot per valid data type.
    std::array<std::mutex, CURL_LOCK_DATA_LAST> share_mutexes;

    // Number of currently-running http_perform() invocations. Incremented by
    // Http::perform() before spawning the io_thread, decremented at the top of
    // http_perform's epilogue. The destructor spin-waits for this to reach zero
    // so detached io_threads don't race the process shutdown that runs
    // curl_global_cleanup.
    std::atomic<int> active_io_threads{0};

    static void share_lock(CURL * /*handle*/, curl_lock_data data, curl_lock_access /*access*/, void *userptr)
    {
        auto *self = static_cast<CurlGlobalInit*>(userptr);
        if (static_cast<size_t>(data) < self->share_mutexes.size()) {
            self->share_mutexes[data].lock();
        }
    }
    static void share_unlock(CURL * /*handle*/, curl_lock_data data, void *userptr)
    {
        auto *self = static_cast<CurlGlobalInit*>(userptr);
        if (static_cast<size_t>(data) < self->share_mutexes.size()) {
            self->share_mutexes[data].unlock();
        }
    }

	CurlGlobalInit()
    {
#ifdef OPENSSL_CERT_OVERRIDE // defined if SLIC3R_STATIC=ON

        // Look for a set of distro specific directories. Don't change the
        // order: https://bugzilla.redhat.com/show_bug.cgi?id=1053882
        static const char * CA_BUNDLES[] = {
            "/etc/pki/tls/certs/ca-bundle.crt",   // Fedora/RHEL 6
            "/etc/ssl/certs/ca-certificates.crt", // Debian/Ubuntu/Gentoo etc.
            "/usr/share/ssl/certs/ca-bundle.crt",
            "/usr/local/share/certs/ca-root-nss.crt", // FreeBSD
            "/etc/ssl/cert.pem",
            "/etc/ssl/ca-bundle.pem"              // OpenSUSE Tumbleweed
        };

        namespace fs = boost::filesystem;
        // Env var name for the OpenSSL CA bundle (SSL_CERT_FILE nomally)
        const char *const SSL_CA_FILE = X509_get_default_cert_file_env();
        const char * ssl_cafile = ::getenv(SSL_CA_FILE);

        if (!ssl_cafile)
            ssl_cafile = X509_get_default_cert_file();

        int replace = true;
        if (!ssl_cafile || !fs::exists(fs::path(ssl_cafile))) {
            const char * bundle = nullptr;
            for (const char * b : CA_BUNDLES) {
                if (fs::exists(fs::path(b))) {
                    ::setenv(SSL_CA_FILE, bundle = b, replace);
                    break;
                }
            }

            if (!bundle)
                message = "Unable to get system certificate.";
            else
                message = (boost::format("use system SSL certificate: %1%") % bundle).str();

            message += "\n" + (boost::format("To manually specify the system certificate store, "
            "set the %1% environment variable to the correct CA and restart the application.") % SSL_CA_FILE).str();
        }
#endif // OPENSSL_CERT_OVERRIDE

        if (CURLcode ec = ::curl_global_init(CURL_GLOBAL_DEFAULT)) {
            message += "CURL initialization failed. See the log for additional details.";
            BOOST_LOG_TRIVIAL(error) << ::curl_easy_strerror(ec);
            return;
        }

        share = ::curl_share_init();
        if (share != nullptr) {
            ::curl_share_setopt(share, CURLSHOPT_LOCKFUNC,   share_lock);
            ::curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, share_unlock);
            ::curl_share_setopt(share, CURLSHOPT_USERDATA,   this);
            ::curl_share_setopt(share, CURLSHOPT_SHARE,      CURL_LOCK_DATA_DNS);
            ::curl_share_setopt(share, CURLSHOPT_SHARE,      CURL_LOCK_DATA_SSL_SESSION);
#ifdef CURL_LOCK_DATA_CONNECT
            // Share the connection cache so handles can pull from the same pool of
            // live TCP connections. Added in libcurl 7.57.0.
            ::curl_share_setopt(share, CURLSHOPT_SHARE,      CURL_LOCK_DATA_CONNECT);
#endif
        }
    }

	~CurlGlobalInit()
    {
        // Wait for any in-flight io_threads to finish before tearing down libcurl and
        // the share handle — detached threads would otherwise crash or use-after-free
        // during global destruction. Bounded by a hard deadline so we never hang the
        // application on exit even if a thread is genuinely stuck.
        using namespace std::chrono;
        const auto deadline = steady_clock::now() + seconds(5);
        while (active_io_threads.load(std::memory_order_acquire) > 0 &&
               steady_clock::now() < deadline) {
            std::this_thread::sleep_for(milliseconds(20));
        }
        if (int still_active = active_io_threads.load(std::memory_order_acquire); still_active > 0) {
            BOOST_LOG_TRIVIAL(warning) << "CurlGlobalInit: shutting down with "
                                       << still_active << " io_threads still active";
        }

        if (share != nullptr) {
            ::curl_share_cleanup(share);
            share = nullptr;
        }
        ::curl_global_cleanup();
    }
};

std::unique_ptr<CurlGlobalInit> CurlGlobalInit::instance;

// File-local helper to expose the process-global share handle to priv::priv without
// leaking a curl-specific type through Http.hpp. Safe to call before CurlGlobalInit
// has been constructed: returns nullptr and the caller skips CURLOPT_SHARE.
static CURLSH * curl_global_share_handle()
{
    if (CurlGlobalInit::instance) {
        return CurlGlobalInit::instance->share;
    }
    return nullptr;
}

// File-local helpers for the in-flight io_thread counter used by CurlGlobalInit's
// destructor to wait for detached threads before tearing down libcurl.
static void curl_global_io_thread_inc()
{
    if (CurlGlobalInit::instance) {
        CurlGlobalInit::instance->active_io_threads.fetch_add(1, std::memory_order_acq_rel);
    }
}
static void curl_global_io_thread_dec()
{
    if (CurlGlobalInit::instance) {
        CurlGlobalInit::instance->active_io_threads.fetch_sub(1, std::memory_order_acq_rel);
    }
}

std::map<std::string, std::string> extra_headers;
std::mutex g_mutex;

struct Http::priv
{
	enum {
		DEFAULT_TIMEOUT_CONNECT = 10,
        // Overall request timeout in seconds. Previously 0 ("no limit"), which meant a
        // stuck half-open TCP socket could hang for the OS retransmit budget (~minutes
        // on Windows) before failing. 120s matches the longest legitimate Helio call
        // (gcode upload) with a bit of headroom.
        DEFAULT_TIMEOUT_MAX = 120,
		DEFAULT_SIZE_LIMIT = 1024 * 1024 * 1024,
	};

	::CURL *curl;
	::curl_httppost *form;
	::curl_httppost *form_end;
	::curl_mime* mime;
	::curl_slist *headerlist;
	// Used for reading the body
	std::string buffer;
	// Used for storing file streams added as multipart form parts
	// Using a deque here because unlike vector it doesn't ivalidate pointers on insertion
	std::deque<fs::ifstream> form_files;
	std::string postfields;
	std::string error_buffer;    // Used for CURLOPT_ERRORBUFFER
    std::string headers;
	size_t limit;
	// Must be atomic: Http::cancel() may be called from any thread (typically the UI
	// thread) while the xfer callback runs on the io_thread that owns this priv.
	std::atomic<bool> cancel;

	// Retry configuration. 0 (the default) means a single attempt — legacy behavior.
	// See http_perform() for the classification of retryable failures.
	int  max_retries            = 0;
	long retry_initial_backoff_ms = 1000;

	std::thread io_thread;
	Http::CompleteFn completefn;
	Http::ErrorFn errorfn;
	Http::ProgressFn progressfn;
	Http::IPResolveFn ipresolvefn;
	Http::HeaderCallbackFn headerfn;

	priv(const std::string &url);
	~priv();

	static bool ca_file_supported(::CURL *curl);
	static size_t writecb(void *data, size_t size, size_t nmemb, void *userp);
	static int xfercb(void *userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
	static int xfercb_legacy(void *userp, double dltotal, double dlnow, double ultotal, double ulnow);
	static size_t form_file_read_cb(char *buffer, size_t size, size_t nitems, void *userp);
	static int    form_file_seek_cb(void *userp, curl_off_t offset, int origin);
    static size_t headers_cb(char *buffer, size_t size, size_t nitems, void *userp);

	void set_timeout_connect(long timeout);
    void set_timeout_max(long timeout);
	void form_add_file(const char *name, const fs::path &path, const char* filename);
	/* mime */
	void mime_form_add_text(const char* name, const char* value);
	void mime_form_add_file(const char* name, const char* path);
	void set_post_body(const fs::path &path);
	void set_post_body(const std::string &body);
	void set_put_body(const fs::path &path);
	void set_del_body(const std::string& body);

	std::string curl_error(CURLcode curlcode);
	std::string body_size_error();
	void http_perform();
};

// Curl debug callback. By default this is a no-op — curl internally skips most of the
// expensive formatting when the callback is present but returns 0, so there is no cost.
// When the environment variable BAMBU_CURL_VERBOSE is set, we route text-class events
// into the boost log so that user-submitted log bundles can be used to diagnose TLS
// handshake failures, proxy rejections, and DNS issues that otherwise leave no trace.
// Data-class events (CURLINFO_*_DATA_*, CURLINFO_*_HEADER_OUT) are skipped so we don't
// leak request bodies or auth tokens into the log.
static int log_trace(CURL * /*handle*/, curl_infotype type,
	char *data, size_t size,
	void * /*userp*/)
{
	static const bool verbose_enabled = (std::getenv("BAMBU_CURL_VERBOSE") != nullptr);
	if (!verbose_enabled) return 0;

	// Only log text-class events to avoid leaking secrets in headers/bodies.
	if (type != CURLINFO_TEXT) return 0;

	std::string line(data, size);
	// Strip the trailing newline curl typically appends.
	while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
	if (!line.empty()) {
		BOOST_LOG_TRIVIAL(debug) << "curl: " << line;
	}
	return 0;
}

Http::priv::priv(const std::string &url)
	: curl(::curl_easy_init())
	, form(nullptr)
	, form_end(nullptr)
	, mime(nullptr)
	, headerlist(nullptr)
	, error_buffer(CURL_ERROR_SIZE + 1, '\0')
	, limit(0)
	, cancel(false)
{
    Http::tls_global_init();

	if (curl == nullptr) {
		throw Slic3r::RuntimeError(std::string("Could not construct Curl object"));
	}

	set_timeout_connect(DEFAULT_TIMEOUT_CONNECT);
    set_timeout_max(DEFAULT_TIMEOUT_MAX);
	::curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, log_trace);
	::curl_easy_setopt(curl, CURLOPT_URL, url.c_str());   // curl makes a copy internally
	::curl_easy_setopt(curl, CURLOPT_USERAGENT, SLIC3R_APP_NAME "/" SLIC3R_VERSION);
	::curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, &error_buffer.front());

	// SSL options: CURLOPT_SSL_OPTIONS is a bitmask that *replaces* on each setopt call,
	// so everything must be combined in a single call.
	//  - CURLSSLOPT_NATIVE_CA: use the OS native CA store (Schannel on Windows, Keychain on
	//    macOS) rather than bundling our own.
	//  - CURLSSLOPT_REVOKE_BEST_EFFORT (Windows only, libcurl >= 7.70.0): Schannel does
	//    *strict* CRL/OCSP revocation by default, which silently fails TLS handshakes when
	//    revocation endpoints are temporarily unreachable (flaky Wi-Fi, corporate proxies,
	//    slow CA responders). The browser's Schannel usage is soft-fail, so the browser
	//    works while our requests disappear before hitting the wire. Best-effort matches
	//    browser behavior.
	long ssl_options = CURLSSLOPT_NATIVE_CA;
#if defined(_WIN32) && defined(CURLSSLOPT_REVOKE_BEST_EFFORT)
	ssl_options |= CURLSSLOPT_REVOKE_BEST_EFFORT;
#endif
	::curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, ssl_options);

	// Require at least TLS 1.2 on all platforms, but do not cap the maximum.
	// Previously this was CURL_SSLVERSION_MAX_TLSv1_2 on Windows, which blocked TLS 1.3
	// and caused downgrade failures against servers/proxies that expect 1.3.
	::curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

	// Let the resolver pick IPv4 or IPv6 (Happy Eyeballs). Forcing IPv4 means we cannot
	// fall back when a host's v4 path is flaky on a given network.
	::curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER);

	// Required when running libcurl from multiple threads: without this, on POSIX the
	// async DNS resolver installs a SIGALRM handler and uses siglongjmp, which is unsafe
	// across threads. Harmless on Windows.
	::curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

	// Enable TCP keepalive so idle connections crossing a NAT/firewall/VPN don't get
	// silently evicted and leave us blocked waiting on a stale socket. On Windows
	// especially, split-tunnel VPNs and corporate firewalls drop idle flows fast.
	::curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
	::curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE,  30L);
	::curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);

	// Prefer HTTP/2 over TLS when the server advertises it (ALPN-negotiated). Falls
	// back to HTTP/1.1 transparently. HTTP/2's single-connection-per-origin behavior
	// combined with the curl_share (see CurlGlobalInit) means repeated polls reuse
	// the same TCP+TLS connection and amortize the handshake cost.
	::curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);

	// Larger recv/send buffers improve throughput on large gcode uploads and reduce
	// syscall overhead on fast networks. libcurl caps BUFFERSIZE at 512 KiB internally.
	::curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 262144L);
#ifdef CURLOPT_UPLOAD_BUFFERSIZE
	::curl_easy_setopt(curl, CURLOPT_UPLOAD_BUFFERSIZE, 262144L);
#endif

#ifdef _WIN32
	// Apply Windows system proxy settings. libcurl does not consult WinInet/IE proxy
	// configuration on its own, so corporate networks that require a proxy for the
	// browser will drop direct requests from the app without ever reaching the target.
	{
		const std::string sys_proxy = detect_windows_system_proxy(url);
		if (!sys_proxy.empty()) {
			::curl_easy_setopt(curl, CURLOPT_PROXY, sys_proxy.c_str());
			// Corporate Windows proxies almost always require NTLM/Negotiate/Kerberos.
			// CURLAUTH_ANY lets libcurl pick whatever the proxy demands; passing an
			// empty user:password triggers SSPI so the logged-in user's Windows
			// credentials are used automatically — same as the browser.
			::curl_easy_setopt(curl, CURLOPT_PROXYAUTH, (long)CURLAUTH_ANY);
			::curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, ":");
		}
	}
#endif

	// Attach the process-global curl_share so DNS resolutions and TLS session tickets
	// are pooled across every Http instance. Each Helio optimization wait makes ~60
	// polls against the same host; without sharing, each poll is a full TLS handshake
	// and DNS round-trip. With sharing, polls 2..60 resume the TLS session in 1-RTT.
	if (CURLSH *share = curl_global_share_handle()) {
		::curl_easy_setopt(curl, CURLOPT_SHARE, share);
	}
}

Http::priv::~priv()
{
	::curl_easy_cleanup(curl);
	::curl_formfree(form);
	::curl_mime_free(mime);
	::curl_slist_free_all(headerlist);
}

bool Http::priv::ca_file_supported(::CURL *curl)
{
	//BBS support set ca file by default
	bool res = true;

	if (curl == nullptr) { return res; }

#if LIBCURL_VERSION_MAJOR >= 7 && LIBCURL_VERSION_MINOR >= 48
	::curl_tlssessioninfo *tls;
	if (::curl_easy_getinfo(curl, CURLINFO_TLS_SSL_PTR, &tls) == CURLE_OK) {
		if (tls->backend == CURLSSLBACKEND_SCHANNEL || tls->backend == CURLSSLBACKEND_DARWINSSL) {
			// With Windows and OS X native SSL support, cert files cannot be set
			res = false;
		}
	}
#endif

	return res;
}

size_t Http::priv::writecb(void *data, size_t size, size_t nmemb, void *userp)
{
	auto self = static_cast<priv*>(userp);
	const char *cdata = static_cast<char*>(data);
	const size_t realsize = size * nmemb;

	const size_t limit = self->limit > 0 ? self->limit : DEFAULT_SIZE_LIMIT;
	if (self->buffer.size() + realsize > limit) {
		// This makes curl_easy_perform return CURLE_WRITE_ERROR
		return 0;
	}

	self->buffer.append(cdata, realsize);

	return realsize;
}

int Http::priv::xfercb(void *userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	auto self = static_cast<priv*>(userp);
	// Snapshot the external cancel flag early so a race doesn't give the progressfn a
	// stale view of whether it still needs to compute.
	bool cb_cancel = self->cancel.load(std::memory_order_acquire);

	if (self->progressfn) {
		double speed;
        curl_easy_getinfo(self->curl, CURLINFO_SPEED_UPLOAD, &speed);
		if (speed > 0.01)
			speed = speed;
		Progress progress(dltotal, dlnow, ultotal, ulnow, speed);
		self->progressfn(progress, cb_cancel);
	}

	if (cb_cancel) { self->cancel.store(true, std::memory_order_release); }

	// Returning non-zero from CURLOPT_XFERINFOFUNCTION aborts the transfer with
	// CURLE_ABORTED_BY_CALLBACK.
	return self->cancel.load(std::memory_order_acquire) ? 1 : 0;
}

int Http::priv::xfercb_legacy(void *userp, double dltotal, double dlnow, double ultotal, double ulnow)
{
	return xfercb(userp, dltotal, dlnow, ultotal, ulnow);
}

size_t Http::priv::form_file_read_cb(char *buffer, size_t size, size_t nitems, void *userp)
{
	try {
        auto fstream = static_cast<fs::ifstream *>(userp);

		if (!fstream) { throw std::runtime_error(std::string("The fstream is nullptr! please check")); return CURL_READFUNC_ABORT; }

		fstream->read(buffer, size * nitems);
		return fstream->gcount();
	} catch (const std::exception &) {
		return CURL_READFUNC_ABORT;
	}

	return CURL_READFUNC_ABORT;
}

// Seek callback paired with form_file_read_cb. Required for any retry or follow-redirect
// path that re-issues the same PUT — without it, curl cannot rewind the upload stream
// and the second attempt would send an empty body. Returns CURL_SEEKFUNC_OK on success,
// CURL_SEEKFUNC_CANTSEEK to tell curl the stream is not rewindable (which would force it
// to bail with CURLE_SEND_FAIL_REWIND on the retry).
int Http::priv::form_file_seek_cb(void *userp, curl_off_t offset, int origin)
{
	try {
		auto fstream = static_cast<fs::ifstream *>(userp);
		if (!fstream) { return CURL_SEEKFUNC_FAIL; }

		std::ios_base::seekdir dir = std::ios_base::beg;
		switch (origin) {
			case SEEK_SET: dir = std::ios_base::beg; break;
			case SEEK_CUR: dir = std::ios_base::cur; break;
			case SEEK_END: dir = std::ios_base::end; break;
			default: return CURL_SEEKFUNC_FAIL;
		}

		// Clear any EOF/fail bits before seeking — after a full read, the stream is
		// in eof()==true and subsequent read() calls return 0 bytes until state clears.
		fstream->clear();
		fstream->seekg(static_cast<std::streamoff>(offset), dir);
		return fstream->good() ? CURL_SEEKFUNC_OK : CURL_SEEKFUNC_CANTSEEK;
	} catch (const std::exception &) {
		return CURL_SEEKFUNC_FAIL;
	}
}

size_t Http::priv::headers_cb(char *buffer, size_t size, size_t nitems, void *userp)
{
	auto self = static_cast<priv*>(userp);

	if (self->headerfn) {
        self->headers.append(buffer, nitems * size);
		self->headerfn(self->headers);
	}
	return nitems * size;
}

void Http::priv::set_timeout_connect(long timeout)
{
	::curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout);
}

void Http::priv::set_timeout_max(long timeout)
{
    ::curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
}

void Http::priv::form_add_file(const char *name, const fs::path &path, const char* filename)
{
	// We can't use CURLFORM_FILECONTENT, because curl doesn't support Unicode filenames on Windows
	// and so we use CURLFORM_STREAM with boost ifstream to read the file.

	if (filename == nullptr) {
		filename = path.string().c_str();
	}

	form_files.emplace_back(path, std::ios::in | std::ios::binary);
	auto &stream = form_files.back();
	stream.seekg(0, std::ios::end);
	size_t size = stream.tellg();
	stream.seekg(0);

	if (filename != nullptr) {
		::curl_formadd(&form, &form_end,
			CURLFORM_COPYNAME, name,
			CURLFORM_FILENAME, filename,
			CURLFORM_CONTENTTYPE, "application/octet-stream",
			CURLFORM_STREAM, static_cast<void*>(&stream),
			CURLFORM_CONTENTSLENGTH, static_cast<long>(size),
			CURLFORM_END
		);
	}
}

void Http::priv::mime_form_add_text(const char* name, const char* value)
{
	if (!mime) {
		mime = curl_mime_init(curl);
	}

	curl_mimepart *part;
	part = curl_mime_addpart(mime);
	curl_mime_name(part, name);
	curl_mime_type(part, "multipart/form-data");
	curl_mime_data(part, value, CURL_ZERO_TERMINATED);
}

void Http::priv::mime_form_add_file(const char* name, const char* path)
{
	if (!mime) {
		mime = curl_mime_init(curl);
	}

	curl_mimepart* part;
	part = curl_mime_addpart(mime);
	curl_mime_name(part, "file");
	curl_mime_type(part, "multipart/form-data");
	curl_mime_filedata(part, path);
	// BBS specify filename after filedata
	curl_mime_filename(part, name);
}

//FIXME may throw! Is the caller aware of it?
void Http::priv::set_post_body(const fs::path &path)
{
	std::ifstream file(path.string());
	std::string file_content { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
	postfields = std::move(file_content);
}

void Http::priv::set_post_body(const std::string &body)
{
	postfields = body;
}

void Http::priv::set_put_body(const fs::path &path)
{
	boost::system::error_code ec;
	boost::uintmax_t filesize = file_size(path, ec);
	if (!ec) {
		form_files.emplace_back(path, std::ios_base::binary |std::ios_base::in);
		auto &putFile = form_files.back();
		::curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
		::curl_easy_setopt(curl, CURLOPT_READDATA, static_cast<void*>(&putFile));
		::curl_easy_setopt(curl, CURLOPT_INFILESIZE, filesize);
		// Seek callback so the retry loop (see http_perform) can rewind the file
		// between attempts. Without this, a retried PUT would send a zero-byte body
		// because the ifstream is left at EOF after the first pass.
		::curl_easy_setopt(curl, CURLOPT_SEEKFUNCTION, form_file_seek_cb);
		::curl_easy_setopt(curl, CURLOPT_SEEKDATA, static_cast<void*>(&putFile));
	}
}

void Http::priv::set_del_body(const std::string& body)
{
	postfields = body;
}

std::string Http::priv::curl_error(CURLcode curlcode)
{
	return (boost::format("curl:%1%:\n%2%\n[Error %3%]")
		% ::curl_easy_strerror(curlcode)
		% error_buffer.c_str()
		% curlcode
	).str();
}

std::string Http::priv::body_size_error()
{
	return (boost::format("HTTP body data size exceeded limit (%1% bytes)") % limit).str();
}

void Http::priv::http_perform()
{
	::curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	::curl_easy_setopt(curl, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);
	::curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writecb);
	::curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(this));
	::curl_easy_setopt(curl, CURLOPT_READFUNCTION, form_file_read_cb);
	//BBS set header functions
	::curl_easy_setopt(curl, CURLOPT_HEADERDATA, static_cast<void *>(this));
	::curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headers_cb);

	::curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
#if LIBCURL_VERSION_MAJOR >= 7 && LIBCURL_VERSION_MINOR >= 32
	::curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xfercb);
	::curl_easy_setopt(curl, CURLOPT_XFERINFODATA, static_cast<void*>(this));
#ifndef _WIN32
	(void)xfercb_legacy;   // prevent unused function warning
#endif
#else
	::curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, xfercb);
	::curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, static_cast<void*>(this));
#endif

	::curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

	if (headerlist != nullptr) {
		::curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
	}

	if (form != nullptr) {
		::curl_easy_setopt(curl, CURLOPT_HTTPPOST, form);
	}

	if (mime != nullptr) {
		::curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
	}

	if (!postfields.empty()) {
		::curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postfields.c_str());
		::curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, postfields.size());
	}

	// Classification helpers for the retry loop. The broad rule: retry anything that
	// could plausibly succeed on a second attempt against the same URL with the same
	// inputs, and never retry anything where re-issuing is either pointless or unsafe.
	auto is_retryable_curl_error = [](CURLcode code) -> bool {
		switch (code) {
			// User-driven or programmatic cancellation — never retry.
			case CURLE_ABORTED_BY_CALLBACK:
			// Size-limit abort from the write callback — never retry.
			case CURLE_WRITE_ERROR:
			// Programmer error — retry will not help.
			case CURLE_UNSUPPORTED_PROTOCOL:
			case CURLE_FAILED_INIT:
			case CURLE_URL_MALFORMAT:
			case CURLE_NOT_BUILT_IN:
			case CURLE_FUNCTION_NOT_FOUND:
			case CURLE_BAD_FUNCTION_ARGUMENT:
			case CURLE_UNKNOWN_OPTION:
			// Auth failures — credentials are not going to repair themselves.
			case CURLE_LOGIN_DENIED:
			case CURLE_AUTH_ERROR:
			case CURLE_REMOTE_ACCESS_DENIED:
			// We handle HTTP status-code errors separately via the response code branch.
			case CURLE_HTTP_RETURNED_ERROR:
				return false;
			// Everything else — DNS failures, connect refusals, TLS handshake failures
			// (critical on Windows with Schannel), timeouts, partial reads — is retried.
			default:
				return true;
		}
	};
	auto is_retryable_http_status = [](long status) -> bool {
		if (status == 408 || status == 425) return true;              // Request Timeout / Too Early
		if (status >= 500 && status <= 599 && status != 501) return true; // 5xx except Not Implemented
		return false;
	};

	const int total_attempts = std::max(1, max_retries + 1);
	CURLcode res             = CURLE_OK;
	long     http_status     = 0;

	for (int attempt = 0; attempt < total_attempts; ++attempt) {
		if (attempt > 0) {
			// Exponential backoff with a 30s ceiling per attempt. The cap matters for
			// callers that bumped retry count high — without it, 6 retries at 1s base
			// would wait 63 seconds on the final backoff alone.
			long delay_ms = retry_initial_backoff_ms * (1L << (attempt - 1));
			if (delay_ms > 30000) delay_ms = 30000;

			// Sleep in short chunks so Http::cancel() can abort a long backoff. Without
			// this, a user who clicks Cancel during a retry backoff would wait up to
			// 30s before anything happened.
			const long chunk_ms   = 200;
			long       remaining  = delay_ms;
			while (remaining > 0 && !cancel.load(std::memory_order_acquire)) {
				const long this_chunk = remaining > chunk_ms ? chunk_ms : remaining;
				std::this_thread::sleep_for(std::chrono::milliseconds(this_chunk));
				remaining -= this_chunk;
			}
			if (cancel.load(std::memory_order_acquire)) {
				res = CURLE_ABORTED_BY_CALLBACK;
				break;
			}

			// Reset per-attempt state. curl itself reuses the handle happily, but we
			// need to clear the buffers we own so the second attempt doesn't append
			// to a partially-filled buffer from the first.
			buffer.clear();
			headers.clear();
			if (!error_buffer.empty()) {
				std::fill(error_buffer.begin(), error_buffer.end(), '\0');
			}

			BOOST_LOG_TRIVIAL(info) << "Http: retrying after transient failure, attempt "
			                        << (attempt + 1) << "/" << total_attempts;
		}

		res         = ::curl_easy_perform(curl);
		http_status = 0;

		if (res == CURLE_OK) {
			::curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
			if (http_status >= 200 && http_status < 300) break;       // success
			if (!is_retryable_http_status(http_status)) break;        // permanent HTTP error
		} else {
			if (!is_retryable_curl_error(res)) break;                 // permanent transport error
		}
		// Otherwise fall through to the next iteration of the retry loop.
	}

	// Final dispatch to user callbacks. This runs exactly once per http_perform call.
	if (res != CURLE_OK) {
		if (res == CURLE_ABORTED_BY_CALLBACK) {
			if (cancel.load(std::memory_order_acquire)) {
				// The abort comes from the request being cancelled programatically
				Progress dummyprogress(0, 0, 0, 0);
				bool cancel_flag = true;
				if (progressfn) { progressfn(dummyprogress, cancel_flag); }
			} else {
				// The abort comes from the CURLOPT_READFUNCTION callback, which means reading file failed
				if (errorfn) { errorfn(std::move(buffer), "Error reading file for file upload", 0); }
			}
		}
		else if (res == CURLE_WRITE_ERROR) {
			if (errorfn) { errorfn(std::move(buffer), body_size_error(), 0); }
		} else {
			if (errorfn) { errorfn(std::move(buffer), curl_error(res), 0); }
		};
	} else {
		//BBS check success http status code
		if (http_status >= 200 && http_status < 300) {
			if (completefn) { completefn(std::move(buffer), http_status); }
			if (ipresolvefn) {
				char* ct;
				res = curl_easy_getinfo(curl, CURLINFO_PRIMARY_IP, &ct);
				if ((CURLE_OK == res) && ct) {
					ipresolvefn(ct);
				}
			}
		}
		//BBS check error http status code
		else if (http_status >= 400) {
			if (errorfn) { errorfn(std::move(buffer), std::string(), http_status); }
		}
	}
}

Http::Http(const std::string &url) : p(new priv(url)) {

    std::lock_guard<std::mutex> l(g_mutex);
	for (auto it = extra_headers.begin(); it != extra_headers.end(); it++)
		this->header(it->first, it->second);
}


// Public

Http::Http(Http &&other) : p(std::move(other.p)) {}

Http::~Http()
{
	if (p && p->io_thread.joinable()) {
		p->io_thread.detach();
	}
}


Http& Http::timeout_connect(long timeout)
{
	if (timeout < 1) { timeout = priv::DEFAULT_TIMEOUT_CONNECT; }
	if (p) { p->set_timeout_connect(timeout); }
	return *this;
}

Http& Http::timeout_max(long timeout)
{
    if (timeout < 1) { timeout = priv::DEFAULT_TIMEOUT_MAX; }
    if (p) { p->set_timeout_max(timeout); }
    return *this;
}

Http& Http::size_limit(size_t sizeLimit)
{
	if (p) { p->limit = sizeLimit; }
	return *this;
}

Http& Http::retries(int retry_count, long initial_backoff_ms)
{
	if (p) {
		p->max_retries             = (retry_count < 0) ? 0 : retry_count;
		p->retry_initial_backoff_ms = (initial_backoff_ms < 0) ? 0 : initial_backoff_ms;
	}
	return *this;
}

Http& Http::header(std::string name, const std::string &value)
{
	if (!p) { return * this; }

	if (name.size() > 0) {
		name.append(": ").append(value);
	} else {
		name.push_back(':');
	}
	p->headerlist = curl_slist_append(p->headerlist, name.c_str());
	return *this;
}

Http& Http::remove_header(std::string name)
{
	if (p) {
		name.push_back(':');
		p->headerlist = curl_slist_append(p->headerlist, name.c_str());
	}

	return *this;
}

// Authorization by HTTP digest, based on RFC2617.
Http& Http::auth_digest(const std::string &user, const std::string &password)
{
	curl_easy_setopt(p->curl, CURLOPT_USERNAME, user.c_str());
	curl_easy_setopt(p->curl, CURLOPT_PASSWORD, password.c_str());
	curl_easy_setopt(p->curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);

	return *this;
}

Http& Http::auth_basic(const std::string &user, const std::string &password)
{
    curl_easy_setopt(p->curl, CURLOPT_USERNAME, user.c_str());
    curl_easy_setopt(p->curl, CURLOPT_PASSWORD, password.c_str());
    curl_easy_setopt(p->curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);

    return *this;
}

Http& Http::ca_file(const std::string &name)
{
	if (p && priv::ca_file_supported(p->curl)) {
		::curl_easy_setopt(p->curl, CURLOPT_CAINFO, name.c_str());
	}

	return *this;
}


Http& Http::form_add(const std::string &name, const std::string &contents)
{
	if (p) {
		::curl_formadd(&p->form, &p->form_end,
			CURLFORM_COPYNAME, name.c_str(),
			CURLFORM_COPYCONTENTS, contents.c_str(),
			CURLFORM_END
		);
	}

	return *this;
}

Http& Http::form_add_file(const std::string &name, const fs::path &path)
{
	if (p) { p->form_add_file(name.c_str(), path.c_str(), nullptr); }
	return *this;
}


Http& Http::mime_form_add_text(std::string &name, std::string &value)
{
	if (p) { p->mime_form_add_text(name.c_str(), value.c_str()); }
	return *this;
}

Http& Http::mime_form_add_file(std::string &name, const char* path)
{
	if (p) { p->mime_form_add_file(name.c_str(), path); }
	return *this;
}


Http& Http::form_add_file(const std::wstring& name, const fs::path& path)
{
	if (p) { p->form_add_file((char*)name.c_str(), path.c_str(), nullptr); }
	return *this;
}

Http& Http::form_add_file(const std::string &name, const fs::path &path, const std::string &filename)
{
	if (p) { p->form_add_file(name.c_str(), path.c_str(), filename.c_str()); }
	return *this;
}

#ifdef WIN32
// Tells libcurl to ignore certificate revocation checks in case of missing or offline distribution points for those SSL backends where such behavior is present.
// This option is only supported for Schannel (the native Windows SSL library).
Http& Http::ssl_revoke_best_effort(bool set)
{
	// BBS
#if 0
	if(p && set){
		::curl_easy_setopt(p->curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_REVOKE_BEST_EFFORT);
	}
#endif
	return *this;
}
#endif // WIN32

Http& Http::set_post_body(const fs::path &path)
{
	if (p) { p->set_post_body(path);}
	return *this;
}

Http& Http::set_post_body(const std::string &body)
{
	if (p) { p->set_post_body(body); }
	return *this;
}

Http& Http::set_put_body(const fs::path &path)
{
	if (p) { p->set_put_body(path);}
	return *this;
}

Http& Http::set_del_body(const std::string &body)
{
	if (p) { p->set_del_body(body); }
	return *this;
}

Http& Http::on_complete(CompleteFn fn)
{
	if (p) { p->completefn = std::move(fn); }
	return *this;
}

Http& Http::on_error(ErrorFn fn)
{
	if (p) { p->errorfn = std::move(fn); }
	return *this;
}

Http& Http::on_progress(ProgressFn fn)
{
	if (p) { p->progressfn = std::move(fn); }
	return *this;
}

Http& Http::on_ip_resolve(IPResolveFn fn)
{
	if (p) { p->ipresolvefn = std::move(fn); }
	return *this;
}

Http &Http::on_header_callback(HeaderCallbackFn fn)
{
	if (p) { p->headerfn = std::move(fn); }
	return *this;
}

Http::Ptr Http::perform()
{
	auto self = std::make_shared<Http>(std::move(*this));

	if (self->p) {
		// Increment the in-flight counter *before* spawning the thread so
		// CurlGlobalInit's destructor can never observe a moment where the thread
		// exists but the counter hasn't been bumped yet. The wrapper lambda
		// guarantees the matching decrement even if http_perform throws.
		curl_global_io_thread_inc();
		auto io_thread = std::thread([self](){
				try {
					self->p->http_perform();
				} catch (...) {
					curl_global_io_thread_dec();
					throw;
				}
				curl_global_io_thread_dec();
			});
		self->p->io_thread = std::move(io_thread);
	}

	return self;
}

void Http::perform_sync()
{
	if (p) {
		// Synchronous execution runs on the caller's thread, so no detached thread
		// to track — but we still wrap the counter for consistency and so that a
		// shutdown initiated from another thread while a sync request is in flight
		// waits for it to finish.
		curl_global_io_thread_inc();
		try {
			p->http_perform();
		} catch (...) {
			curl_global_io_thread_dec();
			throw;
		}
		curl_global_io_thread_dec();
	}
}

void Http::cancel()
{
	// May be called from any thread (typically the UI thread). The xfer callback
	// running on the io_thread reads this and aborts curl_easy_perform by returning
	// non-zero from CURLOPT_XFERINFOFUNCTION.
	if (p) { p->cancel.store(true, std::memory_order_release); }
}

Http Http::get(std::string url)
{
    return Http{std::move(url)};
}

Http Http::post(std::string url)
{
	Http http{std::move(url)};
	curl_easy_setopt(http.p->curl, CURLOPT_POST, 1L);
	return http;
}

Http Http::put(std::string url)
{
	Http http{std::move(url)};
	curl_easy_setopt(http.p->curl, CURLOPT_UPLOAD, 1L);
	return http;
}

Http Http::put2(std::string url)
{
	Http http{ std::move(url) };
	curl_easy_setopt(http.p->curl, CURLOPT_CUSTOMREQUEST, "PUT");
	return http;
}

Http Http::patch(std::string url)
{
	Http http{ std::move(url) };
	curl_easy_setopt(http.p->curl, CURLOPT_CUSTOMREQUEST, "PATCH");
	return http;
}

Http Http::del(std::string url)
{
	Http http{ std::move(url) };
	curl_easy_setopt(http.p->curl, CURLOPT_CUSTOMREQUEST, "DELETE");
	return http;
}

void Http::set_extra_headers(std::map<std::string, std::string> headers)
{
    std::lock_guard<std::mutex> l(g_mutex);
	extra_headers.swap(headers);
}

std::map<std::string, std::string> Http::get_extra_headers()
{
    std::lock_guard<std::mutex> l(g_mutex);
    return extra_headers;
}

bool Http::ca_file_supported()
{
	::CURL *curl = ::curl_easy_init();
	bool res = priv::ca_file_supported(curl);
	if (curl != nullptr) { ::curl_easy_cleanup(curl); }
    return res;
}

std::string Http::tls_global_init()
{
    if (!CurlGlobalInit::instance)
        CurlGlobalInit::instance = std::make_unique<CurlGlobalInit>();

    return CurlGlobalInit::instance->message;
}

std::string Http::tls_system_cert_store()
{
    std::string ret;

#ifdef OPENSSL_CERT_OVERRIDE
    ret = ::getenv(X509_get_default_cert_file_env());
#endif

    return ret;
}

std::string Http::url_encode(const std::string &str)
{
	::CURL *curl = ::curl_easy_init();
	if (curl == nullptr) {
		return str;
	}
	char *ce = ::curl_easy_escape(curl, str.c_str(), str.length());
	std::string encoded = std::string(ce);

	::curl_free(ce);
	::curl_easy_cleanup(curl);

	return encoded;
}

std::string Http::url_decode(const std::string &str)
{
    ::CURL *curl = ::curl_easy_init();
    if (curl == nullptr) { return str; }
    int outlen = 0;
    char *ce = ::curl_easy_unescape(curl, str.c_str(), str.length(), &outlen);
    std::string dencoded = std::string(ce, outlen);

    ::curl_free(ce);
    ::curl_easy_cleanup(curl);

    return dencoded;
}

std::string Http::get_filename_from_url(const std::string &url)
{
    int end_pos = url.find_first_of('?');
	if (end_pos <= 0) return "";
	std::string path_url = url.substr(0, end_pos);
	int start_pos = path_url.find_last_of("/");
	if (start_pos < 0) return "";
	return path_url.substr(start_pos + 1, path_url.length() - start_pos - 1);
}

std::ostream& operator<<(std::ostream &os, const Http::Progress &progress)
{
	os << "Http::Progress("
		<< "dltotal = " << progress.dltotal
		<< ", dlnow = " << progress.dlnow
		<< ", ultotal = " << progress.ultotal
		<< ", ulnow = " << progress.ulnow
		<< ")";
	return os;
}


}
