#include <iostream>
#include <nexus/component.hpp>

int main() {
    nexus::Component kde(
    "desktop.kde",
    "KDE Plasma",
    "6.0",
    nexus::ComponentType::Desktop
);

    std::cout << "Nexus Core\n";
    std::cout << "Component: " << kde.name() << "\n";
    std::cout << "ID: " << kde.id() << "\n";
    std::cout << "Version: " << kde.version() << "\n";

    return 0;
}
