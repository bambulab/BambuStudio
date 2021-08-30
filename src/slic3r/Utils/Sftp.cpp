#include "Sftp.hpp"

#include <cstdlib>
#include <functional>
#include <thread>
#include <deque>
#include <sstream>
#include <exception>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include <curl/curl.h>

#ifdef OPENSSL_CERT_OVERRIDE
#include <openssl/x509.h>
#endif

#include <libslic3r/libslic3r.h>
#include <libslic3r/Utils.hpp>
#include <slic3r/GUI/I18N.hpp>
#include <slic3r/GUI/format.hpp>

namespace fs = boost::filesystem;


namespace Slic3r {


// Private
struct CurlGlobalInit
{
    static std::unique_ptr<CurlGlobalInit> sftp_instance;
    std::string message;

    CurlGlobalInit()
    {
#ifdef OPENSSL_CERT_OVERRIDE // defined if SLIC3R_STATIC=ON

        // Look for a set of distro specific directories. Don't change the
        // order: https://bugzilla.redhat.com/show_bug.cgi?id=1053882
        static const char* CA_BUNDLES[] = {
            "/etc/pki/tls/certs/ca-bundle.crt",   // Fedora/RHEL 6
            "/etc/ssl/certs/ca-certificates.crt", // Debian/Ubuntu/Gentoo etc.
            "/usr/share/ssl/certs/ca-bundle.crt",
            "/usr/local/share/certs/ca-root-nss.crt", // FreeBSD
            "/etc/ssl/cert.pem",
            "/etc/ssl/ca-bundle.pem"              // OpenSUSE Tumbleweed
        };

        namespace fs = boost::filesystem;
        // Env var name for the OpenSSL CA bundle (SSL_CERT_FILE nomally)
        const char* const SSL_CA_FILE = X509_get_default_cert_file_env();
        const char* ssl_cafile = ::getenv(SSL_CA_FILE);

        if (!ssl_cafile)
            ssl_cafile = X509_get_default_cert_file();

        int replace = true;
        if (!ssl_cafile || !fs::exists(fs::path(ssl_cafile))) {
            const char* bundle = nullptr;
            for (const char* b : CA_BUNDLES) {
                if (fs::exists(fs::path(b))) {
                    ::setenv(SSL_CA_FILE, bundle = b, replace);
                    break;
                }
            }

            if (!bundle)
                message = _u8L("Could not detect system SSL certificate store. "
                    "PrusaSlicer will be unable to establish secure "
                    "network connections.");
            else
                message = Slic3r::GUI::format(
                    _L("PrusaSlicer detected system SSL certificate store in: %1%"),
                    bundle);

            message += "\n" + Slic3r::GUI::format(
                _L("To specify the system certificate store manually, please "
                    "set the %1% environment variable to the correct CA bundle "
                    "and restart the application."),
                SSL_CA_FILE);
        }

#endif // OPENSSL_CERT_OVERRIDE

        if (CURLcode ec = ::curl_global_init(CURL_GLOBAL_DEFAULT)) {
            message += _u8L("CURL init has failed. PrusaSlicer will be unable to establish "
                "network connections. See logs for additional details.");

            BOOST_LOG_TRIVIAL(error) << ::curl_easy_strerror(ec);
        }
    }

    ~CurlGlobalInit() { ::curl_global_cleanup(); }
};

std::unique_ptr<CurlGlobalInit> CurlGlobalInit::sftp_instance;


struct Sftp::priv
{
    enum {
        DEFAULT_TIMEOUT_CONNECT = 10,
        DEFAULT_SIZE_LIMIT = 5 * 1024 * 1024,
    };

    ::CURL* curl;
    ::curl_slist* headerlist;
    // Used for storing file streams added as multipart form parts
    // Using a deque here because unlike vector it doesn't ivalidate pointers on insertion
    std::deque<fs::ifstream> form_files;
    std::string postfields;
    std::string src_fullpath;
    std::string error_buffer;    // Used for CURLOPT_ERRORBUFFER
    size_t limit;
    bool cancel;
    std::unique_ptr<fs::ifstream> uploadFile;

    std::thread io_thread;

    Sftp::CompleteFn completefn;
    Sftp::ErrorFn errorfn;
    Sftp::ProgressFn progressfn;

    priv(const std::string& url);
    ~priv();

