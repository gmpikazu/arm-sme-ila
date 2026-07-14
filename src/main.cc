#include "arm.h"
#include <iostream>

using namespace arm;

int main() {
    std::cout << "=== Testing ILA ===\n";

    try {
        ArmSme sme;
        std::cout << "ArmSme constructor completed successfully\n";

        Ila m = sme.get();
        
        std::cout << "\nInstructions created:\n";
        for (size_t i = 0; i < m.instr_num(); i++){
            std::cout << "  " << m.instr(i).name() << std::endl;
        }

        std::cout << "\nState variables:\n";
        for (size_t i = 0; i < m.state_num(); i++){
            auto state = m.state(i);
            std::cout << "  " << state.name() << " (" << state.bit_width() << " bits)" << std::endl;
        }

        std::cout << "\nAll tests passed!\n";
    } 
    catch (const std::exception &e) {
        std::cerr << "(!) Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
