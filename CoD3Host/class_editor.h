#pragma once

// The custom class editor on the kit screen (class_editor.cpp, multiplayer
// only).
namespace ClassEditor
{
    void Open();
    bool IsOpen();   // while open, the menus get no buttons (lobby_rules.cpp)
}
