import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import org.kweek.app
import "DateUtils.js" as DateUtils

ColumnLayout {
    id: root

    property date anchorDate
    readonly property date weekStart: DateUtils.startOfWeek(anchorDate)
    readonly property date weekEnd: DateUtils.addDays(weekStart, 7)

    readonly property real hourHeight: Kirigami.Units.gridUnit * 3
    readonly property real timeColumnWidth: Kirigami.Units.gridUnit * 3

    signal eventActivated(string uid)
    signal dayActivated(date day)
    signal newEventRequested(date start, date end)

    spacing: 0

    EventListModel {
        id: eventModel
        calendarManager: CalendarManager
        rangeStart: root.weekStart
        rangeEnd: root.weekEnd
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

    function eventsForDay(dayIndex) {
        const dayStart = DateUtils.addDays(root.weekStart, dayIndex);
        const dayEnd = DateUtils.addDays(dayStart, 1);
        const result = [];

        for (let i = 0; i < timedEvents.length; i++) {
            const e = timedEvents[i];
            if (e.start < dayEnd && e.end > dayStart) {
                const visibleStart = e.start < dayStart ? dayStart : e.start;
                const visibleEnd = e.end > dayEnd ? dayEnd : e.end;
                const startMinutes = (visibleStart.getTime() - dayStart.getTime()) / 60000;
                const endMinutes = (visibleEnd.getTime() - dayStart.getTime()) / 60000;
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

    // Header row: day names and dates.
    RowLayout {
        Layout.fillWidth: true
        spacing: 1

        Item {
            Layout.preferredWidth: root.timeColumnWidth
        }

        Repeater {
            model: 7

            Controls.Button {
                Layout.fillWidth: true
                flat: true
                readonly property date day: DateUtils.addDays(root.weekStart, index)
                readonly property bool isToday: DateUtils.isSameDay(day, new Date())

                text: Qt.locale().dayName(index === 6 ? 7 : index + 1, Locale.ShortFormat) + "\n" + day.getDate()
                highlighted: isToday
                onClicked: root.dayActivated(day)
            }
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

        Repeater {
            model: 7

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 2

                readonly property date day: DateUtils.addDays(root.weekStart, index)
                readonly property date dayEnd: DateUtils.addDays(day, 1)

                Repeater {
                    model: root.allDayEvents.filter(e => {
                        const endExclusive = new Date(e.end.getTime() - 1);
                        return DateUtils.startOfDay(e.start) <= parent.day && DateUtils.startOfDay(endExclusive) >= parent.day;
                    })

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
    }

    Kirigami.Separator {
        Layout.fillWidth: true
        visible: root.allDayEvents.length > 0
    }

    Controls.ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true

        RowLayout {
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

            // Day columns.
            Repeater {
                model: 7

                Item {
                    id: dayColumn

                    readonly property date day: DateUtils.addDays(root.weekStart, index)
                    readonly property bool isToday: DateUtils.isSameDay(day, new Date())
                    readonly property var dayEvents: root.eventsForDay(index)

                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: root.hourHeight * 24
                    Layout.alignment: Qt.AlignTop

                    Rectangle {
                        anchors.fill: parent
                        color: dayColumn.isToday ? Kirigami.Theme.highlightColor : Kirigami.Theme.backgroundColor
                        opacity: dayColumn.isToday ? 0.06 : 1
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
                            const start = new Date(dayColumn.day);
                            start.setHours(hour, 0, 0, 0);
                            const end = new Date(start);
                            end.setHours(end.getHours() + 1);
                            root.newEventRequested(start, end);
                        }
                    }

                    // Current time indicator.
                    Rectangle {
                        visible: dayColumn.isToday
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
                        model: dayColumn.dayEvents

                        Rectangle {
                            readonly property int columnCount: Math.min(dayColumn.dayEvents.length, 3)

                            x: (width + 2) * (modelData.column % columnCount)
                            y: modelData.top
                            width: parent.width / columnCount - 2
                            height: modelData.height
                            radius: 3
                            color: modelData.event.color.length > 0 ? modelData.event.color : Kirigami.Theme.highlightColor

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
                                    visible: modelData.height > Kirigami.Units.gridUnit * 1.8
                                    text: Qt.formatTime(modelData.event.start, "HH:mm") + " – " + Qt.formatTime(modelData.event.end, "HH:mm")
                                    color: "white"
                                    opacity: 0.85
                                    font.pixelSize: Kirigami.Units.gridUnit * 0.6
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.eventActivated(modelData.event.uid)
                            }
                        }
                    }
                }
            }
        }
    }
}
