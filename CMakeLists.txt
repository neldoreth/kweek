cmake_minimum_required(VERSION 3.16)

project(kweek VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(QT_MIN_VERSION "6.6.0")
set(KF_MIN_VERSION "6.0.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQmlModule)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS CoreAddons I18n Kirigami CalendarCore)

add_subdirectory(src)

install(FILES icons/io.github.neldoreth.Kweek.svg
    DESTINATION ${KDE_INSTALL_FULL_ICONDIR}/hicolor/scalable/apps)
install(FILES data/io.github.neldoreth.Kweek.desktop
    DESTINATION ${KDE_INSTALL_FULL_APPDIR})
install(FILES data/io.github.neldoreth.Kweek.metainfo.xml
    DESTINATION ${KDE_INSTALL_FULL_METAINFODIR})
