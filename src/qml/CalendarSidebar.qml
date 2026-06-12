import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import org.kweek.app

ColumnLayout {
    id: root

    readonly property var colors: ["#7B61FF", "#FF7A59", "#36CFC9", "#F2C94C", "#6FCF97", "#EB5757", "#9B51E0", "#56CCF2"]

    spacing: Kirigami.Units.smallSpacing

    Controls.Label {
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.smallSpacing
        text: i18nc("@title:group", "Calendars")
        font.bold: true
    }

    Repeater {
        model: CalendarManager.calendars

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            Controls.CheckBox {
                checked: modelData.visible
                onToggled: CalendarManager.setCalendarVisible(modelData.id, checked)
            }

            Rectangle {
                width: Kirigami.Units.gridUnit * 0.8
                height: width
                radius: width / 2
                color: modelData.color

                TapHandler {
                    onTapped: {
                        colorPicker.calendarId = modelData.id;
                        colorPicker.calendarName = modelData.name;
                        colorPicker.open();
                    }
                }
            }

            Controls.TextField {
                Layout.fillWidth: true
                text: modelData.name

                onEditingFinished: {
                    if (text.length > 0 && text !== modelData.name) {
                        CalendarManager.updateCalendar(modelData.id, text, modelData.color);
                    }
                }
            }

            Controls.ToolButton {
                icon.name: "edit-delete-remove"
                visible: CalendarManager.calendars.length > 1
                onClicked: CalendarManager.removeCalendar(modelData.id)
            }
        }
    }

    Controls.Button {
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.smallSpacing
        icon.name: "list-add"
        text: i18nc("@action:button", "New calendar")
        onClicked: {
            const colorIndex = CalendarManager.calendars.length % root.colors.length;
            CalendarManager.addCalendar(i18nc("@item new calendar default name", "New calendar"), root.colors[colorIndex]);
        }
    }

    Item { Layout.fillHeight: true }

    Controls.Popup {
        id: colorPicker

        property string calendarId: ""
        property string calendarName: ""

        modal: true
        focus: true
        x: (root.width - width) / 2
        y: Kirigami.Units.gridUnit * 2

        GridLayout {
            columns: 4
            rowSpacing: Kirigami.Units.smallSpacing
            columnSpacing: Kirigami.Units.smallSpacing

            Repeater {
                model: root.colors

                Rectangle {
                    width: Kirigami.Units.gridUnit * 1.2
                    height: width
                    radius: width / 2
                    color: modelData

                    TapHandler {
                        onTapped: {
                            CalendarManager.updateCalendar(colorPicker.calendarId, colorPicker.calendarName, modelData);
                            colorPicker.close();
                        }
                    }
                }
            }
        }
    }
}
