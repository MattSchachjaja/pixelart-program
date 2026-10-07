cmake_minimum_required(VERSION 3.16)
project(minipaint LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# AUTOMOC verarbeitet alle Q_OBJECT-Klassen in den Headern automatisch,
# daher wird kein "#include ...moc" mehr benötigt.
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui OpenGL OpenGLWidgets)

add_executable(minipaint
    # --- src/ui (Toplevel-UI & Einstieg) ---
    src/ui/main.cpp
    src/ui/mainwindow.cpp
    src/ui/mainwindow.h
    src/ui/dialogs.cpp
    src/ui/dialogs.h

    # --- src/core (Datenmodell & Logik) ---
    src/core/config.h
    src/core/colorutils.h
    src/core/layer.h
    src/core/document.cpp
    src/core/document.h
    src/core/colorstate.cpp
    src/core/colorstate.h
    src/core/history.cpp
    src/core/history.h
    src/core/commands.cpp
    src/core/commands.h

    # --- src/tools (ITool-Interface + Werkzeuge + ToolManager) ---
    src/tools/tool.h
    src/tools/tools.cpp
    src/tools/tools.h

    # --- src/widgets (Wiederverwendbare Qt-Widgets/Docks) ---
    src/widgets/canvaswidget.cpp
    src/widgets/canvaswidget.h
    src/widgets/layerdock.cpp
    src/widgets/layerdock.h
    src/widgets/historydock.cpp
    src/widgets/historydock.h
    src/widgets/colorwidgets.cpp
    src/widgets/colorwidgets.h
    src/widgets/palettedock.cpp
    src/widgets/palettedock.h
    src/widgets/toolsdock.cpp
    src/widgets/toolsdock.h
)

# Damit die bestehenden #include "document.h"-Statements ohne Anpassung
# weiterhin funktionieren, werden alle Unterordner als Include-Pfade gesetzt.
target_include_directories(minipaint PRIVATE
    src/core
    src/tools
    src/widgets
    src/ui
)

target_link_libraries(minipaint PRIVATE
    Qt6::Widgets
    Qt6::Gui
    Qt6::OpenGL
    Qt6::OpenGLWidgets
)