import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import org.kweek.app

Kirigami.Dialog {
    id: root

    title: i18nc("@title:dialog", "Connect CalDAV account")
    standardButtons: Kirigami.Dialog.NoButtons

    readonly property var presets: [
        { label: i18nc("@item:inlistbox caldav preset", "Apple iCloud"), url: "https://caldav.icloud.com" },
        { label: i18nc("@item:inlistbox caldav preset", "Fastmail"), url: "https://caldav.fastmail.com/dav/" },
        { label: i18nc("@item:inlistbox caldav preset", "Nextcloud"), url: "" },
        { label: i18nc("@item:inlistbox caldav preset", "Custom server"), url: "" },
    ]

    property bool busy: false

    function reset() {
        presetCombo.currentIndex = 0;
        serverField.text = root.presets[0].url;
        usernameField.text = "";
        passwordField.text = "";
        statusLabel.text = "";
        busy = false;
    }

    function open2() {
        reset();
        root.open();
    }

    Connections {
        target: CalendarManager
        function onCalDavAccountAdded(accountId, calendarCount, error) {
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
        Kirigami.FormLayout {

            Controls.ComboBox {
                id: presetCombo
                Kirigami.FormData.label: i18nc("@label:listbox", "Provider:")
                model: root.presets.map(p => p.label)
                onActivated: {
                    const url = root.presets[currentIndex].url;
                    if (url.length > 0) {
                        serverField.text = url;
                    }
                }
            }

            Controls.TextField {
                id: serverField
                Kirigami.FormData.label: i18nc("@label:textbox", "Server URL:")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 18
                placeholderText: "https://example.com/remote.php/dav/"
            }

            Controls.TextField {
                id: usernameField
                Kirigami.FormData.label: i18nc("@label:textbox", "Username:")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 18
            }

            Controls.TextField {
                id: passwordField
                Kirigami.FormData.label: i18nc("@label:textbox", "App password:")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 18
                echoMode: TextInput.Password
            }
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
                text: i18nc("@action:button", "Connect")
                enabled: !root.busy && serverField.text.length > 0 && usernameField.text.length > 0 && passwordField.text.length > 0
                onClicked: {
                    root.busy = true;
                    statusLabel.text = "";
                    CalendarManager.addCalDavAccount(serverField.text, usernameField.text, passwordField.text);
                }
            }
        }
    }
}
