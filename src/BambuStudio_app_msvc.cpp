// Why?
#define _WIN32_WINNT 0x0502
// The standard Windows includes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include <wchar.h>



#ifdef SLIC3R_GUI
extern "C"
{
    // Let the NVIDIA and AMD know we want to use their graphics card
    // on a dual graphics card system.
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000000;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0;
}
#endif /* SLIC3R_GUI */

#include <stdlib.h>
#include <stdio.h>

#ifdef SLIC3R_GUI
    #include <GL/GL.h>
#endif /* SLIC3R_GUI */

#include <string>
#include <vector>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <stdio.h>

extern "C" {
    typedef int (__stdcall *Slic3rMainFunc)(int argc, wchar_t **argv);
    Slic3rMainFunc bambustu_main = nullptr;
}

extern "C" {
#ifdef SLIC3R_WRAPPER_NOCONSOLE
int APIENTRY wWinMain(HINSTANCE /* hInstance */, HINSTANCE /* hPrevInstance */, PWSTR /* lpCmdLine */, int /* nCmdShow */)
{
    int 	  argc;
    wchar_t **argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
#else
int wmain(int argc, wchar_t **argv)
{
#endif
    // Allow the asserts to open message box, such message box allows to ignore the assert and continue with the application.
    // Without this call, the seemingly same message box is being opened by the abort() function, but that is too late and
    // the application will be killed even if "Ignore" button is pressed.
    _set_error_mode(_OUT_TO_MSGBOX);

    std::vector<wchar_t*> argv_extended;
    argv_extended.emplace_back(argv[0]);

#ifdef SLIC3R_WRAPPER_GCODEVIEWER
    wchar_t gcodeviewer_param[] = L"--gcodeviewer";
    argv_extended.emplace_back(gcodeviewer_param);
#endif /* SLIC3R_WRAPPER_GCODEVIEWER */

#ifdef SLIC3R_GUI
    // Here one may push some additional parameters based on the wrapper type.
    bool force_mesa = false;
#endif /* SLIC3R_GUI */
    for (int i = 1; i < argc; ++ i) {
#ifdef SLIC3R_GUI
        if (wcscmp(argv[i], L"--sw-renderer") == 0)
            force_mesa = true;
        else if (wcscmp(argv[i], L"--no-sw-renderer") == 0)
            force_mesa = false;
#endif /* SLIC3R_GUI */
        argv_extended.emplace_back(argv[i]);
    }
    argv_extended.emplace_back(nullptr);

    wchar_t path_to_exe[MAX_PATH + 1] = { 0 };
    ::GetModuleFileNameW(nullptr, path_to_exe, MAX_PATH);
    wchar_t drive[_MAX_DRIVE];
    wchar_t dir[_MAX_DIR];
    wchar_t fname[_MAX_FNAME];
    wchar_t ext[_MAX_EXT];
    _wsplitpath(path_to_exe, drive, dir, fname, ext);
    _wmakepath(path_to_exe, drive, dir, nullptr, nullptr);

    wchar_t path_to_slic3r[MAX_PATH + 1] = { 0 };
    wcscpy(path_to_slic3r, path_to_exe);
    wcscat(path_to_slic3r, L"BambuStudio.dll");
//	printf("Loading Slic3r library: %S\n", path_to_slic3r);
    HINSTANCE hInstance_Slic3r = LoadLibraryExW(path_to_slic3r, nullptr, 0);
    if (hInstance_Slic3r == nullptr) {
        printf("BambuStudio.dll was not loaded, error=%d\n", GetLastError());
        return -1;
    }

    // resolve function address here
    bambustu_main = (Slic3rMainFunc)GetProcAddress(hInstance_Slic3r,
#ifdef _WIN64
        // there is just a single calling conversion, therefore no mangling of the function name.
        "bambustu_main"
#else	// stdcall calling convention declaration
        "_bambustu_main@8"
#endif
        );
    if (bambustu_main == nullptr) {
        printf("could not locate the function bambustu_main in BambuStudio.dll\n");
        return -1;
    }
    // argc minus the trailing nullptr of the argv
    return bambustu_main((int)argv_extended.size() - 1, argv_extended.data());
}
}
