#include "arm.h"
#include "test.h"
#include "test_helpers.h"
#include <iostream>
#include <glog/logging.h> // supress ILAng logging for DRAM test using UFs

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
    fLI::FLAGS_minloglevel = 2; // NOTE: remove noisy ILAng logs

    // NOTE: only DRAM tests require both, others don't matter
    ArmSme sme_DramLE(true); // DRAM is Little Endian
    ArmSme sme_DramBE(false); // DRAM is Big Endian
    
    // list everything
    list_states(sme_DramLE);
    list_instrs(sme_DramLE);
    
    // instruction unit tests
    // test_quick(sme_DramLE);

    // NOTE: tested and working
    // test_pstate(sme_DramLE);
    // test_cstr_helper(sme_DramLE);
    // test_slice_helper(sme_DramLE);
    // test_zero(sme_DramLE);
    // test_mova(sme_DramLE);
    // test_addha_addva(sme_DramLE);
    // test_integer_outer_prod(sme_DramLE);
    // test_spl_svl(sme_DramLE);

    // NOTE: under development
    // test_float_outer_prod(sme_DramLE);
    // test_load(sme_DramLE, sme_DramBE);
    test_store(sme_DramLE, sme_DramBE);
    
    // summary
    print_test_summary();

    return 0;
}
