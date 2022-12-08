#include "PresetUpdater.hpp"

#include <algorithm>
#include <thread>
#include <unordered_map>
#include <ostream>
#include <utility>
#include <stdexcept>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>

#include <wx/app.h>
#include <wx/msgdlg.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/format.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/UpdateDialogs.hpp"
#include "slic3r/GUI/ConfigWizard.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Config/Version.hpp"
#include "slic3r/Config/Snapshot.hpp"
#include "slic3r/GUI/MarkdownTip.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"

namespace fs = boost::filesystem;
using Slic3r::GUI::Config::Index;
using Slic3r::GUI::Config::Version;
using Slic3r::GUI::Config::Snapshot;
using Slic3r::GUI::Config::SnapshotDB;


// FIXME: Incompat bundle resolution doesn't deal with inherited user presets

namespace Slic3r {


static const char *INDEX_FILENAME = "index.idx";
static const char *TMP_EXTENSION = ".data";


void copy_file_fix(const fs::path &source, const fs::path &target)
{
	BOOST_LOG_TRIVIAL(debug) << format("PresetUpdater: Copying %1% -> %2%", source, target);
	std::string error_message;
	//CopyFileResult cfr = Slic3r::GUI::copy_file_gui(source.string(), target.string(), error_message, false);
	CopyFileResult cfr = copy_file(source.string(), target.string(), error_message, false);
	if (cfr != CopyFileResult::SUCCESS) {
		BOOST_LOG_TRIVIAL(error) << "Copying failed(" << cfr << "): " << error_message;
		throw Slic3r::CriticalException(GUI::format(
				_L("Copying of file %1% to %2% failed: %3%"),
				source, target, error_message));
	}
	// Permissions should be copied from the source file by copy_file(). We are not sure about the source
	// permissions, let's rewrite them with 644.
	static constexpr const auto perms = fs::owner_read | fs::owner_write | fs::group_read | fs::others_read;
	fs::permissions(target, perms);
}

//BBS: add directory copy
void copy_directory_fix(const fs::path &source, const fs::path &target)
{
    BOOST_LOG_TRIVIAL(debug) << format("PresetUpdater: Copying %1% -> %2%", source, target);
    std::string error_message;

    if (fs::exists(target))
        fs::remove_all(target);
    fs::create_directories(target);
    for (auto &dir_entry : boost::filesystem::directory_iterator(source))
    {
        std::string source_file = dir_entry.path().string();
        std::string name = dir_entry.path().filename().string();
        std::string target_file = target.string() + "/" + name;

        if (boost::filesystem::is_directory(dir_entry)) {
            const auto target_path = target / name;
            copy_directory_fix(dir_entry, target_path);
        }
        else {
            //CopyFileResult cfr = Slic3r::GUI::copy_file_gui(source_file, target_file, error_message, false);
            CopyFileResult cfr = copy_file(source_file, target_file, error_message, false);
            if (cfr != CopyFileResult::SUCCESS) {
                BOOST_LOG_TRIVIAL(error) << "Copying failed(" << cfr << "): " << error_message;
                throw Slic3r::CriticalException(GUI::format(
                    _L("Copying directory %1% to %2% failed: %3%"),
                    source, target, error_message));
            }
        }
    }
    return;
}

struct Update
{
	fs::path source;
	fs::path target;

	Version version;
	std::string vendor;
	//BBS: use changelog string instead of url
	std::string change_log;
	std::string descriptions;

	bool forced_update;
	//BBS: add directory support
	bool is_directory {false};

	Update() {}
	//BBS: add directory support
	//BBS: use changelog string instead of url
	Update(fs::path &&source, fs::path &&target, const Version &version, std::string vendor, std::string changelog, std::string description, bool forced = false, bool is_dir = false)
		: source(std::move(source))
		, target(std::move(target))
		, version(version)
		, vendor(std::move(vendor))
		, change_log(std::move(changelog))
		, descriptions(std::move(description))
		, forced_update(forced)
		, is_directory(is_dir)
	{}

	//BBS: add directory support
	void install() const
	{
	    if (is_directory) {
            copy_directory_fix(source, target);
        }
        else {
            copy_file_fix(source, target);
        }
	}

	friend std::ostream& operator<<(std::ostream& os, const Update &self)
	{
		os << "Update(" << self.source.string() << " -> " << self.target.string() << ')';
		return os;
	}
};

struct Incompat
{
	fs::path bundle;
	Version version;
	std::string vendor;
	//BBS: add directory support
	bool is_directory {false};

	Incompat(fs::path &&bundle, const Version &version, std::string vendor, bool is_dir = false)
		: bundle(std::move(bundle))
		, version(version)
		, vendor(std::move(vendor))
		, is_directory(is_dir)
	{}

	void remove() {
		// Remove the bundle file
		if (is_directory) {
			if (fs::exists(bundle))
                fs::remove_all(bundle);
		}
		else {
			if (fs::exists(bundle))
				fs::remove(bundle);
		}
	}

	friend std::ostream& operator<<(std::ostream& os , const Incompat &self) {
		os << "Incompat(" << self.bundle.string() << ')';
		return os;
	}
};

struct Updates
{
	std::vector<Incompat> incompats;
	std::vector<Update> updates;
};


wxDEFINE_EVENT(EVT_SLIC3R_VERSION_ONLINE, wxCommandEvent);
wxDEFINE_EVENT(EVT_SLIC3R_EXPERIMENTAL_VERSION_ONLINE, wxCommandEvent);


struct PresetUpdater::priv
{
	std::vector<Index> index_db;

	bool enabled_version_check;
	bool enabled_config_update;
	std::string version_check_url;

	fs::path cache_path;
	fs::path rsrc_path;
	fs::path vendor_path;

	bool cancel;
	std::thread thread;

	bool has_waiting_updates { false };
	Updates waiting_updates;

    struct Resource
    {
        std::string              version;
        std::string              description;
        std::string              url;
        std::string              cache_root;
        std::vector<std::string> sub_caches;
    };

    priv();

	void set_download_prefs(AppConfig *app_config);
	bool get_file(const std::string &url, const fs::path &target_path) const;
	//BBS: refine preset update logic
    bool extract_file(const fs::path &source_path, const fs::path &dest_path = {});
	void prune_tmps() const;
	void sync_version() const;
	void parse_version_string(const std::string& body) const;
    void sync_resources(std::string http_url, std::map<std::string, Resource> &resources, bool check_patch = false,  std::string current_version="");
    void sync_config(std::string http_url, const VendorMap vendors);
    void sync_tooltip(std::string http_url, std::string language);
    void sync_plugins(std::string http_url, std::string plugin_version);

