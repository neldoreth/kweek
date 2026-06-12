import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import org.kweek.app
import "DateUtils.js" as DateUtils

ColumnLayout {
    id: root

    property date anchorDate
    readonly property date gridStart: DateUtils.monthGridStart(anchorDate)
    readonly property date rangeEnd: DateUtils.addDays(gridStart, 42)

    signal eventActivated(string uid)
    signal dayActivated(date day)
    signal newEventRequested(date start, date end)

    spacing: 0

    EventListModel {
        id: eventModel
        calendarManager: CalendarManager
        rangeStart: root.gridStart
        rangeEnd: root.rangeEnd
    }

    property var cells: []

    function rebuild() {
        const days = [];
        for (let i = 0; i < 42; i++) {
            days.push({date: DateUtils.addDays(gridStart, i), events: []});
        }

        for (let r = 0; r < eventModel.rowCount(); r++) {
            const e = eventModel.get(r);
            const endExclusive = e.allDay ? new Date(e.end.getTime() - 1) : e.end;
            let d = DateUtils.startOfDay(e.start);
            const endDay = DateUtils.startOfDay(endExclusive);

            while (d <= endDay) {
                const idx = Math.round((d - gridStart) / 86400000);
                if (idx >= 0 && idx < 42) {
                    days[idx].events.push(e);
                }
                d = DateUtils.addDays(d, 1);
            }
        }

        cells = days;
    }

    Component.onCompleted: rebuild()
    onGridStartChanged: rebuild()

    Connections {
        target: eventModel
        function onModelReset() { root.rebuild() }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 1

        Repeater {
            model: 7
            Controls.Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                font.bold: true
                opacity: 0.7
                text: Qt.locale().dayName((index + 1) % 7 === 0 ? 7 : (index + 1), Locale.ShortFormat)
            }
        }
    }

    Kirigami.Separator {
        Layout.fillWidth: true
    }

    GridLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        columns: 7
        rows: 6
        rowSpacing: 1
        columnSpacing: 1

        Repeater {
            model: 42

            Rectangle {
                id: cell

                readonly property var cellData: root.cells[index]
                readonly property bool isToday: cellData ? DateUtils.isSameDay(cellData.date, new Date()) : false
                readonly property bool inCurrentMonth: cellData ? cellData.date.getMonth() === root.anchorDate.getMonth() : false

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: 1
                color: Kirigami.Theme.backgroundColor
                opacity: inCurrentMonth ? 1 : 0.5

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        const start = new Date(cellData.date);
                        start.setHours(9, 0, 0, 0);
                        const end = new Date(start);
                        end.setHours(end.getHours() + 1);
                        root.newEventRequested(start, end);
                    }
                    onDoubleClicked: root.dayActivated(cellData.date)
                }

                DropArea {
                    anchors.fill: parent

                    onDropped: drop => {
                        const dayDelta = Math.round((cellData.date - drop.source.originDate) / 86400000);
                        if (dayDelta !== 0) {
                            CalendarManager.rescheduleEvent(drop.source.eventUid, dayDelta * 86400);
                        }
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: 2

                    Controls.Label {
                        text: cellData ? cellData.date.getDate() : ""
                        font.bold: cell.isToday
                        color: cell.isToday ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
                    }

                    Repeater {
                        model: cellData ? cellData.events.slice(0, 3) : []

                        Rectangle {
                            id: chip

                            Layout.fillWidth: true
                            height: Kirigami.Units.gridUnit
                            radius: 3
                            color: modelData.color.length > 0 ? modelData.color : Kirigami.Theme.highlightColor

                            readonly property string eventUid: modelData.uid
                            readonly property date originDate: cellData.date

                            Drag.active: chipMouseArea.drag.active
                            Drag.dragType: Drag.Automatic
                            Drag.supportedActions: Qt.MoveAction

                            Controls.Label {
                                anchors.fill: parent
                                anchors.leftMargin: 4
                                anchors.rightMargin: 4
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                text: modelData.summary
                                color: "white"
                                font.pixelSize: Kirigami.Units.gridUnit * 0.7
                            }

                            MouseArea {
                                id: chipMouseArea
                                anchors.fill: parent
                                drag.target: chip
                                onClicked: root.eventActivated(modelData.uid)
                                onReleased: if (chip.Drag.active) chip.Drag.drop()
                            }
                        }
                    }

                    Controls.Label {
                        visible: cellData && cellData.events.length > 3
                        text: cellData ? i18nc("@info more events indicator", "+%1 more", cellData.events.length - 3) : ""
                        opacity: 0.7
                        font.pixelSize: Kirigami.Units.gridUnit * 0.65
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
