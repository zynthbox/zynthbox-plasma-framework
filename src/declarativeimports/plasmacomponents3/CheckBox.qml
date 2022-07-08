/*
    SPDX-FileCopyrightText: 2016 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

import QtQuick 2.6
import QtQuick.Layouts 1.3
import QtQuick.Templates @QQC2_VERSION@ as T
import QtQuick.Controls @QQC2_VERSION@
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PC3
import org.kde.kirigami 2.20 as Kirigami
import "private"

T.CheckBox {
    id: control
    property real __indicatorMargin: control.indicator && control.indicator.visible && control.indicator.width > 0 ?
        indicator.width + control.spacing : 0

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding,
                            implicitIndicatorWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    baselineOffset: contentItem.y + contentItem.baselineOffset

    spacing: PlasmaCore.Units.smallSpacing

    hoverEnabled: true

    // Keeping old default smallMedium size for compatibility
    // with UIs that currently expect that as the default size
    icon.width: PlasmaCore.Units.iconSizes.smallMedium
    icon.height: PlasmaCore.Units.iconSizes.smallMedium

    PC3.ToolTip.text: control.text.length > 0 ? control.text : ""
    PC3.ToolTip.visible: control.hovered
                         && control.text.length > 0
                         && control.display === PC3.AbstractButton.IconOnly
    PC3.ToolTip.delay: Kirigami.Units.toolTipDelay
    Accessible.name: control.text.length > 0 ? control.text : ""

    indicator: CheckIndicator {
        x: !control.mirrored ? control.leftPadding : control.width - width - control.rightPadding
        y: control.topPadding + Math.round((control.availableHeight - height) / 2)
        control: control
    }

    contentItem: IconLabel {
        leftPadding: control.mirrored ? 0 : control.__indicatorMargin
        rightPadding: !control.mirrored ? 0 : control.__indicatorMargin
        palette: control.palette
        font: control.font
        alignment: Qt.AlignLeft
        display: control.display
        spacing: control.spacing
        iconItem.implicitWidth: control.icon.width
        iconItem.implicitHeight: control.icon.height
        iconItem.source: control.icon.name || control.icon.source
        label.text: control.text
        Rectangle { // As long as we don't enable antialiasing, not rounding should be fine
            parent: control.contentItem.label
            width: Math.min(parent.width, parent.contentWidth)
            height: PlasmaCore.Units.devicePixelRatio
            anchors.left: parent.left
            anchors.top: parent.bottom
            color: PlasmaCore.ColorScope.highlightColor
            visible: control.visualFocus
        }
    }
}