    void set_timeout_connect(long timeout);
    static bool ca_file_supported(::CURL* curl);
    static size_t file_read_cb(char* buffer, size_t size, size_t nitems, void* userp);
    static int xfercb(void* userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

    void set_src_path(const std::string src_path);
    std::string curl_error(CURLcode curlcode);
    std::string body_size_error();
    void sftp_perform();
};

Sftp::priv::priv(const std::string& url)
    :curl(::curl_easy_init())
    , error_buffer(CURL_ERROR_SIZE + 1, '\0')
    , cancel(false)
{
    Sftp::tls_global_init();

    if (curl == nullptr) {
        throw Slic3r::RuntimeError(std::string("Could not construct Curl object"));
    }

    set_timeout_connect(DEFAULT_TIMEOUT_CONNECT);
    ::curl_easy_setopt(curl, CURLOPT_URL, url.c_str());   // curl makes a copy internally
    ::curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, &error_buffer.front());
}

Sftp::priv::~priv()
{
    ::curl_easy_cleanup(curl);
}

bool Sftp::priv::ca_file_supported(::CURL* curl)
{
#ifdef _WIN32
    bool res = false;
#else
    bool res = true;
#endif

    if (curl == nullptr) { return res; }

    ::curl_tlssessioninfo* tls;
    if (::curl_easy_getinfo(curl, CURLINFO_TLS_SSL_PTR, &tls) == CURLE_OK) {
        if (tls->backend == CURLSSLBACKEND_SCHANNEL || tls->backend == CURLSSLBACKEND_DARWINSSL) {
            // With Windows and OS X native SSL support, cert files cannot be set
            res = false;
        }
    }
    return res;
}


size_t Sftp::priv::file_read_cb(char* buffer, size_t size, size_t nitems, void* userp)
{
    auto stream = reinterpret_cast<fs::ifstream*>(userp);

    try {
        stream->read(buffer, size * nitems);
    }
    catch (const std::exception&) {
        return CURL_READFUNC_ABORT;
    }

    return stream->gcount();
}

int Sftp::priv::xfercb(void* userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    auto self = static_cast<priv*>(userp);
    bool cb_cancel = false;

    if (self->progressfn) {
        Progress progress(dltotal, dlnow, ultotal, ulnow);
        self->progressfn(progress, cb_cancel);
    }

    if (cb_cancel) { self->cancel = true; }

    return self->cancel;
}

std::string Sftp::priv::curl_error(CURLcode curlcode)
{
    return (boost::format("%1%:\n%2%\n[Error %3%]")
        % ::curl_easy_strerror(curlcode)
        % error_buffer.c_str()
        % curlcode
        ).str();
}

void Sftp::priv::set_timeout_connect(long timeout)
{
    ::curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout);
}

void Sftp::priv::set_src_path(const std::string src_path)
{
    src_fullpath = src_path;
    try {
        boost::filesystem::path path(src_path);
        boost::system::error_code ec;
        boost::uintmax_t filesize = file_size(path, ec);
        if (!ec) {
            uploadFile = std::make_unique<fs::ifstream>(path);
            ::curl_easy_setopt(curl, CURLOPT_READDATA, (void*)(uploadFile.get()));
            ::curl_easy_setopt(curl, CURLOPT_INFILESIZE, filesize);
        }
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(trace) << "Sftp::set_src_path failed!";
    }
}

void Sftp::priv::sftp_perform()
{
    ::curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    ::curl_easy_setopt(curl, CURLOPT_READFUNCTION, file_read_cb);

    ::curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    ::curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xfercb);
    ::curl_easy_setopt(curl, CURLOPT_XFERINFODATA, static_cast<void*>(this));
    
    CURLcode res = curl_easy_perform(curl);

    uploadFile.reset();

    if (res != CURLE_OK) {
        if (res == CURLE_ABORTED_BY_CALLBACK) {
            if (cancel) {
                // The abort comes from the request being cancelled programatically
                Progress dummyprogress(0, 0, 0, 0);
                bool cancel = true;
                if (progressfn) { progressfn(dummyprogress, cancel); }
            }
            else {
                // The abort comes from the CURLOPT_READFUNCTION callback, which means reading file failed
                if (errorfn) { errorfn("Error reading file for file upload"); }
            }
        } else {
            if (errorfn) { errorfn(curl_error(res)); }
        };
    }
    else {
        if (completefn) { completefn("Transform complete"); }
    }
}

// Public

Sftp::Sftp(Sftp&& other) : p(std::move(other.p)) {}


Sftp::~Sftp()
{
    assert(!p || !p->uploadFile);
    if (p && p->io_thread.joinable()) {
        p->io_thread.detach();
    }
}

Sftp& Sftp::on_complete(CompleteFn fn)
{
    if (p) { p->completefn = std::move(fn); }
    return *this;
}

Sftp& Sftp::on_error(ErrorFn fn)
{
    if (p) { p->errorfn = std::move(fn); }
    return *this;
}

Sftp& Sftp::ca_file(const std::string& name)
{
    if (p && priv::ca_file_supported(p->curl)) {
        ::curl_easy_setopt(p->curl, CURLOPT_CAINFO, name.c_str());
    }

    return *this;
}


Sftp& Sftp::on_progress(ProgressFn fn)
{
    if (p) { p->progressfn = std::move(fn); }
    return *this;
}

Sftp Sftp::upload(std::string url, std::string dst_path, std::string src_path, std::string user, std::string password)
{
    std::string full_url = (boost::format("sftp://%1%:%2%@%3%%4%") % user % password % url % dst_path).str();
    Sftp sftp(std::move(full_url));
    if (sftp.p) { sftp.p->set_src_path(src_path); }
    return sftp;
}


Sftp::Ptr Sftp::perform()
{
    auto self = std::make_shared<Sftp>(std::move(*this));

    if (self->p) {
        auto io_thread = std::thread([self]() {
            self->p->sftp_perform();
            });
        self->p->io_thread = std::move(io_thread);
    }

    return self;
}


void Sftp::perform_sync()
{
    if (p) { p->sftp_perform(); }
}


void Sftp::cancel()
{
    if (p) { p->cancel = true; }
}

std::string Sftp::tls_global_init()
{
    if (!CurlGlobalInit::sftp_instance)
        CurlGlobalInit::sftp_instance = std::make_unique<CurlGlobalInit>();

    return CurlGlobalInit::sftp_instance->message;
}

std::string Sftp::tls_system_cert_store()
{
    std::string ret;

#ifdef OPENSSL_CERT_OVERRIDE
    ret = ::getenv(X509_get_default_cert_file_env());
#endif

    return ret;
}


Sftp::Sftp(const std::string& url) : p(new priv(url)) {}

}