	//BBS: refine preset update logic
	bool install_bundles_rsrc(std::vector<std::string> bundles, bool snapshot) const;
	void check_installed_vendor_profiles() const;
	Updates get_config_updates(const Semver& old_slic3r_version) const;
	bool perform_updates(Updates &&updates, bool snapshot = true) const;
	void set_waiting_updates(Updates u);
};

//BBS: change directories by design
PresetUpdater::priv::priv()
	: cache_path(fs::path(Slic3r::data_dir()) / "ota")
	, rsrc_path(fs::path(resources_dir()) / "profiles")
	, vendor_path(fs::path(Slic3r::data_dir()) / PRESET_SYSTEM_DIR)
	, cancel(false)
{
	//BBS: refine preset updater logic
	enabled_version_check = true;
	set_download_prefs(GUI::wxGetApp().app_config);
	// Install indicies from resources. Only installs those that are either missing or older than in resources.
	check_installed_vendor_profiles();
	// Load indices from the cache directory.
	//index_db = Index::load_db();
}

// Pull relevant preferences from AppConfig
void PresetUpdater::priv::set_download_prefs(AppConfig *app_config)
{
	version_check_url = app_config->version_check_url();
	//TODO: for debug currently
	if (version_check_url.empty())
		enabled_config_update = true;
	else
		enabled_config_update = false;
}

//BBS: refine the Preset Updater logic
// Downloads a file (http get operation). Cancels if the Updater is being destroyed.
bool PresetUpdater::priv::get_file(const std::string &url, const fs::path &target_path) const
{
    bool res = false;
    fs::path tmp_path = target_path;
    tmp_path += format(".%1%%2%", get_current_pid(), TMP_EXTENSION);

    BOOST_LOG_TRIVIAL(info) << format("[BBS Updater]download file `%1%`, stored to `%2%`, tmp path `%3%`",
        url,
        target_path.string(),
        tmp_path.string());

    Slic3r::Http::get(url)
        .on_progress([this](Slic3r::Http::Progress, bool &cancel_http) {
            if (cancel) {
                cancel_http = true;
            }
        })
        .on_error([&](std::string body, std::string error, unsigned http_status) {
            (void)body;
            BOOST_LOG_TRIVIAL(error) << format("[BBS Updater]getting: `%1%`: http status %2%, %3%",
                url,
                http_status,
                error);
        })
        .on_complete([&](std::string body, unsigned /* http_status */) {
            fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
            file.write(body.c_str(), body.size());
            file.close();
            fs::rename(tmp_path, target_path);
            res = true;
        })
        .perform_sync();

    return res;
}

//BBS: refine preset update logic
bool PresetUpdater::priv::extract_file(const fs::path &source_path, const fs::path &dest_path)
{
    bool res = true;
    std::string file_path = source_path.string();
    std::string parent_path = (!dest_path.empty() ? dest_path : source_path.parent_path()).string();
    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);

    if (!open_zip_reader(&archive, file_path))
    {
        BOOST_LOG_TRIVIAL(error) << "Unable to open zip reader for "<<file_path;
        return false;
    }

    mz_uint num_entries = mz_zip_reader_get_num_files(&archive);

    mz_zip_archive_file_stat stat;
    // we first loop the entries to read from the archive the .amf file only, in order to extract the version from it
    for (mz_uint i = 0; i < num_entries; ++i)
    {
        if (mz_zip_reader_file_stat(&archive, i, &stat))
        {
            std::string dest_file = parent_path+"/"+stat.m_filename;
            if (stat.m_is_directory) {
                fs::path dest_path(dest_file);
                if (!fs::exists(dest_path))
                    fs::create_directories(dest_path);
				continue;
            }
            else if (stat.m_uncomp_size == 0) {
                BOOST_LOG_TRIVIAL(warning) << "[BBL Updater]Unzip: invalid size for file "<<stat.m_filename;
                continue;
            }
            try
            {
                res = mz_zip_reader_extract_to_file(&archive, stat.m_file_index, dest_file.c_str(), 0);
                if (!res) {
                    BOOST_LOG_TRIVIAL(error) << "[BBL Updater]extract file "<<stat.m_filename<<" to dest "<<dest_file<<" failed";
                    close_zip_reader(&archive);
                    return res;
                }
                BOOST_LOG_TRIVIAL(info) << "[BBL Updater]successfully extract file " << stat.m_file_index << " to "<<dest_file;
            }
            catch (const std::exception& e)
            {
                // ensure the zip archive is closed and rethrow the exception
                close_zip_reader(&archive);
                BOOST_LOG_TRIVIAL(error) << "[BBL Updater]Archive read exception:"<<e.what();
                return false;
            }
        }
        else {
            BOOST_LOG_TRIVIAL(warning) << "[BBL Updater]Unzip: read file stat failed";
        }
    }
    close_zip_reader(&archive);

	return true;
}

// Remove leftover paritally downloaded files, if any.
void PresetUpdater::priv::prune_tmps() const
{
    for (auto &dir_entry : boost::filesystem::directory_iterator(cache_path))
		if (is_plain_file(dir_entry) && dir_entry.path().extension() == TMP_EXTENSION) {
			BOOST_LOG_TRIVIAL(debug) << "[BBL Updater]remove old cached files: " << dir_entry.path().string();
			fs::remove(dir_entry.path());
		}
}

//BBS: refine the Preset Updater logic
// Get Slic3rPE version available online, save in AppConfig.
void PresetUpdater::priv::sync_version() const
{
	if (! enabled_version_check) { return; }

#if 0
	Http::get(version_check_url)
		.size_limit(SLIC3R_VERSION_BODY_MAX)
		.on_progress([this](Http::Progress, bool &cancel) {
			cancel = this->cancel;
		})
		.on_error([&](std::string body, std::string error, unsigned http_status) {
			(void)body;
			BOOST_LOG_TRIVIAL(error) << format("Error getting: `%1%`: HTTP %2%, %3%",
				version_check_url,
				http_status,
				error);
		})
		.on_complete([&](std::string body, unsigned /* http_status */) {
			boost::trim(body);
			parse_version_string(body);
		})
		.perform_sync();
#endif
}

