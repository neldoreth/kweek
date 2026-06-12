import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

import org.kweek.app

Kirigami.ApplicationWindow {
    id: root

    title: i18nc("@title:window", "Kweek")

    width: 1100
    height: 750

    pageStack.initialPage: calendarPageComponent

    Component {
        id: calendarPageComponent
        CalendarPage {}
    }
}
