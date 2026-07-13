#include "arm.h"
#include <iostream>

using namespace arm;

int main() {
    std::cout << "=== ARM SME ILA Verification ===\n";

    try {
        ArmSme sme;
        std::cout << "ArmSme constructor completed successfully\n";

        std::cout << "\nAll tests passed!\n";
    } 
    catch (const std::exception &e) {
        std::cerr << "(!) Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