// Parses version string obtained in sync_version() and sends events to UI thread.
// Version string must contain release version on first line. Follows non-mandatory alpha / beta releases on following lines (alpha=2.0.0-alpha1).
void PresetUpdater::priv::parse_version_string(const std::string& body) const
{
#if 0
	// release version
	std::string version;
	const auto first_nl_pos = body.find_first_of("\n\r");
	if (first_nl_pos != std::string::npos)
		version = body.substr(0, first_nl_pos);
	else
		version = body;
	boost::optional<Semver> release_version = Semver::parse(version);
	if (!release_version) {
		BOOST_LOG_TRIVIAL(error) << format("Received invalid contents from `%1%`: Not a correct semver: `%2%`", SLIC3R_APP_NAME, version);
		return;
	}
	BOOST_LOG_TRIVIAL(info) << format("Got %1% online version: `%2%`. Sending to GUI thread...", SLIC3R_APP_NAME, version);
	wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_VERSION_ONLINE);
	evt->SetString(GUI::from_u8(version));
	GUI::wxGetApp().QueueEvent(evt);

	// alpha / beta version
	std::vector<std::string> prerelease_versions;
	size_t nexn_nl_pos = first_nl_pos;
	while (nexn_nl_pos != std::string::npos && body.size() > nexn_nl_pos + 1) {
		const auto last_nl_pos = nexn_nl_pos;
		nexn_nl_pos = body.find_first_of("\n\r", last_nl_pos + 1);
		std::string line;
		if (nexn_nl_pos == std::string::npos)
			line = body.substr(last_nl_pos + 1);
		else
			line = body.substr(last_nl_pos + 1, nexn_nl_pos - last_nl_pos - 1);

		// alpha
		if (line.substr(0, 6) == "alpha=") {
			version = line.substr(6);
			if (!Semver::parse(version)) {
				BOOST_LOG_TRIVIAL(error) << format("Received invalid contents for alpha release from `%1%`: Not a correct semver: `%2%`", SLIC3R_APP_NAME, version);
				return;
			}
			prerelease_versions.emplace_back(version);
		// beta
		}
		else if (line.substr(0, 5) == "beta=") {
			version = line.substr(5);
			if (!Semver::parse(version)) {
				BOOST_LOG_TRIVIAL(error) << format("Received invalid contents for beta release from `%1%`: Not a correct semver: `%2%`", SLIC3R_APP_NAME, version);
				return;
			}
			prerelease_versions.emplace_back(version);
		}
	}
	// find recent version that is newer than last full release.
	boost::optional<Semver> recent_version;
	for (const std::string& ver_string : prerelease_versions) {
		boost::optional<Semver> ver = Semver::parse(ver_string);
		if (ver && *release_version < *ver && ((recent_version && *recent_version < *ver) || !recent_version)) {
			recent_version = ver;
			version = ver_string;
		}
	}
	if (recent_version) {
		BOOST_LOG_TRIVIAL(info) << format("Got %1% online version: `%2%`. Sending to GUI thread...", SLIC3R_APP_NAME, version);
		wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_EXPERIMENTAL_VERSION_ONLINE);
		evt->SetString(GUI::from_u8(version));
		GUI::wxGetApp().QueueEvent(evt);
	}
#endif
    return;
}

//BBS: refine the Preset Updater logic
// Download vendor indices. Also download new bundles if an index indicates there's a new one available.
// Both are saved in cache.
void PresetUpdater::priv::sync_resources(std::string http_url, std::map<std::string, Resource> &resources, bool check_patch, std::string current_version_str)
{
    std::map<std::string, Resource>    resource_list;

    BOOST_LOG_TRIVIAL(info) << boost::format("[BBL Updater]: sync_resources get preferred setting version for app version %1%, url: %2%, current_version_str %3%, check_patch %4%")%SLIC3R_APP_NAME%http_url%current_version_str%check_patch;

    std::string query_params = "?";
    bool        first        = true;
    for (auto resource_it : resources) {
        if (cancel) { return; }
        auto resource_name = resource_it.first;
        boost::to_lower(resource_name);
        std::string query_resource = (boost::format("%1%=%2%")
            % resource_name % resource_it.second.version).str();
        if (!first) query_params += "&";
        query_params += query_resource;
        first = false;
    }

    std::string url = http_url;
    url += query_params;
    Slic3r::Http http = Slic3r::Http::get(url);
    BOOST_LOG_TRIVIAL(info) << boost::format("[BBL Updater]: sync_resources request_url: %1%")%url;
    http.on_progress([this](Slic3r::Http::Progress, bool &cancel_http) {
            if (cancel) {
                cancel_http = true;
            }
        })
        .on_complete([this, &resource_list, resources](std::string body, unsigned) {
            try {
                BOOST_LOG_TRIVIAL(info) << "[BBL Updater]: request_resources, body=" << body;

                json        j       = json::parse(body);
                std::string message = j["message"].get<std::string>();

                if (message == "success") {
                    json resource = j.at("resources");
                    if (resource.is_array()) {
                        for (auto iter = resource.begin(); iter != resource.end(); iter++) {
                            std::string version;
                            std::string url;
                            std::string resource;
                            std::string description;
                            for (auto sub_iter = iter.value().begin(); sub_iter != iter.value().end(); sub_iter++) {
                                if (boost::iequals(sub_iter.key(), "type")) {
                                    resource = sub_iter.value();
                                    BOOST_LOG_TRIVIAL(trace) << "[BBL Updater]: get version of settings's type, " << sub_iter.value();
                                } else if (boost::iequals(sub_iter.key(), "version")) {
                                    version = sub_iter.value();
                                } else if (boost::iequals(sub_iter.key(), "description")) {
                                    description = sub_iter.value();
                                } else if (boost::iequals(sub_iter.key(), "url")) {
                                    url = sub_iter.value();
                                }
                            }
                            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]: get type " << resource << ", version " << version << ", url " << url;

                            resource_list.emplace(resource, Resource{version, description, url});
                        }
                    }
                } else {
                    BOOST_LOG_TRIVIAL(error) << "[BBL Updater]: get version of settings failed, body=" << body;
                }
            } catch (std::exception &e) {
                BOOST_LOG_TRIVIAL(error) << (boost::format("[BBL Updater]: get version of settings failed, exception=%1% body=%2%") % e.what() % body).str();
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << "[BBL Updater]: get version of settings failed, body=" << body;
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("[BBL Updater]: status=%1%, error=%2%, body=%3%") % status % error % body;
        })
        .perform_sync();

    for (auto & resource_it : resources) {
        if (cancel) { return; }

        auto resource = resource_it.second;
        std::string resource_name = resource_it.first;
        boost::to_lower(resource_name);
        auto        resource_update = resource_list.find(resource_name);
        if (resource_update == resource_list.end()) {
            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]Vendor " << resource_name << " can not get setting versions online";
            continue;
        }
        Semver online_version = resource_update->second.version;
        // Semver current_version = get_version_from_json(vendor_root_config.string());
        Semver current_version = current_version_str.empty()?resource.version:current_version_str;
        bool version_match = ((online_version.maj() == current_version.maj()) && (online_version.min() == current_version.min()));
        if (version_match && check_patch) {
            int online_cc_patch = online_version.patch()/100;
            int current_cc_patch = current_version.patch()/100;
            if (online_cc_patch != current_cc_patch) {
                version_match = false;
                BOOST_LOG_TRIVIAL(warning) << boost::format("[BBL Updater]: online patch CC not match: online_cc_patch=%1%, current_cc_patch=%2%") % online_cc_patch % current_cc_patch;
            }
        }
        if (version_match && (current_version < online_version)) {
            if (cancel) { return; }

            // need to download the online files
            fs::path cache_path(resource.cache_root);
            std::string online_url      = resource_update->second.url;
            std::string cache_file_path = (fs::temp_directory_path() / (fs::unique_path().string() + TMP_EXTENSION)).string();
            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]Downloading resource: " << resource_name << ", version " << online_version.to_string();
            if (!get_file(online_url, cache_file_path)) {
                BOOST_LOG_TRIVIAL(warning) << "[BBL Updater]download resource " << resource_name << " failed, url: " << online_url;
                continue;
            }
            if (cancel) { return; }

            // remove previous files before
            if (resource.sub_caches.empty()) {
                if (fs::exists(cache_path)) {
                    fs::remove_all(cache_path);
                    BOOST_LOG_TRIVIAL(info) << "[BBL Updater]remove cache path " << cache_path.string();
                }
            } else {
                for (auto sub : resource.sub_caches) {
                    if (fs::exists(cache_path / sub)) {
                        fs::remove_all(cache_path / sub);
                        BOOST_LOG_TRIVIAL(info) << "[BBL Updater]remove cache path " << (cache_path / sub).string();
                    }
                }
            }
            // extract the file downloaded
            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]start to unzip the downloaded file " << cache_file_path << " to "<<cache_path;
            fs::create_directories(cache_path);
            if (!extract_file(cache_file_path, cache_path)) {
                BOOST_LOG_TRIVIAL(warning) << "[BBL Updater]extract resource " << resource_it.first << " failed, path: " << cache_file_path;
                continue;
            }
            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]finished unzip the downloaded file " << cache_file_path;

            if (!resource_update->second.description.empty()) {
                // save the description to disk
                std::string changelog_file = (cache_path / "changelog").string();

                boost::nowide::ofstream c;
                c.open(changelog_file, std::ios::out | std::ios::trunc);
                c << resource_update->second.description << std::endl;
                c.close();
            }

            resource_it.second = resource_update->second;
        }
        else {
            BOOST_LOG_TRIVIAL(warning) << boost::format("[BBL Updater]: online version=%1%, current_version=%2%, no need to download") % online_version.to_string() % current_version.to_string();
        }
    }
}

