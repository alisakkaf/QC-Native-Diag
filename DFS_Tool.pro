QT       += core gui serialport widgets

TARGET = DFS_Tool
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    src/main.cpp \
    src/MainWindow.cpp \
    src/DeviceManager.cpp \
    src/DiagProtocol.cpp \
    src/ProgressDialog.cpp \
    src/NVManager.cpp

HEADERS += \
    include/Version.h \
    include/MainWindow.h \
    include/DeviceManager.h \
    include/DiagProtocol.h \
    include/ProgressDialog.h \
    include/NVManager.h \
    include/NVDatabase.h

INCLUDEPATH += $$PWD

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resources/resources.qrc

RC_ICONS = resources/app_icon.ico

