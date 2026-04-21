# GUI/DeviceWeb
# usage -- GUI about device web for BambuStudio
# date -- 2025.01.01
# status -- Building

list(APPEND SLIC3R_GUI_SOURCES
    GUI/DeviceWeb/DeviceHttpServer.cpp
    GUI/DeviceWeb/DeviceHttpServer.hpp
    GUI/DeviceWeb/DeviceWebPage.cpp
    GUI/DeviceWeb/DeviceWebPage.hpp
    GUI/DeviceWeb/DeviceWebBridge.cpp
    GUI/DeviceWeb/DeviceWebBridge.hpp
    GUI/DeviceWeb/DeviceWebModel.hpp
    GUI/DeviceWeb/IViewModel.hpp
    GUI/DeviceWeb/DeviceWebManager.cpp
    GUI/DeviceWeb/DeviceWebManager.hpp
    GUI/DeviceWeb/FilaManagerVM.cpp
    GUI/DeviceWeb/FilaManagerVM.hpp
)
set(SLIC3R_GUI_SOURCES ${SLIC3R_GUI_SOURCES} PARENT_SCOPE)