//BBS: refine the Preset Updater logic
// Download vendor indices. Also download new bundles if an index indicates there's a new one available.
// Both are saved in cache.
void PresetUpdater::priv::sync_config(std::string http_url, const VendorMap vendors)
{
    std::map<std::string, std::pair<Semver, std::string>> vendor_list;
    std::map<std::string, std::string> vendor_descriptions;
	BOOST_LOG_TRIVIAL(info) << "[BBL Updater]: sync_config Syncing configuration cache";

	if (!enabled_config_update) { return; }

    BOOST_LOG_TRIVIAL(info) << boost::format("[BBL Updater]: sync_config get preferred setting version for app version %1%, http_url: %2%")%SLIC3R_APP_NAME%http_url;

    std::string query_params = "?";
    bool first = true;
    for (auto vendor_it :vendors) {
        if (cancel) { return; }

        const VendorProfile& vendor_profile = vendor_it.second;
        std::string vendor_name = vendor_profile.id;
        boost::to_lower(vendor_name);

        std::string query_vendor = (boost::format("slicer/settings/%1%=%2%")
            % vendor_name
            % GUI::VersionInfo::convert_full_version(SLIC3R_VERSION)
            ).str();
        if (!first)
            query_params += "&";
        query_params += query_vendor;
    }

    std::string url = http_url;
    url += query_params;
    Slic3r::Http http = Slic3r::Http::get(url);
    BOOST_LOG_TRIVIAL(info) << boost::format("[BBL Updater]: sync_config request_url: %1%")%url;
    http.on_progress([this](Slic3r::Http::Progress, bool &cancel_http) {
            if (cancel) {
                cancel_http = true;
            }
        })
        .on_complete(
        [this, &vendor_list, &vendor_descriptions, vendors](std::string body, unsigned) {
            try {
                BOOST_LOG_TRIVIAL(info) << "[BBL Updater]::body=" << body;

                json j = json::parse(body);
                std::string message = j["message"].get<std::string>();

                if (message == "success") {
                    json resource =j.at("resources");
                    if (resource.is_array()) {
                        for (auto iter = resource.begin(); iter != resource.end(); iter++) {
                            Semver version;
                            std::string url;
                            std::string type;
                            std::string vendor;
                            std::string description;
                            for (auto sub_iter = iter.value().begin(); sub_iter != iter.value().end(); sub_iter++) {
                                if (boost::iequals(sub_iter.key(),"type")) {
                                    type = sub_iter.value();
                                    BOOST_LOG_TRIVIAL(trace) << "[BBL Updater]: get version of settings's type, " << sub_iter.value();
                                }
                                else if (boost::iequals(sub_iter.key(),"version")) {
                                    version = *(Semver::parse(sub_iter.value()));
                                }
                                else if (boost::iequals(sub_iter.key(),"description")) {
                                    description = sub_iter.value();
                                }
                                else if (boost::iequals(sub_iter.key(),"url")) {
                                    url = sub_iter.value();
                                }
                            }
                            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]: get type "<< type <<", version "<<version.to_string()<<", url " << url;

                            for (auto vendor_it :vendors) {
                                const VendorProfile& vendor_profile = vendor_it.second;
                                std::string vendor_name = vendor_profile.id;
                                boost::to_lower(vendor_name);
                                if (type.find(vendor_name) != std::string::npos) {
                                    vendor = vendor_profile.id;
                                    break;
                                }
                            }
                            if (!vendor.empty()) {
                                vendor_list.emplace(vendor, std::pair<Semver, std::string>(version, url));
                                vendor_descriptions.emplace(vendor, description);
                            }
                        }
                    }
                }
                else {
                    BOOST_LOG_TRIVIAL(error) << "[BBL Updater]: get version of settings failed, body=" << body;
                }
            }
            catch (std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << (boost::format("[BBL Updater]: get version of settings failed, exception=%1% body=%2%")
                    % e.what()
                    % body).str();
            }
            catch (...) {
                BOOST_LOG_TRIVIAL(error) << "[BBL Updater]: get version of settings failed,, body=" << body;
            }
        }
    )
    .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("[BBL Updater]: status=%1%, error=%2%, body=%3%")
                % status
                % error
                % body;
        }
    )
    .perform_sync();

    for (auto vendor_it :vendors) {
        if (cancel) { return; }

        const VendorProfile& vendor_profile = vendor_it.second;
        std::string vendor_name = vendor_profile.id;
        auto vendor_update = vendor_list.find(vendor_name);
        if (vendor_update == vendor_list.end()) {
			BOOST_LOG_TRIVIAL(info) << "[BBL Updater]Vendor " << vendor_name << " can not get setting versions online";
			continue;
		}
        Semver online_version = vendor_update->second.first;
        //Semver current_version = get_version_from_json(vendor_root_config.string());
        Semver current_version = vendor_profile.config_version;
        bool version_match = ((online_version.maj() == current_version.maj()) && (online_version.min() == current_version.min()));
        if (version_match && (current_version < online_version)) {
            auto cache_file = cache_path / (vendor_name+".json");
            auto cache_print_dir = (cache_path / vendor_name / PRESET_PRINT_NAME);
            auto cache_filament_dir = (cache_path / vendor_name / PRESET_FILAMENT_NAME);
            auto cache_machine_dir = (cache_path / vendor_name / PRESET_PRINTER_NAME);

            if (( fs::exists(cache_file))
                &&( fs::exists(cache_print_dir))
                &&( fs::exists(cache_filament_dir))
                &&( fs::exists(cache_machine_dir))) {
                Semver version = get_version_from_json(cache_file.string());
                bool cached_version_match = ((online_version.maj() == version.maj()) && (online_version.min() == version.min()));
                if (cached_version_match && (version >= online_version)) {
                    //already downloaded before
                    BOOST_LOG_TRIVIAL(info) << "[BBL Updater]Vendor " << vendor_name << ", already cached a version "<<version.to_string();
                    continue;
                }
            }
            if (cancel) { return; }

            //need to download the online files
            std::string online_url = vendor_update->second.second;
            std::string cache_file_path = (cache_path / (vendor_name + TMP_EXTENSION)).string();
            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]Downloading online settings for vendor: " << vendor_name<<", version "<<online_version.to_string();
            if (!get_file(online_url, cache_file_path)) {
                BOOST_LOG_TRIVIAL(warning) << "[BBL Updater]download settings for vendor "<<vendor_name<<" failed, url: " << online_url;
                continue;
            }
		    if (cancel) { return; }

            //remove previous files before
            if (fs::exists(cache_print_dir))
                fs::remove_all(cache_print_dir);
            if (fs::exists(cache_filament_dir))
                fs::remove_all(cache_filament_dir);
            if (fs::exists(cache_machine_dir))
                fs::remove_all(cache_machine_dir);
            //extract the file downloaded
            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]start to unzip the downloaded file "<< cache_file_path;
            if (!extract_file(cache_file_path)) {
                BOOST_LOG_TRIVIAL(warning) << "[BBL Updater]extract settings for vendor "<<vendor_name<<" failed, path: " << cache_file_path;
                continue;
            }
            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]finished unzip the downloaded file "<< cache_file_path;

            auto vendor_description = vendor_descriptions.find(vendor_name);
            if (vendor_description != vendor_descriptions.end()) {
                //save the description to disk
                std::string changelog_file = (cache_path / (vendor_name + ".changelog")).string();

                boost::nowide::ofstream c;
                c.open(changelog_file, std::ios::out | std::ios::trunc);
                c << vendor_description->second << std::endl;
                c.close();
            }
        }
    }
}

