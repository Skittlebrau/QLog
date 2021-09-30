#-------------------------------------------------
#
# Project created by QtCreator 2019-06-10T09:13:09
#
#-------------------------------------------------

QT       += core gui sql network xml charts

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = qlog
TEMPLATE = app
VERSION = 0.1.0

DEFINES += VERSION=\\\"$$VERSION\\\"

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS QT_MESSAGELOGCONTEXT

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

CONFIG += c++11

SOURCES += \
        core/AppGuard.cpp \
        core/ClubLog.cpp \
        core/Conditions.cpp \
        core/CredentialStore.cpp \
        core/Cty.cpp \
        core/Fldigi.cpp \
        core/Gridsquare.cpp \
        core/HamQTH.cpp \
        core/Lotw.cpp \
        core/Migration.cpp \
        core/Rig.cpp \
        core/Rotator.cpp \
        core/Sat.cpp \
        core/Wsjtx.cpp \
        core/debug.cpp \
        core/main.cpp \
        data/Data.cpp \
        data/Dxcc.cpp \
        data/StationProfile.cpp \
        logformat/AdiFormat.cpp \
        logformat/AdxFormat.cpp \
        logformat/JsonFormat.cpp \
        logformat/LogFormat.cpp \
        models/DxccTableModel.cpp \
        models/LogbookModel.cpp \
        models/RigTypeModel.cpp \
        models/RotTypeModel.cpp \
        models/SqlListModel.cpp \
        ui/BandmapWidget.cpp \
        ui/ClockWidget.cpp \
        ui/ColumnSettingDialog.cpp \
        ui/DxFilterDialog.cpp \
        ui/DxWidget.cpp \
        ui/DxccTableWidget.cpp \
        ui/ExportDialog.cpp \
        ui/ImportDialog.cpp \
        ui/LogbookWidget.cpp \
        ui/LotwDialog.cpp \
        ui/LotwShowUploadDialog.cpp \
        ui/MainWindow.cpp \
        ui/MapWidget.cpp \
        ui/NewContactWidget.cpp \
        ui/QSLImportStatDialog.cpp \
        ui/QSOFilterDetail.cpp \
        ui/QSOFilterDialog.cpp \
        ui/RigWidget.cpp \
        ui/RotatorWidget.cpp \
        ui/SettingsDialog.cpp \
        ui/StatisticsWidget.cpp \
        ui/WsjtxWidget.cpp

HEADERS += \
        core/AppGuard.h \
        core/ClubLog.h \
        core/Conditions.h \
        core/CredentialStore.h \
        core/Cty.h \
        core/Fldigi.h \
        core/Gridsquare.h \
        core/HamQTH.h \
        core/Lotw.h \
        core/Migration.h \
        core/Rig.h \
        core/Rotator.h \
        core/Sat.h \
        core/StyleItemDelegate.h \
        core/Wsjtx.h \
        core/debug.h \
        data/Band.h \
        data/Data.h \
        data/DxSpot.h \
        data/Dxcc.h \
        data/StationProfile.h \
        logformat/AdiFormat.h \
        logformat/AdxFormat.h \
        logformat/JsonFormat.h \
        logformat/LogFormat.h \
        models/DxccTableModel.h \
        models/LogbookModel.h \
        models/RigTypeModel.h \
        models/RotTypeModel.h \
        models/SqlListModel.h \
        ui/BandmapWidget.h \
        ui/ClockWidget.h \
        ui/ColumnSettingDialog.h \
        ui/DxFilterDialog.h \
        ui/DxWidget.h \
        ui/DxccTableWidget.h \
        ui/ExportDialog.h \
        ui/ImportDialog.h \
        ui/LogbookWidget.h \
        ui/LotwDialog.h \
        ui/LotwShowUploadDialog.h \
        ui/MainWindow.h \
        ui/MapWidget.h \
        ui/NewContactWidget.h \
        ui/QSLImportStatDialog.h \
        ui/QSOFilterDetail.h \
        ui/QSOFilterDialog.h \
        ui/RigWidget.h \
        ui/RotatorWidget.h \
        ui/SettingsDialog.h \
        ui/StatisticsWidget.h \
        ui/WsjtxWidget.h

FORMS += \
        ui/BandmapWidget.ui \
        ui/ClockWidget.ui \
        ui/ColumnSettingDialog.ui \
        ui/DxFilterDialog.ui \
        ui/DxWidget.ui \
        ui/ExportDialog.ui \
        ui/ImportDialog.ui \
        ui/LogbookWidget.ui \
        ui/LotwDialog.ui \
        ui/LotwShowUploadDialog.ui \
        ui/MainWindow.ui \
        ui/NewContactWidget.ui \
        ui/QSLImportStatDialog.ui \
        ui/QSOFilterDetail.ui \
        ui/QSOFilterDialog.ui \
        ui/RigWidget.ui \
        ui/RotatorWidget.ui \
        ui/SettingsDialog.ui \
        ui/StatisticsWidget.ui \
        ui/WsjtxWidget.ui

RESOURCES += \
    i18n/i18n.qrc \
    res/flags/flags.qrc \
    res/icons/icons.qrc \
    res/res.qrc

OTHER_FILES += \
    res/stylesheet.css \
    res/qlog.rc \
    res/qlog.desktop

TRANSLATIONS = i18n/qlog_de.ts

RC_ICONS = res/qlog.ico
ICON = res/qlog.icns

unix:!macx {
  isEmpty(PREFIX) {
    PREFIX = /usr/local
  }

  target.path = $$PREFIX/bin

  desktop.path = $$PREFIX/share/applications/
  desktop.files += res/$${TARGET}.desktop

  icon.path = $$PREFIX/share/icons/hicolor/256x256/apps
  icon.files += res/$${TARGET}.png

  INSTALLS += target desktop icon

  INCLUDEPATH += /usr/local/include
  LIBS += -L/usr/local/lib -lhamlib -lqt5keychain
}

macx: {
    INCLUDEPATH += /usr/local/include
    LIBS += -L/usr/local/lib -lhamlib -lqt5keychain
    DISTFILES +=
}

win32: {
    TARGET = qlog
    QMAKE_TARGET_COMPANY = OK1MLG
    QMAKE_TARGET_DESCRIPTION = Hamradio logging
    LIBS += -L"$$PWD/../hamlib/lib/gcc" -lhamlib
    LIBS += -L"$$PWD../hamlib/bin"
    LIBS += -L"$$PWD/../qtkeychain" -llibqt5keychaind
    INCLUDEPATH += "$$PWD/../hamlib/include/"
    INCLUDEPATH += "$$PWD/../qtkeychain/"
}

DISTFILES += \
    Changelog \
    res/data/sat_modes
