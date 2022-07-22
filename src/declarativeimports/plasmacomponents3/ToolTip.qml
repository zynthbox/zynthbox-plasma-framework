/*
    SPDX-FileCopyrightText: 2016 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2016 The Qt Company Ltd.

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

import QtQuick 2.6
import QtQuick.Layouts 1.15
import QtQuick.Templates @QQC2_VERSION@ as T
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.kirigami 2.5 as Kirigami

T.ToolTip {
    id: control

    x: parent ? Math.round((parent.width - implicitWidth) / 2) : 0
    y: -implicitHeight - 3

    visible: parent instanceof T.AbstractButton && (Kirigami.Settings.tabletMode ? parent.pressed : parent.hovered)
    delay: Kirigami.Settings.tabletMode ? Qt.styleHints.mousePressAndHoldInterval : Kirigami.Units.toolTipDelay
    // Timeout based on text length, from QTipLabel::restartExpireTimer
    timeout: 10000 + 40 * Math.max(0, text.length - 100)

    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, contentHeight + topPadding + bottomPadding)
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, contentWidth + leftPadding + rightPadding)

    margins: PlasmaCore.Units.smallSpacing
    leftPadding: backgroundItem.margins.left
    topPadding: backgroundItem.margins.top
    rightPadding: backgroundItem.margins.right
    bottomPadding: backgroundItem.margins.bottom

    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    contentItem: RowLayout {
        Label {
            // Strip out ampersands right before non-whitespace characters, i.e.
            // those used to determine the alt key shortcut
            text: control.text.replace(/&(?=\S)/g, "")
            wrapMode: Text.WordWrap
            font: control.font

            PlasmaCore.ColorScope.colorGroup: PlasmaCore.Theme.ToolTipColorGroup
            PlasmaCore.ColorScope.inherit: false
            Layout.fillWidth: true
            // This value is basically arbitrary. It just looks nice.
            Layout.maximumWidth: PlasmaCore.Units.gridUnit * 14
        }
    }

    background: Item {
        implicitHeight: PlasmaCore.Units.gridUnit + backgroundItem.margins.top + backgroundItem.margins.bottom
        implicitWidth: PlasmaCore.Units.gridUnit + backgroundItem.margins.left + backgroundItem.margins.right

        PlasmaCore.FrameSvgItem {
            anchors.fill: parent
            anchors.leftMargin: -margins.left
            anchors.rightMargin: -margins.right
            anchors.topMargin: -margins.top
            anchors.bottomMargin: -margins.bottom
            imagePath: "solid/widgets/tooltip"
            prefix: "shadow"
            colorGroup: PlasmaCore.Theme.ToolTipColorGroup
            PlasmaCore.ColorScope.inherit: false
        }

        PlasmaCore.FrameSvgItem {
            id: backgroundItem
            anchors.fill: parent
            // Because the transparent one doesn't match the appearance of all
            // other ones
            imagePath: "solid/widgets/tooltip"
            colorGroup: PlasmaCore.Theme.ToolTipColorGroup
            PlasmaCore.ColorScope.inherit: false
        }
    }
}
