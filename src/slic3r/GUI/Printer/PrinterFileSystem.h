#ifndef slic3r_GUI_PrinterFileSystem_h_
#define slic3r_GUI_PrinterFileSystem_h_

#include "BambuTunnel.h"

#include <wx/event.h>

#include <boost/thread.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <functional>
#include <deque>

namespace pt = boost::property_tree;

wxDECLARE_EVENT(EVT_FILE_CHANGED, wxCommandEvent);

class PrinterFileSystem : public wxEvtHandler, public boost::enable_shared_from_this<PrinterFileSystem>
{
    static const int PRINTER_REQ = 0x1000;
    static const int PRINTER_RESP   = 0x1001;

    static const int FILE_LIST_ALL = 1;
    static const int FILE_DELETE  = 2;
    static const int FILE_DOWNLOAD  = 3;
    static const int FILE_THUMBNAIL = 4;

    static const int ERROR_PIPE  = -1;
    static const int ERROR_CONT  = 1; // continue
    static const int ERROR_ITEM  = 2;
    static const int ERROR_JSON  = 100;
    static const int ERROR_SIZE  = 101;
    static const int ERROR_MD5   = 102;


public:
    PrinterFileSystem(std::string const & url, void * logger);

    ~PrinterFileSystem();

public:
    enum GroupMode {
        G_NONE, 
        G_MONTH,
        G_YEAR,
    };

    void SetGroupMode(GroupMode mode);

    int EnterSubGroup(int index);

    GroupMode GetGroupMode() const { return mode; }

    template<typename T> using Callback = std::function<void(int, T)>;

    struct File
    {
        std::string name;
        wxInt64     time;
        wxInt64     size;
        wxBitmap    thumbnail;

        friend bool operator<(File const & l, File const & r) { return l.time > r.time; }
    };

    struct Void {};

    typedef std::vector<File> FileList;

    struct Thumbnail
    {
        int         index;
        std::string name;
        wxBitmap    thumbnail;
    };

    struct Progress
    {
        wxInt64 size;
        wxInt64 total;
    };

    void ListAllFiles(Callback<Void> const &callback);

    void DeleteFile(int index, Callback<Void> const &callback);

    void DownloadFile(int index, std::string const & path, Callback<Progress> const &callback);

    int GetCount() const;

    int GetIndexAtTime(wxInt64 time);

    void LockFiles(int start, int count, Callback<Thumbnail> const &callback);

    File const & GetFile(int index);

    void UnlockFiles(int start, int count);

private:
    template<typename T> using Translator = std::function<int(pt::ptree const & resp, T &)>;

    template<typename T, typename MT> using Applier = std::function<int(MT &&, T &)>;

    typedef std::function<void(int, pt::ptree const & resp)> callback_t;

    template <typename T, typename MT = T>
    struct NullApplier
    {
        T operator()(MT const &) { return T(); }
    };

    template <typename T>
    struct NullApplier<T, T>
    {
        T const & operator()(T const & t) { return t; }
    };

    template <typename T, typename MT = T>
    void SendRequest(int type, pt::ptree const& req, Translator<MT> const& translator, Applier<T, MT> const& applier, Callback<T> const& callback)
    {
        auto c = [translator, applier, callback, thiz = shared_from_this()](int result, pt::ptree const& resp)
        {
            MT mt;
            if (result == 0 || result == ERROR_CONT) {
                try {
                    int n = (translator != nullptr) ? translator(resp, mt) : 0;
                    result = n == 0 ? result : n;
                }
                catch (...) {
                    result = ERROR_JSON;
                }
            }
            // TODO: clear this callback if not continue
            if (applier) {
                thiz->PostCallback<MT>([callback, applier, thiz](int result, MT mt) {
                    T t;
                    if (result == 0 || result == ERROR_CONT)
                        result = applier(std::move(mt), t);
                    callback(result, t);
                }, result, mt);
            } else {
                thiz->PostCallback<T>(callback, result, NullApplier<T, MT>()(mt));
            }
        };
        SendRequest(type, req, c);
    }

    void SendRequest(int type, pt::ptree const &req, callback_t const & callback);

    void RecvMessageThread();

    int RecvData(std::function<int(Bambu_Sample & sample)> const & callback);

    template <typename T>
    void PostCallback(Callback<T> const& callback, int result, T const& resp)
    {
        PostCallback([=] { callback(result, resp); });
    }

    void PostCallback(std::function<void(void)> const & callback);

    PrinterFileSystem(PrinterFileSystem *parent);

private:
    void BuildGroups();

    void FileRemoved(int index, std::string const & name);

    void SendChangedEvent();

protected:
    GroupMode mode = G_NONE;
    FileList files;
    std::vector<int> group_year;
    std::vector<int> group_month;

private:
    std::vector<int> thumbnail_requests;

private:
    std::string url;
    Bambu_Session session;
    size_t sequence;
    std::deque<callback_t> callbacks;
    bool stop = false;
    boost::thread recv_thread;
    boost::mutex mutex;
    wxEvtHandler * event_handler;
};

#endif // !slic3r_GUI_PrinterFileSystem_h_
