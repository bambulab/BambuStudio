#ifndef slic3r_Sftp_hpp_
#define slic3r_Sftp_hpp_

#include <memory>
#include <string>
#include <functional>
#include <boost/filesystem/path.hpp>


namespace BBL {

class Sftp : public std::enable_shared_from_this<Sftp> {

private:
	struct priv;
public:
	struct Progress
	{
		size_t dltotal;   // Total bytes to download
		size_t dlnow;     // Bytes downloaded so far
		size_t ultotal;   // Total bytes to upload
		size_t ulnow;     // Bytes uploaded so far

		Progress(size_t dltotal, size_t dlnow, size_t ultotal, size_t ulnow) :
			dltotal(dltotal), dlnow(dlnow), ultotal(ultotal), ulnow(ulnow)
		{}
	};

	typedef std::shared_ptr<Sftp> Ptr;
	typedef std::function<void(std::string /* body */)> CompleteFn;
	typedef std::function<void(std::string /* error */)> ErrorFn;

	// See the Progress struct above.
	// Writing true to the `cancel` reference cancels the request in progress.
	typedef std::function<void(Progress, bool& /* cancel */)> ProgressFn;

	Sftp(Sftp&& other);

	// for example, url is iot.qa.bbl or ip address
	static Sftp upload(std::string url, std::string src_path, std::string dst_path, std::string user, std::string password);

	~Sftp();

	// Sets a CA certificate file for usage with Sftp. This is only supported on some backends,
	// specifically, this is supported with OpenSSL and NOT supported with Windows and OS X native certificate store.
	// See also ca_file_supported().
	Sftp& ca_file(const std::string& filename);

	// Callback called on HTTP request complete
	Sftp& on_complete(CompleteFn fn);
	// Callback called on an error occuring at any stage of the requests: Url parsing, DNS lookup,
	// TCP connection, Sftp transfer, and finally also when the response indicates an error (status >= 400).
	// Therefore, a response body may or may not be present.
	Sftp& on_error(ErrorFn fn);
	// Callback called on data download/upload prorgess (called fairly frequently).
	// See the `Progress` structure for description of the data passed.
	// Writing a true-ish value into the cancel reference parameter cancels the request.
	Sftp& on_progress(ProgressFn fn);

	// Starts performing the request in a background thread
	Ptr perform();
	// Starts performing the request on the current thread
	void perform_sync();
	// Cancels a request in progress
	void cancel();

	// Return empty string on success or error message on fail.
	static std::string tls_global_init();
	static std::string tls_system_cert_store();
private:
	Sftp(const std::string& url);

	std::unique_ptr<priv> p;
};

std::ostream& operator<<(std::ostream&, const Sftp::Progress&);

}

#endif
