# GUI/fila_manager
# Filament Manager — inventory, WebView UI, AMS sync

list(APPEND SLIC3R_GUI_SOURCES
    GUI/fila_manager/wgtFilaManagerStore.h
    GUI/fila_manager/wgtFilaManagerStore.cpp
    GUI/fila_manager/wgtFilaManagerPanel.h
    GUI/fila_manager/wgtFilaManagerPanel.cpp
    GUI/fila_manager/wgtFilaManagerSync.h
    GUI/fila_manager/wgtFilaManagerSync.cpp
    GUI/fila_manager/wgtFilaManagerCloudClient.h
    GUI/fila_manager/wgtFilaManagerCloudClient.cpp
    GUI/fila_manager/wgtFilaManagerCloudSync.h
    GUI/fila_manager/wgtFilaManagerCloudSync.cpp
    GUI/fila_manager/wgtFilaManagerCloudDispatcher.h
    GUI/fila_manager/wgtFilaManagerCloudDispatcher.cpp
)
set(SLIC3R_GUI_SOURCES ${SLIC3R_GUI_SOURCES} PARENT_SCOPE)
