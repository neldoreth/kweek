import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import org.kweek.app

Kirigami.Dialog {
    id: root

    property string uid: ""

    title: uid.length > 0 ? i18nc("@title:dialog", "Edit event") : i18nc("@title:dialog", "New event")
    standardButtons: Kirigami.Dialog.NoButtons

    readonly property var colors: ["#7B61FF", "#FF7A59", "#36CFC9", "#F2C94C", "#6FCF97", "#EB5757"]

    property string originalCalendarId: ""

    function reset() {
        uid = "";
        originalCalendarId = "";
        summaryField.text = "";
        descriptionField.text = "";
        locationField.text = "";
        startField.text = "";
        endField.text = "";
        allDayCheck.checked = false;
        busyCheck.checked = true;
        recurrenceCombo.currentIndex = 0;
        reminderCombo.currentIndex = 0;
        useCalendarColorCheck.checked = true;
        colorRow.selectedColor = colors[0];
        calendarCombo.currentIndex = 0;
    }

    function openForCreate(start, end) {
        reset();
        startField.text = Qt.formatDateTime(start, "yyyy-MM-dd HH:mm");
        endField.text = Qt.formatDateTime(end, "yyyy-MM-dd HH:mm");
        root.open();
    }

    function openForEdit(eventUid, calendarId, summary, description, location, start, end, allDay, color, recurrence, reminderMinutes, busy) {
        reset();
        uid = eventUid;
        originalCalendarId = calendarId;
        summaryField.text = summary;
        descriptionField.text = description;
        locationField.text = location;
        startField.text = Qt.formatDateTime(start, "yyyy-MM-dd HH:mm");
        endField.text = Qt.formatDateTime(end, "yyyy-MM-dd HH:mm");
        allDayCheck.checked = allDay;
        busyCheck.checked = busy;

        const recurrenceIndex = ["none", "daily", "weekly", "monthly", "yearly"].indexOf(recurrence);
        recurrenceCombo.currentIndex = recurrenceIndex >= 0 ? recurrenceIndex : 0;

        const reminderValues = [-1, 5, 15, 30, 60, 1440];
        const reminderIndex = reminderValues.indexOf(reminderMinutes);
        reminderCombo.currentIndex = reminderIndex >= 0 ? reminderIndex : 0;

        if (color.length > 0) {
            useCalendarColorCheck.checked = false;
            colorRow.selectedColor = color;
        }

        const calendars = CalendarManager.calendars;
        for (let i = 0; i < calendars.length; i++) {
            if (calendars[i].id === calendarId) {
                calendarCombo.currentIndex = i;
                break;
            }
        }

        root.open();
    }

    function save() {
        const start = Date.fromLocaleString(Qt.locale(), startField.text, "yyyy-MM-dd HH:mm");
        const end = Date.fromLocaleString(Qt.locale(), endField.text, "yyyy-MM-dd HH:mm");
        const recurrence = ["none", "daily", "weekly", "monthly", "yearly"][recurrenceCombo.currentIndex];
        const reminderMinutes = [-1, 5, 15, 30, 60, 1440][reminderCombo.currentIndex];
        const color = useCalendarColorCheck.checked ? "" : colorRow.selectedColor;
        const calendarId = calendarCombo.currentValue;

        if (uid.length > 0) {
            if (calendarId !== originalCalendarId) {
                CalendarManager.moveEventToCalendar(uid, calendarId);
            }
            CalendarManager.updateEvent(uid, summaryField.text, descriptionField.text, locationField.text,
                                         start, end, allDayCheck.checked, color,
                                         recurrence, reminderMinutes, busyCheck.checked);
        } else {
            CalendarManager.addEvent(calendarId, summaryField.text, descriptionField.text, locationField.text,
                                      start, end, allDayCheck.checked, color,
                                      recurrence, reminderMinutes, busyCheck.checked);
        }

        root.close();
    }

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true

            Controls.ComboBox {
                id: calendarCombo
                Kirigami.FormData.label: i18nc("@label:listbox", "Calendar:")
                model: CalendarManager.calendars
                textRole: "name"
                valueRole: "id"
            }

            Controls.TextField {
                id: summaryField
                Kirigami.FormData.label: i18nc("@label:textbox", "Title:")
            }

            Controls.TextField {
                id: locationField
                Kirigami.FormData.label: i18nc("@label:textbox", "Location:")
            }

            Controls.TextArea {
                id: descriptionField
                Kirigami.FormData.label: i18nc("@label:textbox", "Notes:")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                Layout.preferredHeight: Kirigami.Units.gridUnit * 4
                wrapMode: TextEdit.Wrap
            }

            Controls.TextField {
                id: startField
                Kirigami.FormData.label: i18nc("@label:textbox", "Start (yyyy-MM-dd HH:mm):")
                placeholderText: "2026-06-12 09:00"
            }

            Controls.TextField {
                id: endField
                Kirigami.FormData.label: i18nc("@label:textbox", "End (yyyy-MM-dd HH:mm):")
                placeholderText: "2026-06-12 10:00"
            }

            Controls.CheckBox {
                id: allDayCheck
                Kirigami.FormData.label: i18nc("@label:checkbox", "All day:")
                text: i18nc("@option:check", "All-day event")
            }

            Controls.CheckBox {
                id: busyCheck
                Kirigami.FormData.label: i18nc("@label:checkbox", "Availability:")
                text: i18nc("@option:check", "Busy")
                checked: true
            }

            Controls.ComboBox {
                id: recurrenceCombo
                Kirigami.FormData.label: i18nc("@label:listbox", "Repeat:")
                model: [
                    i18nc("@item:inlistbox recurrence", "Does not repeat"),
                    i18nc("@item:inlistbox recurrence", "Daily"),
                    i18nc("@item:inlistbox recurrence", "Weekly"),
                    i18nc("@item:inlistbox recurrence", "Monthly"),
                    i18nc("@item:inlistbox recurrence", "Yearly"),
                ]
            }

            Controls.ComboBox {
                id: reminderCombo
                Kirigami.FormData.label: i18nc("@label:listbox", "Reminder:")
                model: [
                    i18nc("@item:inlistbox reminder", "None"),
                    i18nc("@item:inlistbox reminder", "5 minutes before"),
                    i18nc("@item:inlistbox reminder", "15 minutes before"),
                    i18nc("@item:inlistbox reminder", "30 minutes before"),
                    i18nc("@item:inlistbox reminder", "1 hour before"),
                    i18nc("@item:inlistbox reminder", "1 day before"),
                ]
            }

            Controls.CheckBox {
                id: useCalendarColorCheck
                Kirigami.FormData.label: i18nc("@label", "Color:")
                text: i18nc("@option:check", "Use calendar color")
                checked: true
            }

            RowLayout {
                id: colorRow
                visible: !useCalendarColorCheck.checked

                property string selectedColor: root.colors[0]

                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: root.colors

                    Rectangle {
                        width: Kirigami.Units.gridUnit
                        height: Kirigami.Units.gridUnit
                        radius: width / 2
                        color: modelData
                        border.width: colorRow.selectedColor === modelData ? 2 : 0
                        border.color: Kirigami.Theme.textColor

                        TapHandler {
                            onTapped: colorRow.selectedColor = modelData
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            Layout.topMargin: Kirigami.Units.largeSpacing

            Controls.Button {
                text: i18nc("@action:button", "Cancel")
                onClicked: root.close()
            }

            Controls.Button {
                text: i18nc("@action:button", "Save")
                icon.name: "dialog-ok"
                enabled: summaryField.text.length > 0 && startField.text.length > 0 && endField.text.length > 0
                onClicked: root.save()
            }
        }
    }
}
