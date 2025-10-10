# GUI/DeviceTab
# usage -- GUI about device tab for BambuStudio
# date -- 2025.01.01
# status -- Building

list(APPEND SLIC3R_GUI_SOURCES
    GUI/DeviceTab/uiAmsHumidityPopup.h
    GUI/DeviceTab/uiAmsHumidityPopup.cpp
    GUI/DeviceTab/uiDeviceUpdateVersion.h
    GUI/DeviceTab/uiDeviceUpdateVersion.cpp
    GUI/DeviceTab/wgtDeviceNozzleRack.h
    GUI/DeviceTab/wgtDeviceNozzleRack.cpp
    GUI/DeviceTab/wgtDeviceNozzleRackUpdate.h
    GUI/DeviceTab/wgtDeviceNozzleRackUpdate.cpp
    GUI/DeviceTab/wgtDeviceNozzleSelect.h
    GUI/DeviceTab/wgtDeviceNozzleSelect.cpp
)
set(SLIC3R_GUI_SOURCES ${SLIC3R_GUI_SOURCES} PARENT_SCOPE)