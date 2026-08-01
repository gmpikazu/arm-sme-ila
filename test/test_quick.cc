#include "../include/arm.h"
#include "../include/test_helpers.h"

using namespace arm;
using namespace ilang;

// NOTE: scratchpad to test new verification ideas
void test_quick(ArmSme& sme) {
    CHECK("ZA dump state into CSV", sme, {"ZERO"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            // REQUIRED: Constrain Imm8 to 0xFF for ZERO to work
            cstr_step_bv(s, u, ctx, sme.Imm8, 0xFF, 8);
            // Set up a non-zero horizontal slice at step 0
            auto hor_slice = sme.GetHorizontalSlice(sme.za, 0, 0, BYTE);
            cstr_step(s, u, ctx, hor_slice, bv_val(ctx, {0x0F0E0D0C0B0A0908ULL, 0x0706050403020100ULL}), 0);
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            auto hor_slice = sme.GetHorizontalSlice(sme.za, 0, 0, BYTE);
            
            std::cout << "\n=== Step 0 (before ZERO) ===" << std::endl;
            PrintZa(mdl, u, sme, 0);
            // Show evaluated value instead of symbolic expression
            PRINT(hor_slice, 0, u, mdl, "before ZERO");
            
            std::cout << "\n=== Step 1 (after ZERO) ===" << std::endl;
            PrintZa(mdl, u, sme, 1); 
            // Show evaluated value instead of symbolic expression
            PRINT(hor_slice, 1, u, mdl, "after ZERO");
        }
    );
}
