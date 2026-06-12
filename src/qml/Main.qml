import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    title: i18nc("@title:window", "Kweek")

    width: 1100
    height: 750

    pageStack.initialPage: homePage

    Component {
        id: homePage

        Kirigami.Page {
            title: i18nc("@title", "Calendar")

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                icon.name: "view-calendar"
                text: i18n("Calendar view coming soon")
            }
        }
    }
}
