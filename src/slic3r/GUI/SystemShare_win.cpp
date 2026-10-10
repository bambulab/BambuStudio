#include "SystemShare.hpp"

#ifdef _WIN32

// The Windows native share UI is the system "Share" flyout that's been
// available since Windows 10 1607. It's reached programmatically via the
// WinRT `Windows.ApplicationModel.DataTransfer.DataTransferManager` class
// plus the `IDataTransferManagerInterop` COM bridge that lets a plain Win32
// HWND drive what was originally a UWP-only API.
//
// Since the WinRT headers (winrt/windows.applicationmodel.datatransfer.h)
// only ship with the Windows 10 SDK, the body of this file is gated on
// HAS_WIN10SDK -- the same gate used by Utils/FixModelByWin10.cpp. When the
// gate is off, HasNativePicker() returns false and SystemShare.cpp omits
// the "Share..." menu item. This matches the Linux / no-SDK path served by
// SystemShare_fallback.cpp, but lets a Windows build that DOES have the
// SDK get the full system share flyout.
//
// Activation pattern follows Utils/FixModelByWin10.cpp: dynamic-load
// ComBase.dll, look up RoInitialize / RoGetActivationFactory at runtime,
// activate by class-name HSTRING. This avoids hard-linking to the WinRT
// runtime so that older Windows installs still load BambuStudio.exe even
// if they can't reach this code path.

#ifdef HAS_WIN10SDK

#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <windows.h>
#include <roapi.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>

#include <winrt/windows.applicationmodel.datatransfer.h>
#include <winrt/windows.foundation.h>
#include <shobjidl.h> // IDataTransferManagerInterop

#include <wx/window.h>

namespace ABI_DT  = ABI::Windows::ApplicationModel::DataTransfer;
namespace ABI_F   = ABI::Windows::Foundation;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;

namespace Slic3r { namespace GUI { namespace SystemShare {

// Forward decls of the runtime entry points we look up dynamically. Same
// shape as Utils/FixModelByWin10.cpp -- by going through GetProcAddress we
// don't add a load-time dependency on combase.dll's WinRT entry points.
extern "C" {
    typedef HRESULT (__stdcall *PFRoInitialize)(int);
    typedef HRESULT (__stdcall *PFRoUninitialize)();
    typedef HRESULT (__stdcall *PFRoGetActivationFactory)(HSTRING, REFIID, void **);
    typedef HRESULT (__stdcall *PFWindowsCreateString)(LPCWSTR, UINT32, HSTRING *);
    typedef HRESULT (__stdcall *PFWindowsDeleteString)(HSTRING);
}

namespace {

// Lazy-load the WinRT entry points the first time we share. Returns false
// if combase.dll isn't on the system or any required symbol is missing.
struct WinRtEntry {
    HMODULE                     dll{ nullptr };
    PFRoInitialize              RoInitialize{ nullptr };
    PFRoUninitialize            RoUninitialize{ nullptr };
    PFRoGetActivationFactory    RoGetActivationFactory{ nullptr };
    PFWindowsCreateString       WindowsCreateString{ nullptr };
    PFWindowsDeleteString       WindowsDeleteString{ nullptr };
    bool                        ok{ false };

