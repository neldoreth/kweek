import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import org.kweek.app

Kirigami.Page {
    id: root

    title: i18nc("@title", "Agenda")

    readonly property date weekStart: {
        const d = new Date();
        d.setHours(0, 0, 0, 0);
        d.setDate(d.getDate() - d.getDay());
        return d;
    }

    readonly property date weekEnd: {
        const d = new Date(weekStart);
        d.setDate(d.getDate() + 7);
        return d;
    }

    actions: [
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

    EventListModel {
        id: eventModel
        calendarManager: CalendarManager
        rangeStart: root.weekStart
        rangeEnd: root.weekEnd
    }

    EventEditDialog {
        id: editDialog
    }

    ListView {
        anchors.fill: parent
        model: eventModel
        spacing: Kirigami.Units.smallSpacing

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: eventModel.count === 0
            icon.name: "view-calendar"
            text: i18n("No events this week")
            explanation: i18n("Use the \"New event\" button to create one")
        }

        delegate: Kirigami.SwipeListItem {
            width: ListView.view.width

            actions: [
                Kirigami.Action {
                    text: i18nc("@action:button", "+1 day")
                    icon.name: "go-next"
                    onTriggered: CalendarManager.rescheduleEvent(model.uid, 24 * 60 * 60)
                },
                Kirigami.Action {
                    text: i18nc("@action:button", "+1 hour")
                    icon.name: "chronometer"
                    onTriggered: CalendarManager.rescheduleEvent(model.uid, 60 * 60)
                },
                Kirigami.Action {
                    text: i18nc("@action:button", "Delete")
                    icon.name: "edit-delete"
                    onTriggered: CalendarManager.removeEvent(model.uid)
                }
            ]

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Rectangle {
                    width: Kirigami.Units.smallSpacing
                    Layout.fillHeight: true
                    radius: width / 2
                    color: model.color.length > 0 ? model.color : Kirigami.Theme.highlightColor
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Controls.Label {
                        text: model.summary
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Controls.Label {
                        text: model.location.length > 0 ? model.location : ""
                        visible: model.location.length > 0
                        opacity: 0.7
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                Controls.Label {
                    text: model.allDay
                        ? i18nc("@info event date range", "All day · %1", Qt.formatDate(model.start, "ddd dd MMM"))
                        : i18nc("@info event time range", "%1 – %2",
                                Qt.formatDateTime(model.start, "ddd dd MMM HH:mm"),
                                Qt.formatDateTime(model.end, "HH:mm"))
                    opacity: 0.8
                }

                Kirigami.Icon {
                    source: "task-recurring"
                    visible: model.recurring
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }
            }

            onClicked: editDialog.openForEdit(model.uid, model.summary, model.description, model.location,
                                               model.start, model.end, model.allDay, model.color,
                                               model.recurrence, model.reminderMinutes, model.busy)
        }
    }
}
