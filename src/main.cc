#include "arm.h"
#include "test.h"
#include "test_helpers.h"
#include <iostream>

/** Multipurpose Main Function
 * to build the model and check that new instructions or states were added correctly
 * to call out to test scripts in tests/
 */

using namespace arm;

void list_instrs(ArmSme& sme) {
    Ila m = sme.get();
    
    std::cout << "\nInstructions created:\n";
    for (size_t i = 0; i < m.instr_num(); i++){
        std::cout << "  " << m.instr(i).name() << std::endl;
    }
    std::cout << std::endl;
}

void list_states(ArmSme& sme) {
    Ila m = sme.get();

    std::cout << "\nState variables:\n";
    for (size_t i = 0; i < m.state_num(); i++){
        auto state = m.state(i);
        std::cout << "  " << state.name() << " (" << state.bit_width() << " bits)" << std::endl;
    }
    std::cout << std::endl;
}

int main() {
    ArmSme sme;
    
    // list everything
    list_states(sme);
    list_instrs(sme);
    
    // instruction unit tests
    // test_quick(sme);
    // test_pstate(sme);
    // test_cstr_helper(sme);
    // test_slice_helper(sme);
    // test_zero(sme);
    test_mova(sme);
    
    // summary
    print_test_summary();

    return 0;
}
