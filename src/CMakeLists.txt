qt_add_executable(kweek
    main.cpp
    core/calendarmanager.cpp
    core/eventlistmodel.cpp
    core/credentialstore.cpp
    core/caldavclient.cpp
)

qt_add_qml_module(kweek
    URI org.kweek.app
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/CalendarPage.qml
        qml/EventEditDialog.qml
        qml/CalendarSidebar.qml
        qml/AddCalDavAccountDialog.qml
        qml/AgendaView.qml
        qml/MonthView.qml
        qml/WeekView.qml
        qml/DayView.qml
        qml/DateUtils.js
)

target_include_directories(kweek PRIVATE core)

target_link_libraries(kweek PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    KF6::CoreAddons
    KF6::I18n
    KF6::CalendarCore
    KF6::Wallet
)

install(TARGETS kweek ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