void PresetUpdater::priv::sync_tooltip(std::string http_url, std::string language)
{
    try {
        std::string common_version = "00.00.00.00";
        std::string language_version = "00.00.00.00";
        fs::path cache_root = fs::path(data_dir()) / "resources/tooltip";
        try {
            auto vf = cache_root / "common" / "version";
            if (fs::exists(vf)) fs::load_string_file(vf, common_version);
            vf = cache_root / language / "version";
            if (fs::exists(vf)) fs::load_string_file(vf, language_version);
        } catch (...) {}
        std::map<std::string, Resource> resources
        {
            {"slicer/tooltip/common", { common_version, "", "", (cache_root / "common").string() }},
            {"slicer/tooltip/" + language, { language_version, "", "", (cache_root / language).string() }}
        };
        sync_resources(http_url, resources);
        for (auto &r : resources) {
            if (!r.second.url.empty()) {
                GUI::MarkdownTip::Reload();
                break;
            }
        }
    }
    catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << format("[BBL Updater] sync_tooltip: %1%", e.what());
    }
}

void PresetUpdater::priv::sync_plugins(std::string http_url, std::string plugin_version)
{
    if (plugin_version == "00.00.00.00") {
        BOOST_LOG_TRIVIAL(info) << "non need to sync plugins for there is no plugins currently.";
        return;
    }
    std::string curr_version = SLIC3R_VERSION;
    std::string using_version = curr_version.substr(0, 9) + "00";

    try {
        std::map<std::string, Resource> resources
        {
            {"slicer/plugins/cloud", { using_version, "", "", cache_path.string(), {"plugins"}}}
        };
        sync_resources(http_url, resources, true, plugin_version);
    }
    catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << format("[BBL Updater] sync_plugins: %1%", e.what());
    }
}


bool PresetUpdater::priv::install_bundles_rsrc(std::vector<std::string> bundles, bool snapshot) const
{
	Updates updates;

	BOOST_LOG_TRIVIAL(info) << format("Installing %1% bundles from resources ...", bundles.size());

	for (const auto &bundle : bundles) {
		auto path_in_rsrc = (this->rsrc_path / bundle).replace_extension(".json");
		auto path_in_vendors = (this->vendor_path / bundle).replace_extension(".json");
		updates.updates.emplace_back(std::move(path_in_rsrc), std::move(path_in_vendors), Version(), bundle, "", "");

        //BBS: add directory support
        auto print_in_rsrc = (this->rsrc_path / bundle / PRESET_PRINT_NAME);
		auto print_in_vendors = (this->vendor_path / bundle / PRESET_PRINT_NAME);
        fs::path print_folder(print_in_vendors);
        if (fs::exists(print_folder))
            fs::remove_all(print_folder);
        fs::create_directories(print_folder);
		updates.updates.emplace_back(std::move(print_in_rsrc), std::move(print_in_vendors), Version(), bundle, "", "", false, true);

        auto filament_in_rsrc = (this->rsrc_path / bundle / PRESET_FILAMENT_NAME);
		auto filament_in_vendors = (this->vendor_path / bundle / PRESET_FILAMENT_NAME);
        fs::path filament_folder(filament_in_vendors);
        if (fs::exists(filament_folder))
            fs::remove_all(filament_folder);
        fs::create_directories(filament_folder);
		updates.updates.emplace_back(std::move(filament_in_rsrc), std::move(filament_in_vendors), Version(), bundle, "", "", false, true);

        auto machine_in_rsrc = (this->rsrc_path / bundle / PRESET_PRINTER_NAME);
		auto machine_in_vendors = (this->vendor_path / bundle / PRESET_PRINTER_NAME);
        fs::path machine_folder(machine_in_vendors);
        if (fs::exists(machine_folder))
            fs::remove_all(machine_folder);
        fs::create_directories(machine_folder);
		updates.updates.emplace_back(std::move(machine_in_rsrc), std::move(machine_in_vendors), Version(), bundle, "", "", false, true);
	}

	return perform_updates(std::move(updates), snapshot);
}


