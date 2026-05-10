#include "EditorApp.hpp"

#include <iostream>

int main() {
    forge::editor::EditorApp app;

    if (!app.init()) {
        std::cerr << "[forge-editor] Initialisation failed.\n";
        return 1;
    }

    app.run();
    app.shutdown();
    return 0;
}
