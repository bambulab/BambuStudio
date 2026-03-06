# GUI/DeviceTab
# usage -- GUI about device tab for BambuStudio
# date -- 2025.01.01
# status -- Building

list(APPEND SLIC3R_GUI_SOURCES
    GUI/DeviceTab/uiAmsHumidityPopup.h
    GUI/DeviceTab/uiAmsHumidityPopup.cpp
    GUI/DeviceTab/uiAMSBestPositionPopup.hpp
    GUI/DeviceTab/uiAMSBestPositionPopup.cpp
    GUI/DeviceTab/uiDeviceUpdateVersion.h
    GUI/DeviceTab/uiDeviceUpdateVersion.cpp
    GUI/DeviceTab/wgtDeviceNozzleRack.h
    GUI/DeviceTab/wgtDeviceNozzleRack.cpp
    GUI/DeviceTab/wgtDeviceNozzleRackUpdate.h
    GUI/DeviceTab/wgtDeviceNozzleRackUpdate.cpp
    GUI/DeviceTab/wgtDeviceNozzleSelect.h
    GUI/DeviceTab/wgtDeviceNozzleSelect.cpp
    GUI/DeviceTab/wgtMsgBox.h
    GUI/DeviceTab/wgtMsgBox.cpp
    GUI/DeviceTab/wgtMsgPanel.h
    GUI/DeviceTab/wgtMsgPanel.cpp
    GUI/DeviceTab/AmsControl/AmsControlWebEnable.hpp
    GUI/DeviceTab/AmsControl/wgtAmsControlWebPanel.h
    GUI/DeviceTab/AmsControl/wgtAmsControlWebPanel.cpp
    GUI/DeviceTab/FilamentHotend/wgtDeviceFilamentHotendTabHeader.h
    GUI/DeviceTab/FilamentHotend/wgtDeviceFilamentHotendTabHeader.cpp
    GUI/DeviceTab/FilamentHotend/wgtDeviceFilamentHotendTab.h
    GUI/DeviceTab/FilamentHotend/wgtDeviceFilamentHotendTab.cpp
    GUI/DeviceTab/FilamentHotend/wgtDeviceFilamentHotendTabLoadTips.h
    GUI/DeviceTab/FilamentHotend/wgtDeviceFilamentHotendTabLoadTips.cpp
    GUI/DeviceTab/FilamentHotend/wgtDeviceFilamentPanel.h
    GUI/DeviceTab/FilamentHotend/wgtDeviceFilamentPanel.cpp
    GUI/DeviceTab/FilamentHotend/wgtDeviceAmsPreview.h
    GUI/DeviceTab/FilamentHotend/wgtDeviceAmsPreview.cpp
    GUI/DeviceTab/FilamentHotend/wgtDeviceAmsDryIcon.h
    GUI/DeviceTab/FilamentHotend/wgtDeviceAmsDryIcon.cpp
    GUI/DeviceTab/FilamentHotend/wgtDeviceAmsFilaments.h
    GUI/DeviceTab/FilamentHotend/wgtDeviceAmsFilaments.cpp
    GUI/DeviceTab/FilamentHotend/wgtDeviceFilamentHotendItem.h
    GUI/DeviceTab/FilamentHotend/wgtDeviceFilamentHotendItem.cpp
    GUI/DeviceTab/FilamentHotend/wgtDeviceHotentPanel.h
    GUI/DeviceTab/FilamentHotend/wgtDeviceHotentPanel.cpp
)
set(SLIC3R_GUI_SOURCES ${SLIC3R_GUI_SOURCES} PARENT_SCOPE)