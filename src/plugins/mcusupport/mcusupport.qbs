import qbs 1.0

QtcPlugin {
    name: "McuSupport"

    Depends { name: "Qt.core" }
    Depends { name: "Qt.widgets" }
    Depends { name: "Utils" }

    Depends { name: "Core" }
    Depends { name: "ProjectExplorer" }
    Depends { name: "CMakeProjectManager" }
    Depends { name: "QtSupport" }

    files: [
        "mcusupport.qrc",
        "mcusupport_global.h",
        "mcusupportconstants.h",
        "mcusupportdevice.cpp",
        "mcusupportdevice.h",
        "mcusupportoptionspage.cpp",
        "mcusupportoptionspage.h",
        "mcusupportplugin.cpp",
        "mcusupportplugin.h",
        "mcusupportrunconfiguration.cpp",
        "mcusupportrunconfiguration.h",
    ]
}