//BBS: refine preset update logic
// Install indicies from resources. Only installs those that are either missing or older than in resources.
void PresetUpdater::priv::check_installed_vendor_profiles() const
{
    BOOST_LOG_TRIVIAL(info) << "[BBL Updater]:Checking whether the profile from resource is newer";

    AppConfig *app_config = GUI::wxGetApp().app_config;
    const auto enabled_vendors = app_config->vendors();

    //BBS: refine the init check logic
    std::vector<std::string> bundles;
    for (auto &dir_entry : boost::filesystem::directory_iterator(rsrc_path)) {
        const auto &path = dir_entry.path();
        std::string file_path = path.string();
        if (is_json_file(file_path)) {
            const auto path_in_vendor = vendor_path / path.filename();
            std::string vendor_name = path.filename().string();
            // Remove the .json suffix.
            vendor_name.erase(vendor_name.size() - 5);
            if (enabled_config_update) {
                if ( fs::exists(path_in_vendor)) {
                    if (enabled_vendors.find(vendor_name) != enabled_vendors.end()) {
                        Semver resource_ver = get_version_from_json(file_path);
                        Semver vendor_ver = get_version_from_json(path_in_vendor.string());

                        bool version_match = ((resource_ver.maj() == vendor_ver.maj()) && (resource_ver.min() == vendor_ver.min()));

                        if (!version_match || (vendor_ver < resource_ver)) {
                            BOOST_LOG_TRIVIAL(info) << "[BBL Updater]:found vendor "<<vendor_name<<" newer version "<<resource_ver.to_string() <<" from resource, old version "<<vendor_ver.to_string();
                            bundles.push_back(vendor_name);
                        }
                    }
                    else {
                        //need to be removed because not installed
                        fs::remove(path_in_vendor);
                        const auto path_of_vendor = vendor_path / vendor_name;
                        if (fs::exists(path_of_vendor))
                            fs::remove_all(path_of_vendor);
                    }
                }
                else if ((vendor_name == PresetBundle::BBL_BUNDLE) || (enabled_vendors.find(vendor_name) != enabled_vendors.end())) {//if vendor has no file, copy it from resource for BBL
                    bundles.push_back(vendor_name);
                }
            }
            else if ((vendor_name == PresetBundle::BBL_BUNDLE) || (enabled_vendors.find(vendor_name) != enabled_vendors.end())) { //always update configs from resource to vendor for BBL
                bundles.push_back(vendor_name);
            }
        }
    }

    if (bundles.size() > 0)
        install_bundles_rsrc(bundles, false);
}

// Generates a list of bundle updates that are to be performed.
// Version of slic3r that was running the last time and which was read out from PrusaSlicer.ini is provided
// as a parameter.
//BBS: refine the Preset Updater logic
Updates PresetUpdater::priv::get_config_updates(const Semver &old_slic3r_version) const
{
	Updates updates;

	BOOST_LOG_TRIVIAL(info) << "[BBL Updater]:Checking for cached configuration updates...";

    for (auto &dir_entry : boost::filesystem::directory_iterator(cache_path)) {
        const auto &path = dir_entry.path();
        std::string file_path = path.string();
        if (is_json_file(file_path)) {
            const auto path_in_vendor = vendor_path / path.filename();
            std::string vendor_name = path.filename().string();
            // Remove the .json suffix.
            vendor_name.erase(vendor_name.size() - 5);
            auto print_in_cache = (cache_path / vendor_name / PRESET_PRINT_NAME);
            auto filament_in_cache = (cache_path / vendor_name / PRESET_FILAMENT_NAME);
            auto machine_in_cache = (cache_path / vendor_name / PRESET_PRINTER_NAME);

            if (( fs::exists(path_in_vendor))
                &&( fs::exists(print_in_cache))
                &&( fs::exists(filament_in_cache))
                &&( fs::exists(machine_in_cache))) {
                Semver vendor_ver = get_version_from_json(path_in_vendor.string());

                std::map<std::string, std::string> key_values;
                std::vector<std::string> keys(3);
				Semver cache_ver;
                keys[0] = BBL_JSON_KEY_VERSION;
                keys[1] = BBL_JSON_KEY_DESCRIPTION;
                keys[2] = BBL_JSON_KEY_FORCE_UPDATE;
                get_values_from_json(file_path, keys, key_values);
                std::string description = key_values[BBL_JSON_KEY_DESCRIPTION];
                bool force_update = false;
                if (key_values.find(BBL_JSON_KEY_FORCE_UPDATE) != key_values.end())
                    force_update = (key_values[BBL_JSON_KEY_FORCE_UPDATE] == "1")?true:false;
                auto config_version = Semver::parse(key_values[BBL_JSON_KEY_VERSION]);
                if (config_version)
                    cache_ver = *config_version;

                std::string changelog;
                std::string changelog_file = (cache_path / (vendor_name + ".changelog")).string();
                boost::nowide::ifstream ifs(changelog_file);
                if (ifs) {
                    std::ostringstream oss;
                    oss<< ifs.rdbuf();
                    changelog = oss.str();
                    //ifs>>changelog;
                    ifs.close();
                }

                bool version_match = ((vendor_ver.maj() == cache_ver.maj()) && (vendor_ver.min() == cache_ver.min()));
                if (version_match && (vendor_ver < cache_ver)) {
                    Semver app_ver = *Semver::parse(SLIC3R_VERSION);
                    if (cache_ver.maj() == app_ver.maj()){
                        BOOST_LOG_TRIVIAL(info) << "[BBL Updater]:need to update settings from "<<vendor_ver.to_string()<<" to newer version "<<cache_ver.to_string() <<", app version "<<SLIC3R_VERSION;
                        Version version;
                        version.config_version = cache_ver;
                        version.comment = description;

                        updates.updates.emplace_back(std::move(file_path), std::move(path_in_vendor.string()), std::move(version), vendor_name, changelog, description, force_update, false);

                        //BBS: add directory support
                        auto print_in_vendors = (vendor_path / vendor_name / PRESET_PRINT_NAME);
                        updates.updates.emplace_back(std::move(print_in_cache), std::move(print_in_vendors.string()), Version(), vendor_name, "", "", force_update, true);

                        auto filament_in_vendors = (vendor_path / vendor_name / PRESET_FILAMENT_NAME);
                        updates.updates.emplace_back(std::move(filament_in_cache), std::move(filament_in_vendors.string()), Version(), vendor_name, "", "", force_update, true);

                        auto machine_in_vendors = (vendor_path / vendor_name / PRESET_PRINTER_NAME);
                        updates.updates.emplace_back(std::move(machine_in_cache), std::move(machine_in_vendors.string()), Version(), vendor_name, "", "", force_update, true);
                    }
                }
            }
        }
    }

	return updates;
}

