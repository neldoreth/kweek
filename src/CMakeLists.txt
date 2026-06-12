qt_add_executable(kweek
    main.cpp
)

qt_add_qml_module(kweek
    URI org.kweek.app
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

target_link_libraries(kweek PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    KF6::CoreAddons
    KF6::I18n
)

install(TARGETS kweek ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
