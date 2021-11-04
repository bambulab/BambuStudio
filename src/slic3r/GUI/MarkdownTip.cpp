#include "MarkdownTip.hpp"

#include "libslic3r/Utils.hpp"

#include <wx/webviewarchivehandler.h>

namespace Slic3r { namespace GUI {

// CMGUO

static std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
        std::string::value_type c = (*i);

        // Keep alphanumeric and other accepted characters intact
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
            continue;
        }

        // Any other characters are percent-encoded
        escaped << std::uppercase;
        escaped << '%' << std::setw(2) << int((unsigned char)c);
        escaped << std::nouppercase;
    }
    return escaped.str();
}
/*
 * Edge browser not support WebViewHandler
 * 
class MyWebViewHandler : public wxWebViewArchiveHandler
{
public:
    MyWebViewHandler() : wxWebViewArchiveHandler("tooltip") {}
    wxFSFile* GetFile(const wxString& uri) override {
        // file:///resources/tooltip/test.md
        wxFSFile* direct = wxWebViewArchiveHandler::GetFile(uri);
        if (direct)
            return direct;
        // file:///data/tooltips.zip;protocol=zip/test.md
        int n = uri.Find("resources/tooltip");
        if (n == wxString::npos)
            return direct;
        set_var_dir(data_dir());
        auto url = var("tooltips.zip");
        std::replace(url.begin(), url.end(), '\\', '/');
        auto uri2 = "file:///" + wxString(url) + ";protocol=zip" + uri.substr(n + 17);
        return wxWebViewArchiveHandler::GetFile(uri2);
    } 
};
*/

/*
TODO:
1. Fix height correctly��now h * 1.25 + 50
2. Async RunScript��avoid long call stack risc
3. Fetch markdown content in javascript (*)
4. Use scheme handler to support zip archive & make code tidy
*/

MarkdownTip::MarkdownTip()
    : wxFrame(NULL, wxID_ANY, "BBL MarkdownTip", { 0, 0 }, { 400, 300 }, wxBORDER_NONE)
{
    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

#ifndef __linux__
    _tipView = wxWebView::New();
    set_var_dir(resources_dir());
    auto url = var("tooltip/styled.html");
    std::replace(url.begin(), url.end(), '\\', '/');
    url = "file:///" + url;
    _tipView->Create(this, wxID_ANY, url, wxDefaultPosition, { 400, 300 }, wxBORDER_NONE);
    _tipView->Bind(wxEVT_WEBVIEW_LOADED, &MarkdownTip::OnLoaded, this);
    _tipView->Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &MarkdownTip::OnTitleChanged, this);
    _tipView->Bind(wxEVT_WEBVIEW_ERROR, &MarkdownTip::OnError, this);
    topsizer->Add(_tipView, wxSizerFlags().Expand().Proportion(1));
#endif

    SetSizer(topsizer);

    _timer = new wxTimer;
    _timer->Bind(wxEVT_TIMER, &MarkdownTip::OnTimer, this);
}

bool MarkdownTip::ShowTip(wxPoint pos, std::string const& tip)
{
    if (tip.empty()) {
        if (pos.x) {
            _hide = true;
            this->Hide();
        }
        else if (!_hide) {
            _hide = true;
            _timer->Start(1000, true);
        }
        return false;
    }
    if (_lastTip != tip) {
        auto content = LoadTip(tip);
        if (content.empty()) {
            _hide = true;
            this->Hide();
            return false;
        }
        auto script = "window.showMarkdown('" + url_encode(content) + "', true);";
        if (!_pendingScript.empty()) {
            _pendingScript = script;
        }
        else {
#ifndef __linux__
            _tipView->RunScript(script);
#endif
        }
        _lastTip = tip;
    }
    wxSize size = wxDisplay(wxDisplay::GetFromWindow(this)).GetClientArea().GetSize();
    _requestPos = pos;
    if (pos.y + this->GetSize().y > size.y)
        pos.y = size.y - this->GetSize().y;
    this->SetPosition(pos);
    _hide = false;
    this->Show();
    return true;
}

std::string MarkdownTip::LoadTip(std::string const& tip)
{
    set_var_dir(data_dir());
    auto file = var("tooltip/" + tip + ".md");
    wxFile f;
    file = var("tooltip/" + tip + ".md");
    if (wxFile::Exists(file) && f.Open(file)) {
        std::string content(f.Length(), 0);
        f.Read(&content[0], content.size());
        return content;
    }
    /*
    file = var("tooltips.zip");
    if (wxFile::Exists(file) && f.Open(file)) {
        wxFileInputStream fs(f);
        wxZipInputStream zip(fs);
        file = tip + ".md";
        while (auto e = zip.GetNextEntry()) {
            if (e->GetName() == file) {
                if (zip.OpenEntry(*e)) {
                    std::string content(f.Length(), 0);
                    zip.Read(&content[0], content.size());
                    return content;
                }
                break;
            }
        }
    }
    */
    set_var_dir(resources_dir());
    file = var("tooltip/" + tip + ".md");
    if (wxFile::Exists(file) && f.Open(file)) {
        std::string content(f.Length(), 0);
        f.Read(&content[0], content.size());
        return content;
    }
    return "";
}

void MarkdownTip::OnLoaded(wxWebViewEvent& event) {
}

void MarkdownTip::OnTitleChanged(wxWebViewEvent& event) {
    if (!_pendingScript.empty()) {
#ifndef __linux__
        _tipView->CallAfter([t = _tipView, script = _pendingScript]() {
            t->RunScript(script);
        });
#endif
        _pendingScript.clear();
        return;
    }
#ifdef __linux__
    wxString str = "0";
#else
    wxString str = event.GetString();
#endif
    double height = 0;
    if (str.ToDouble(&height)) {
        if (height > _lastHeight - 10 && height < _lastHeight + 10)
            return;
        _lastHeight = height;
        height *= 1.25; height += 50;
        wxSize size = wxDisplay(wxDisplay::GetFromWindow(this)).GetClientArea().GetSize();
        if (height > size.y)
            height = size.y;
        wxPoint pos = _requestPos;
        if (pos.y + height > size.y)
            pos.y = size.y - height;
        this->SetSize({ 400, (int)height });
        this->SetPosition(pos);
    }
}
void MarkdownTip::OnError(wxWebViewEvent& event) {
}

void MarkdownTip::OnTimer(wxTimerEvent& event) {
    if (_hide) {
        wxPoint pos = ScreenToClient(wxGetMousePosition());
        if (GetClientRect().Contains(pos)) {
            _timer->Start();
            return;
        }
        this->Hide();
    }
}


bool MarkdownTip::ShowTip(std::string const& tip, wxPoint pos)
{
    static MarkdownTip markdownTip;
    return markdownTip.ShowTip(pos, tip);
}

}
}