//BBS: switch to new BBL.json configs
bool PresetUpdater::priv::perform_updates(Updates &&updates, bool snapshot) const
{
    //std::string vendor_path;
    //std::string vendor_name;
    if (updates.incompats.size() > 0) {
        //if (snapshot) {
        //	BOOST_LOG_TRIVIAL(info) << "Taking a snapshot...";
        //	if (! GUI::Config::take_config_snapshot_cancel_on_error(*GUI::wxGetApp().app_config, Snapshot::SNAPSHOT_DOWNGRADE, "",
        //		_u8L("Continue and install configuration updates?")))
        //		return false;
        //}
        BOOST_LOG_TRIVIAL(info) << format("[BBL Updater]:Deleting %1% incompatible bundles", updates.incompats.size());

        for (auto &incompat : updates.incompats) {
            BOOST_LOG_TRIVIAL(info) << '\t' << incompat;
            incompat.remove();
        }
    } else if (updates.updates.size() > 0) {
        //if (snapshot) {
        //	BOOST_LOG_TRIVIAL(info) << "Taking a snapshot...";
        //	if (! GUI::Config::take_config_snapshot_cancel_on_error(*GUI::wxGetApp().app_config, Snapshot::SNAPSHOT_UPGRADE, "",
        //		_u8L("Continue and install configuration updates?")))
        //		return false;
        //}

        BOOST_LOG_TRIVIAL(info) << format("[BBL Updater]:Performing %1% updates", updates.updates.size());

        for (const auto &update : updates.updates) {
            BOOST_LOG_TRIVIAL(info) << '\t' << update;

            update.install();
            //if (!update.is_directory) {
            //    vendor_path = update.source.parent_path().string();
            //    vendor_name = update.vendor;
            //}
        }

        //if (!vendor_path.empty()) {
        //    PresetBundle bundle;
        //    // Throw when parsing invalid configuration. Only valid configuration is supposed to be provided over the air.
        //    bundle.load_vendor_configs_from_json(vendor_path, vendor_name, PresetBundle::LoadConfigBundleAttribute::LoadSystem, ForwardCompatibilitySubstitutionRule::Disable);

        //    BOOST_LOG_TRIVIAL(info) << format("Deleting %1% conflicting presets", bundle.prints.size() + bundle.filaments.size() + bundle.printers.size());

        //    auto preset_remover = [](const Preset& preset) {
        //        BOOST_LOG_TRIVIAL(info) << '\t' << preset.file;
        //        fs::remove(preset.file);
        //    };

        //    for (const auto &preset : bundle.prints)    { preset_remover(preset); }
        //    for (const auto &preset : bundle.filaments) { preset_remover(preset); }
        //    for (const auto &preset : bundle.printers)  { preset_remover(preset); }
        //}
    }

    return true;
}

void PresetUpdater::priv::set_waiting_updates(Updates u)
{
	waiting_updates = u;
	has_waiting_updates = true;
}

PresetUpdater::PresetUpdater() :
	p(new priv())
{}


// Public

PresetUpdater::~PresetUpdater()
{
	if (p && p->thread.joinable()) {
		// This will stop transfers being done by the thread, if any.
		// Cancelling takes some time, but should complete soon enough.
		p->cancel = true;
		p->thread.join();
	}
}

//BBS: change directories by design
//BBS: refine the preset updater logic
void PresetUpdater::sync(std::string http_url, std::string language, std::string plugin_version, PresetBundle *preset_bundle)
{
	//p->set_download_prefs(GUI::wxGetApp().app_config);
	if (!p->enabled_version_check && !p->enabled_config_update) { return; }

	// Copy the whole vendors data for use in the background thread
	// Unfortunatelly as of C++11, it needs to be copied again
	// into the closure (but perhaps the compiler can elide this).
	VendorMap vendors = preset_bundle->vendors;

	p->thread = std::thread([this, vendors, http_url, language, plugin_version]() {
		this->p->prune_tmps();
		if (p->cancel)
			return;
		this->p->sync_version();
		if (p->cancel)
			return;
		this->p->sync_config(http_url, std::move(vendors));
		if (p->cancel)
			return;
		this->p->sync_plugins(http_url, plugin_version);
		//if (p->cancel)
		//	return;
		//remove the tooltip currently
		//this->p->sync_tooltip(http_url, language);
	});
}

void PresetUpdater::slic3r_update_notify()
{
	if (! p->enabled_version_check)
		return;
}

static bool reload_configs_update_gui()
{
	wxString header = _L("Need to check the unsaved changes before configuration updates.");
	if (!GUI::wxGetApp().check_and_save_current_preset_changes(_L("Configuration updates"), header, false ))
		return false;

	// Reload global configuration
	auto* app_config = GUI::wxGetApp().app_config;
	// System profiles should not trigger any substitutions, user profiles may trigger substitutions, but these substitutions
	// were already presented to the user on application start up. Just do substitutions now and keep quiet about it.
	// However throw on substitutions in system profiles, those shall never happen with system profiles installed over the air.
	GUI::wxGetApp().preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::EnableSilentDisableSystem);
	GUI::wxGetApp().load_current_presets();
	GUI::wxGetApp().plater()->set_bed_shape();

	return true;
}

