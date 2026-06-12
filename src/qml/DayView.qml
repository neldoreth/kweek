import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import org.kweek.app
import "DateUtils.js" as DateUtils

ColumnLayout {
    id: root

    property date anchorDate
    readonly property date dayStart: DateUtils.startOfDay(anchorDate)
    readonly property date dayEnd: DateUtils.addDays(dayStart, 1)
    readonly property bool isToday: DateUtils.isSameDay(dayStart, new Date())

    readonly property real hourHeight: Kirigami.Units.gridUnit * 3
    readonly property real timeColumnWidth: Kirigami.Units.gridUnit * 3

    signal eventActivated(string uid)
    signal newEventRequested(date start, date end)

    spacing: 0

    EventListModel {
        id: eventModel
        calendarManager: CalendarManager
        rangeStart: root.dayStart
        rangeEnd: root.dayEnd
    }

    property var allDayEvents: []
    property var timedEvents: []

    function rebuild() {
        const allDay = [];
        const timed = [];
        for (let r = 0; r < eventModel.rowCount(); r++) {
            const e = eventModel.get(r);
            if (e.allDay) {
                allDay.push(e);
            } else {
                timed.push(e);
            }
        }
        allDayEvents = allDay;
        timedEvents = timed;
    }

    Component.onCompleted: rebuild()

    Connections {
        target: eventModel
        function onModelReset() { root.rebuild() }
    }

    readonly property var dayEvents: {
        const result = [];
        for (let i = 0; i < timedEvents.length; i++) {
            const e = timedEvents[i];
            if (e.start < root.dayEnd && e.end > root.dayStart) {
                const visibleStart = e.start < root.dayStart ? root.dayStart : e.start;
                const visibleEnd = e.end > root.dayEnd ? root.dayEnd : e.end;
                const startMinutes = (visibleStart.getTime() - root.dayStart.getTime()) / 60000;
                const endMinutes = (visibleEnd.getTime() - root.dayStart.getTime()) / 60000;
                result.push({
                    event: e,
                    top: startMinutes / 60 * root.hourHeight,
                    height: Math.max((endMinutes - startMinutes) / 60 * root.hourHeight, Kirigami.Units.gridUnit),
                    column: result.length,
                });
            }
        }
        return result;
    }

    // Header: full day name and date.
    RowLayout {
        Layout.fillWidth: true
        spacing: 1

        Item {
            Layout.preferredWidth: root.timeColumnWidth
        }

        Controls.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            font.bold: true
            font.pixelSize: Kirigami.Units.gridUnit * 0.9
            color: root.isToday ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
            text: Qt.locale().toString(root.dayStart, "dddd, d MMMM yyyy")
        }
    }

    Kirigami.Separator {
        Layout.fillWidth: true
    }

    // All-day events row.
    RowLayout {
        Layout.fillWidth: true
        spacing: 1
        visible: root.allDayEvents.length > 0

        Controls.Label {
            Layout.preferredWidth: root.timeColumnWidth
            text: i18nc("@label all-day events row", "All day")
            font.pixelSize: Kirigami.Units.gridUnit * 0.6
            opacity: 0.7
            wrapMode: Text.WordWrap
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 2

            Repeater {
                model: root.allDayEvents

                Rectangle {
                    Layout.fillWidth: true
                    height: Kirigami.Units.gridUnit
                    radius: 3
                    color: modelData.color.length > 0 ? modelData.color : Kirigami.Theme.highlightColor

                    Controls.Label {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        text: modelData.summary
                        color: "white"
                        font.pixelSize: Kirigami.Units.gridUnit * 0.7
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.eventActivated(modelData.uid)
                    }
                }
            }
        }
    }

    Kirigami.Separator {
        Layout.fillWidth: true
        visible: root.allDayEvents.length > 0
    }

    Controls.ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true

        RowLayout {
            id: dayRow
            width: parent.width
            spacing: 1

            // Time labels.
            ColumnLayout {
                Layout.preferredWidth: root.timeColumnWidth
                Layout.alignment: Qt.AlignTop
                spacing: 0

                Repeater {
                    model: 24
                    Controls.Label {
                        height: root.hourHeight
                        verticalAlignment: Text.AlignTop
                        horizontalAlignment: Text.AlignRight
                        Layout.fillWidth: true
                        rightPadding: 4
                        opacity: 0.7
                        font.pixelSize: Kirigami.Units.gridUnit * 0.6
                        text: index === 0 ? "" : (index + ":00")
                    }
                }
            }

            // Single day column.
            Item {
                id: dayColumn

                Layout.fillWidth: true
                Layout.preferredHeight: root.hourHeight * 24
                Layout.alignment: Qt.AlignTop

                Rectangle {
                    anchors.fill: parent
                    color: root.isToday ? Kirigami.Theme.highlightColor : Kirigami.Theme.backgroundColor
                    opacity: root.isToday ? 0.06 : 1
                }

                // Hour grid lines.
                Repeater {
                    model: 24
                    Rectangle {
                        y: index * root.hourHeight
                        width: parent.width
                        height: 1
                        color: Kirigami.Theme.disabledTextColor
                        opacity: 0.2
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: mouse => {
                        const hour = Math.floor(mouse.y / root.hourHeight);
                        const start = new Date(root.dayStart);
                        start.setHours(hour, 0, 0, 0);
                        const end = new Date(start);
                        end.setHours(end.getHours() + 1);
                        root.newEventRequested(start, end);
                    }
                }

                // Current time indicator.
                Rectangle {
                    visible: root.isToday
                    width: parent.width
                    height: 2
                    color: Kirigami.Theme.negativeTextColor
                    y: {
                        const now = new Date();
                        return (now.getHours() * 60 + now.getMinutes()) / 60 * root.hourHeight;
                    }
                }

                // Event blocks.
                Repeater {
                    model: root.dayEvents

                    Rectangle {
                        id: eventRect

                        readonly property int columnCount: Math.min(root.dayEvents.length, 3)
                        readonly property int snapMinutes: 15

                        x: (width + 2) * (modelData.column % columnCount)
                        y: modelData.top
                        width: parent.width / columnCount - 2
                        height: modelData.height
                        radius: 3
                        color: modelData.event.color.length > 0 ? modelData.event.color : Kirigami.Theme.highlightColor
                        opacity: moveArea.dragging ? 0.7 : 1
                        z: (moveArea.dragging || resizeArea.resizing) ? 10 : 0

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 3
                            spacing: 0

                            Controls.Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: modelData.event.summary
                                color: "white"
                                font.bold: true
                                font.pixelSize: Kirigami.Units.gridUnit * 0.7
                            }

                            Controls.Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                visible: eventRect.height > Kirigami.Units.gridUnit * 1.8
                                text: Qt.formatTime(modelData.event.start, "HH:mm") + " – " + Qt.formatTime(modelData.event.end, "HH:mm")
                                color: "white"
                                opacity: 0.85
                                font.pixelSize: Kirigami.Units.gridUnit * 0.6
                            }
                        }

                        // Move (drag to retime, preserving duration).
                        MouseArea {
                            id: moveArea

                            anchors.fill: parent
                            anchors.bottomMargin: 6
                            preventStealing: true
                            cursorShape: dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor

                            property bool dragging: false
                            property real pressY: 0
                            property real origY: 0

                            onPressed: mouse => {
                                pressY = mapToItem(dayRow, mouse.x, mouse.y).y;
                                origY = eventRect.y;
                                dragging = false;
                            }

                            onPositionChanged: mouse => {
                                const currentY = mapToItem(dayRow, mouse.x, mouse.y).y;
                                const dy = currentY - pressY;
                                if (!dragging && Math.abs(dy) > 6) {
                                    dragging = true;
                                }
                                if (dragging) {
                                    eventRect.y = Math.max(0, origY + dy);
                                }
                            }

                            onReleased: mouse => {
                                if (!dragging) {
                                    root.eventActivated(modelData.event.uid);
                                    return;
                                }
                                dragging = false;

                                const currentY = mapToItem(dayRow, mouse.x, mouse.y).y;
                                const dy = currentY - pressY;
                                const minuteDelta = Math.round((dy / root.hourHeight * 60) / eventRect.snapMinutes) * eventRect.snapMinutes;

                                if (minuteDelta !== 0) {
                                    CalendarManager.rescheduleEvent(modelData.event.uid, minuteDelta * 60);
                                } else {
                                    eventRect.y = origY;
                                }
                            }
                        }

                        // Resize handle (drag to change duration).
                        MouseArea {
                            id: resizeArea

                            height: 6
                            width: parent.width
                            anchors.bottom: parent.bottom
                            preventStealing: true
                            cursorShape: Qt.SizeVerCursor

                            property bool resizing: false
                            property real pressGlobalY: 0
                            property real origHeight: 0

                            onPressed: mouse => {
                                pressGlobalY = mapToItem(dayRow, mouse.x, mouse.y).y;
                                origHeight = eventRect.height;
                                resizing = true;
                            }

                            onPositionChanged: mouse => {
                                if (!resizing) {
                                    return;
                                }
                                const currentY = mapToItem(dayRow, mouse.x, mouse.y).y;
                                const dy = currentY - pressGlobalY;
                                const minHeight = root.hourHeight * eventRect.snapMinutes / 60;
                                eventRect.height = Math.max(origHeight + dy, minHeight);
                            }

                            onReleased: mouse => {
                                resizing = false;

                                const currentY = mapToItem(dayRow, mouse.x, mouse.y).y;
                                const dy = currentY - pressGlobalY;
                                const minuteDelta = Math.round((dy / root.hourHeight * 60) / eventRect.snapMinutes) * eventRect.snapMinutes;

                                const e = modelData.event;
                                let newEnd = new Date(e.end.getTime() + minuteDelta * 60000);
                                const minEnd = new Date(e.start.getTime() + eventRect.snapMinutes * 60000);
                                if (newEnd < minEnd) {
                                    newEnd = minEnd;
                                }

                                if (newEnd.getTime() !== e.end.getTime()) {
                                    CalendarManager.updateEvent(e.uid, e.summary, e.description, e.location,
                                                                  e.start, newEnd, e.allDay, e.color,
                                                                  e.recurrence, e.reminderMinutes, e.busy);
                                } else {
                                    eventRect.height = origHeight;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
