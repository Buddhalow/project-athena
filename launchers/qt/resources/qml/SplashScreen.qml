import QtQuick 2.3
import QtQuick.Controls 2.1

Item {
    id: root
    anchors.fill: parent


    Image {
        anchors.centerIn: parent
        width: parent.width
        height: parent.height
        mirror: false
        source: PathUtils.resourcePath("images/hifi_window@2x.png");
        transformOrigin: Item.Center
        rotation: 0
    }

    Image {
        anchors.centerIn: parent
        width: 225
        height: 205
        source: PathUtils.resourcePath("images/hifi_logo_large@2x.png");
    }

    Component.onCompleted: {
        root.parent.setStateInfoState("left");
        root.parent.setBuildInfoState("right");
    }
}
