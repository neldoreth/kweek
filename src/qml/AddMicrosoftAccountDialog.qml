import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import org.kweek.app

Kirigami.Dialog {
    id: root

    title: i18nc("@title:dialog", "Connect Microsoft account")
    standardButtons: Kirigami.Dialog.NoButtons

    property bool busy: false

    function reset() {
        statusLabel.text = "";
        busy = false;
    }

    function open2() {
        reset();
        root.open();
    }

    Connections {
        target: CalendarManager
        function onMicrosoftAccountAdded(accountId, calendarCount, error) {
            if (!root.opened) {
                return;
            }
            root.busy = false;
            if (error.length > 0) {
                statusLabel.text = error;
                statusLabel.color = Kirigami.Theme.negativeTextColor;
            } else {
                statusLabel.text = i18ncp("@info", "Connected, %1 calendar added.", "Connected, %1 calendars added.", calendarCount);
                statusLabel.color = Kirigami.Theme.positiveTextColor;
                root.close();
            }
        }
    }

    ColumnLayout {
        Controls.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 18
            text: i18nc("@info", "Your browser will open so you can sign in with Microsoft and grant Kweek access to your calendars.")
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true

            Controls.Label {
                id: statusLabel
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Controls.BusyIndicator {
                running: root.busy
                visible: root.busy
                implicitWidth: Kirigami.Units.iconSizes.medium
                implicitHeight: Kirigami.Units.iconSizes.medium
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignRight

            Controls.Button {
                text: i18nc("@action:button", "Cancel")
                onClicked: root.close()
            }

            Controls.Button {
                text: i18nc("@action:button", "Sign in with Microsoft")
                enabled: !root.busy
                onClicked: {
                    root.busy = true;
                    statusLabel.text = "";
                    CalendarManager.addMicrosoftAccount();
                }
            }
        }
    }
}
