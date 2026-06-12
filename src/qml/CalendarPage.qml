import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import org.kweek.app
import "DateUtils.js" as DateUtils

Kirigami.Page {
    id: root

    title: viewSwitcher.currentIndex === 1
        ? Qt.formatDate(anchorDate, "MMMM yyyy")
        : i18nc("@title:window week range", "%1 – %2",
                Qt.formatDate(DateUtils.startOfWeek(anchorDate), "d MMM"),
                Qt.formatDate(DateUtils.addDays(DateUtils.startOfWeek(anchorDate), 6), "d MMM yyyy"))

    property date anchorDate: new Date()

    padding: 0

    actions: [
        Kirigami.Action {
            text: i18nc("@action:button", "Today")
            icon.name: "go-jump-today"
            onTriggered: root.anchorDate = new Date()
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Previous")
            icon.name: "go-previous"
            onTriggered: {
                if (viewSwitcher.currentIndex === 1) {
                    root.anchorDate = DateUtils.addMonths(root.anchorDate, -1);
                } else {
                    root.anchorDate = DateUtils.addDays(root.anchorDate, -7);
                }
            }
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Next")
            icon.name: "go-next"
            onTriggered: {
                if (viewSwitcher.currentIndex === 1) {
                    root.anchorDate = DateUtils.addMonths(root.anchorDate, 1);
                } else {
                    root.anchorDate = DateUtils.addDays(root.anchorDate, 7);
                }
            }
        },
        Kirigami.Action {
            text: i18nc("@action:button", "New event")
            icon.name: "list-add"
            onTriggered: {
                const start = new Date();
                start.setMinutes(0, 0, 0);
                const end = new Date(start);
                end.setHours(end.getHours() + 1);
                editDialog.openForCreate(start, end);
            }
        }
    ]

    EventEditDialog {
        id: editDialog

        function openForEditUid(uid) {
            const data = CalendarManager.eventData(uid);
            if (Object.keys(data).length === 0) {
                return;
            }
            openForEdit(data.uid, data.summary, data.description, data.location,
                         data.start, data.end, data.allDay, data.color,
                         data.recurrence, data.reminderMinutes, data.busy);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Controls.TabBar {
            id: viewSwitcher
            Layout.fillWidth: true
            currentIndex: 1

            Controls.TabButton {
                text: i18nc("@item:inlistbox calendar view", "Agenda")
            }
            Controls.TabButton {
                text: i18nc("@item:inlistbox calendar view", "Month")
            }
            Controls.TabButton {
                text: i18nc("@item:inlistbox calendar view", "Week")
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: viewSwitcher.currentIndex

            AgendaView {
                anchorDate: root.anchorDate
                onEventActivated: uid => editDialog.openForEditUid(uid)
            }

            MonthView {
                anchorDate: root.anchorDate
                onEventActivated: uid => editDialog.openForEditUid(uid)
                onDayActivated: day => {
                    root.anchorDate = day;
                    viewSwitcher.currentIndex = 2;
                }
                onNewEventRequested: (start, end) => editDialog.openForCreate(start, end)
            }

            WeekView {
                anchorDate: root.anchorDate
                onEventActivated: uid => editDialog.openForEditUid(uid)
                onDayActivated: day => {
                    root.anchorDate = day;
                }
                onNewEventRequested: (start, end) => editDialog.openForCreate(start, end)
            }
        }
    }
}