    bool ensure_loaded() {
        if (ok) return true;
        if (!dll) dll = LoadLibraryW(L"ComBase.dll");
        if (!dll) return false;
        RoInitialize           = (PFRoInitialize)           GetProcAddress(dll, "RoInitialize");
        RoUninitialize         = (PFRoUninitialize)         GetProcAddress(dll, "RoUninitialize");
        RoGetActivationFactory = (PFRoGetActivationFactory) GetProcAddress(dll, "RoGetActivationFactory");
        WindowsCreateString    = (PFWindowsCreateString)    GetProcAddress(dll, "WindowsCreateString");
        WindowsDeleteString    = (PFWindowsDeleteString)    GetProcAddress(dll, "WindowsDeleteString");
        ok = RoInitialize && RoUninitialize && RoGetActivationFactory
             && WindowsCreateString && WindowsDeleteString;
        return ok;
    }
};

WinRtEntry &winrt() { static WinRtEntry e; return e; }

} // anonymous namespace

// HasNativePicker decides whether the SystemShare cross-platform menu
// includes the "Share..." item. Returning false here causes the menu to
// fall back to the Open Link / Copy Link pair only. We consult
// ensure_loaded() so that older Windows installs lacking ComBase.dll's
// WinRT entry points (or any of the specific symbols we depend on) hide
// the menu item entirely instead of advertising a feature that would
// silently no-op when invoked.
//
// ensure_loaded() is idempotent and caches its result, so the lazy load
// happens once at the first share-affordance state check; subsequent
// calls are O(1).
bool HasNativePicker() { return winrt().ensure_loaded(); }

namespace {

// RAII wrapper for HSTRING.
struct HStringHolder {
    HSTRING h{ nullptr };
    HRESULT make(LPCWSTR s) {
        if (!winrt().ensure_loaded()) return E_FAIL;
        return winrt().WindowsCreateString(s, (UINT32) wcslen(s), &h);
    }
    ~HStringHolder() {
        if (h && winrt().WindowsDeleteString) winrt().WindowsDeleteString(h);
    }
};

// IDataTransferManagerInterop GUID. Defined in shobjidl.h on recent SDKs;
// repeated here so the build works on slightly older SDKs that lack the
// definition but still ship the header.
const GUID IID_IDataTransferManagerInterop_local = {
    0x3A3DCD6C, 0x3EAB, 0x43DC,
    {0xBC, 0xDE, 0x45, 0x67, 0x1C, 0xE8, 0x00, 0xC8}
};

} // namespace

void OpenNativePicker(wxWindow *anchor, const wxString &url, const wxString &title)
{
    if (url.IsEmpty()) return;
    if (!winrt().ensure_loaded()) return;

    // Find the parent HWND. wxWindow::GetHandle() returns the underlying
    // HWND on Windows. The share flyout anchors to the top-level window.
    HWND hwnd = anchor ? (HWND) anchor->GetHandle() : nullptr;
    if (!hwnd) hwnd = GetActiveWindow();
    if (!hwnd) return;

    // Initialize WinRT for this thread (idempotent; safe to call repeatedly).
    HRESULT init_hr = winrt().RoInitialize(RO_INIT_SINGLETHREADED);
    const bool initialized = SUCCEEDED(init_hr) || init_hr == RPC_E_CHANGED_MODE
                             || init_hr == S_FALSE;

    // Resolve the DataTransferManager statics via the interop COM bridge.
    HStringHolder cls_static;
    if (FAILED(cls_static.make(L"Windows.ApplicationModel.DataTransfer.DataTransferManager")))
        return;

    ComPtr<IDataTransferManagerInterop> interop;
    HRESULT hr = winrt().RoGetActivationFactory(cls_static.h,
                                                IID_IDataTransferManagerInterop_local,
                                                reinterpret_cast<void **>(interop.GetAddressOf()));
    if (FAILED(hr) || !interop) {
        if (initialized) winrt().RoUninitialize();
        return;
    }

    // Bind the manager to our HWND.
    ComPtr<ABI_DT::IDataTransferManager> dtm;
    hr = interop->GetForWindow(hwnd, IID_PPV_ARGS(&dtm));
    if (FAILED(hr) || !dtm) {
        if (initialized) winrt().RoUninitialize();
        return;
    }

    // Hook the DataRequested event. The handler runs when the user actually
    // commits the share (picks a target). It fills the data package with the
    // URI + title.
    //
    // Capture url/title by value so they survive the async callback.
    std::wstring wurl   = url.ToStdWstring();
    std::wstring wtitle = title.ToStdWstring();
    if (wtitle.empty()) wtitle = wurl;

    // We hand both the cookie returned by add_DataRequested and the DTM
    // pointer itself into the lambda by shared_ptr so the handler can
    // unregister itself when it fires. Without that, every SystemShare
    // call appends a new handler to the DTM's internal list (DTM is a
    // per-HWND singleton; add_DataRequested is additive). Subsequent
    // shares would invoke every handler in registration order, each
    // overwriting the previous one's data package on the request -- not
    // user-visible-broken (the last write wins) but a leak that grows
    // linearly with the number of shares.
    auto cookie = std::make_shared<EventRegistrationToken>();
    auto dtm_for_handler = dtm;
    auto handler = Callback<ABI_F::ITypedEventHandler<
        ABI_DT::DataTransferManager *, ABI_DT::DataRequestedEventArgs *>>(
        [wurl, wtitle, cookie, dtm_for_handler](ABI_DT::IDataTransferManager *,
                                                ABI_DT::IDataRequestedEventArgs *args) -> HRESULT {
            // Unregister ourselves first thing -- whatever the handler does
            // below, this invocation is the last for this share request, so
            // we don't want to stay subscribed. By the time the handler
            // fires, add_DataRequested has returned and cookie has been
            // populated by the OS.
            if (dtm_for_handler)
                dtm_for_handler->remove_DataRequested(*cookie);

            if (!args) return E_INVALIDARG;
            ComPtr<ABI_DT::IDataRequest> request;
            HRESULT hr = args->get_Request(&request);
            if (FAILED(hr) || !request) return hr;
            ComPtr<ABI_DT::IDataPackage> data;
            hr = request->get_Data(&data);
            if (FAILED(hr) || !data) return hr;
            ComPtr<ABI_DT::IDataPackagePropertySet> props;
            hr = data->get_Properties(&props);
            if (FAILED(hr) || !props) return hr;

            HStringHolder h_title;
            if (SUCCEEDED(h_title.make(wtitle.c_str())))
                props->put_Title(h_title.h);

            // Set the URI as the canonical share payload.
            ComPtr<ABI_F::IUriRuntimeClassFactory> uri_factory;
            HStringHolder cls_uri;
            if (FAILED(cls_uri.make(L"Windows.Foundation.Uri")))
                return S_OK; // best-effort
            HRESULT fr = winrt().RoGetActivationFactory(cls_uri.h,
                            __uuidof(ABI_F::IUriRuntimeClassFactory),
                            reinterpret_cast<void **>(uri_factory.GetAddressOf()));
            if (SUCCEEDED(fr) && uri_factory) {
                HStringHolder h_url;
                if (SUCCEEDED(h_url.make(wurl.c_str()))) {
                    ComPtr<ABI_F::IUriRuntimeClass> uri_obj;
                    if (SUCCEEDED(uri_factory->CreateUri(h_url.h, &uri_obj)) && uri_obj) {
                        data->SetWebLink(uri_obj.Get());
                    }
                }
            }
            return S_OK;
        });

    HRESULT add_hr = E_FAIL;
    if (handler) {
        add_hr = dtm->add_DataRequested(handler.Get(), cookie.get());
    }

    // Show the share UI. Returns asynchronously; the system handles the
    // popup. We don't remove the handler here -- DataRequested fires
    // synchronously inside ShowShareUIForWindow as the system populates
    // the data package, regardless of whether the user later commits to
    // a target or dismisses the flyout. The handler removes itself on
    // fire, so the registration list doesn't grow over the process
    // lifetime.
    HRESULT show_hr = E_FAIL;
    if (SUCCEEDED(add_hr))
        show_hr = interop->ShowShareUIForWindow(hwnd);

    // If ShowShareUIForWindow itself failed (bad HWND, system share UI
    // unavailable, etc.) the handler will never fire, so we must
    // unregister explicitly to avoid leaking it to the next share call.
    if (SUCCEEDED(add_hr) && FAILED(show_hr)) {
        dtm->remove_DataRequested(*cookie);
    }

    // Note: do NOT call RoUninitialize() here. The flyout is async and
    // will reach back into WinRT after we return. Leaving WinRT initialized
    // for the lifetime of the process is safe and matches what
    // FixModelByWin10.cpp does.
}

}}} // namespace Slic3r::GUI::SystemShare

#else // !HAS_WIN10SDK

// Without the Win10 SDK at compile time we can't talk to
// DataTransferManager. Surface the same fallback shape as Linux: no
// "Share..." menu item, just Open Link / Copy Link.
namespace Slic3r { namespace GUI { namespace SystemShare {
bool HasNativePicker() { return false; }
void OpenNativePicker(wxWindow *, const wxString &, const wxString &) {}
}}}

#endif // HAS_WIN10SDK

#endif // _WIN32