//BBS: refine the preset updater logic
PresetUpdater::UpdateResult PresetUpdater::config_update(const Semver& old_slic3r_version, UpdateParams params) const
{
    if (! p->enabled_config_update) { return R_NOOP; }

    auto updates = p->get_config_updates(old_slic3r_version);
    //if (updates.incompats.size() > 0) {
    //	BOOST_LOG_TRIVIAL(info) << format("%1% bundles incompatible. Asking for action...", updates.incompats.size());

    //	std::unordered_map<std::string, wxString> incompats_map;
    //	for (const auto &incompat : updates.incompats) {
    //		const auto min_slic3r = incompat.version.min_slic3r_version;
    //		const auto max_slic3r = incompat.version.max_slic3r_version;
    //		wxString restrictions;
    //		if (min_slic3r != Semver::zero() && max_slic3r != Semver::inf()) {
    //               restrictions = GUI::format_wxstr(_L("requires min. %s and max. %s"),
    //                   min_slic3r.to_string(),
    //                   max_slic3r.to_string());
    //		} else if (min_slic3r != Semver::zero()) {
    //			restrictions = GUI::format_wxstr(_L("requires min. %s"), min_slic3r.to_string());
    //			BOOST_LOG_TRIVIAL(debug) << "Bundle is not downgrade, user will now have to do whole wizard. This should not happen.";
    //		} else {
    //               restrictions = GUI::format_wxstr(_L("requires max. %s"), max_slic3r.to_string());
    //		}

    //		incompats_map.emplace(std::make_pair(incompat.vendor, std::move(restrictions)));
    //	}

    //	GUI::MsgDataIncompatible dlg(std::move(incompats_map));
    //	const auto res = dlg.ShowModal();
    //	if (res == wxID_REPLACE) {
    //		BOOST_LOG_TRIVIAL(info) << "User wants to re-configure...";

    //		// This effectively removes the incompatible bundles:
    //		// (snapshot is taken beforehand)
    //		if (! p->perform_updates(std::move(updates)) ||
    //			! GUI::wxGetApp().run_wizard(GUI::ConfigWizard::RR_DATA_INCOMPAT))
    //			return R_INCOMPAT_EXIT;

    //		return R_INCOMPAT_CONFIGURED;
    //	}
    //	else {
    //		BOOST_LOG_TRIVIAL(info) << "User wants to exit Slic3r, bye...";
    //		return R_INCOMPAT_EXIT;
    //	}

    //} else
    if (updates.updates.size() > 0) {

        bool force_update = false;
        for (const auto& update : updates.updates) {
            force_update = (update.forced_update ? true : force_update);
            //td::cout << update.forced_update << std::endl;
            //BOOST_LOG_TRIVIAL(info) << format("Update requires higher version.");
        }

        //forced update
        if (force_update)
        {
            BOOST_LOG_TRIVIAL(info) << format("[BBL Updater]:Force updating will start, size %1% ", updates.updates.size());
            bool ret = p->perform_updates(std::move(updates));
            if (!ret) {
                BOOST_LOG_TRIVIAL(warning) << format("[BBL Updater]:perform_updates failed");
                return R_INCOMPAT_EXIT;
            }

            ret = reload_configs_update_gui();
            if (!ret) {
                BOOST_LOG_TRIVIAL(warning) << format("[BBL Updater]:reload_configs_update_gui failed");
                return R_INCOMPAT_EXIT;
            }
            Semver cur_ver = GUI::wxGetApp().preset_bundle->get_vendor_profile_version(PresetBundle::BBL_BUNDLE);

            GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::PresetUpdateFinished, GUI::NotificationManager::NotificationLevel::ImportantNotificationLevel,  _u8L("Configuration package updated to ")+cur_ver.to_string());

            return R_UPDATE_INSTALLED;
        }

        // regular update
        if (params == UpdateParams::SHOW_NOTIFICATION) {
            p->set_waiting_updates(updates);
            GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::PresetUpdateAvailable);
        }
        else {
            BOOST_LOG_TRIVIAL(info) << format("[BBL Updater]:Configuration package available. size %1%, need to confirm...", p->waiting_updates.updates.size());

            std::vector<GUI::MsgUpdateConfig::Update> updates_msg;
            for (const auto& update : updates.updates) {
                //BBS: skip directory
                if (update.is_directory)
                    continue;
                std::string changelog = update.change_log;
                updates_msg.emplace_back(update.vendor, update.version.config_version, update.descriptions, std::move(changelog));
            }

            GUI::MsgUpdateConfig dlg(updates_msg, params == UpdateParams::FORCED_BEFORE_WIZARD);

            const auto res = dlg.ShowModal();
            if (res == wxID_OK) {
                BOOST_LOG_TRIVIAL(debug) << "[BBL Updater]:selected yes to update";
                if (! p->perform_updates(std::move(updates)) ||
                    ! reload_configs_update_gui())
                    return R_ALL_CANCELED;
                return R_UPDATE_INSTALLED;
            }
            else {
                BOOST_LOG_TRIVIAL(info) << "[BBL Updater]:selected no for updating";
                if (params == UpdateParams::FORCED_BEFORE_WIZARD && res == wxID_CANCEL)
                    return R_ALL_CANCELED;
                return R_UPDATE_REJECT;
            }
        }

        // MsgUpdateConfig will show after the notificaation is clicked
    } else {
        BOOST_LOG_TRIVIAL(info) << "[BBL Updater]:No configuration updates available.";
    }

	return R_NOOP;
}

//BBS: add json related logic
bool PresetUpdater::install_bundles_rsrc(std::vector<std::string> bundles, bool snapshot) const
{
	return p->install_bundles_rsrc(bundles, snapshot);
}

void PresetUpdater::on_update_notification_confirm()
{
	if (!p->has_waiting_updates)
		return;
	BOOST_LOG_TRIVIAL(info) << format("Update of %1% bundles available. Asking for confirmation ...", p->waiting_updates.updates.size());

	std::vector<GUI::MsgUpdateConfig::Update> updates_msg;
	for (const auto& update : p->waiting_updates.updates) {
		//BBS: skip directory
		if (update.is_directory)
			continue;
		std::string changelog = update.change_log;
		updates_msg.emplace_back(update.vendor, update.version.config_version, update.descriptions, std::move(changelog));
	}

	GUI::MsgUpdateConfig dlg(updates_msg);

	const auto res = dlg.ShowModal();
	if (res == wxID_OK) {
		BOOST_LOG_TRIVIAL(debug) << "User agreed to perform the update";
		if (p->perform_updates(std::move(p->waiting_updates)) &&
			reload_configs_update_gui()) {
			p->has_waiting_updates = false;
		}
	}
	else {
		BOOST_LOG_TRIVIAL(info) << "User refused the update";
	}
}

bool PresetUpdater::version_check_enabled() const
{
	return p->enabled_version_check;
}

}
