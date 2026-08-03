#include "../include/arm.h"
#include "../include/test_helpers.h"

using namespace arm;
using namespace ilang;

// NOTE: the tests chain two instructions, first read from A and write to B, then read new B and write C
void test_spl_svl(ArmSme& sme) {
    CHECK("ADDSPL chain read X[3] write to SP, then read SP write to X[10]", sme, {"ADDSPL", "ADDSPL"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            // first phase (step 0)
            cstr_step_bv(s, u, ctx, sme.SP, 0ULL, sme.SP.bit_width()); // zero out SP
            cstr_step_bv(s, u, ctx, sme.Rn, 0x03ULL, sme.Rn.bit_width()); // read X[3]
            cstr_step_bv(s, u, ctx, sme.Rd, 0x1FULL, sme.Rd.bit_width()); // write SP (idx 31)
            cstr_step_bv(s, u, ctx, sme.GPRs[3], 0x4ULL, 64); // X[3] = 0x4
            cstr_step_bv(s, u, ctx, sme.Imm6, 0xFDULL, sme.Imm6.bit_width()); // Imm6 = -3
            
            // next phase (step 1)
            cstr_step_bv(s, u, ctx, sme.Rn, 0x1FULL, sme.Rn.bit_width(), 1); // read SP (idx 31)
            cstr_step_bv(s, u, ctx, sme.Rd, 0xAULL, sme.Rd.bit_width(), 1); // write X[10]
            cstr_step_bv(s, u, ctx, sme.GPRs[10], 0ULL, 64, 1); // zero out X[10]
            cstr_step_bv(s, u, ctx, sme.Imm6, 0xFAULL, sme.Imm6.bit_width(), 1); // Imm6 = -6
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            // first result (step 1)
            PRINT(sme.Imm6, 0, u, mdl, "Imm6 @ 0");
            PRINT(sme.SP, 0, u, mdl, "SP @ 0 (zeroed)");
            PRINT(sme.SP, 1, u, mdl, "SP @ 1");
            EXPECT_TRUE(TO_STR(sme.SP, 1, u, mdl) == "#xfffffffffffffffe"); // SP = -2
            
            // second result (step 2)
            PRINT(sme.Imm6, 1, u, mdl, "Imm6 @ 1");
            PRINT(sme.GPRs[10], 1, u, mdl, "X[10] @ 1 (zeroed)");
            PRINT(sme.GPRs[10], 2, u, mdl, "X[10] @ 2");
            EXPECT_TRUE(TO_STR(sme.GPRs[10], 2, u, mdl) == "#xfffffffffffffff2"); // X[10] = -14
        }
    );

    CHECK("ADDSVL chain read SP write to X[3], then read X[3] write to SP", sme, {"ADDSVL", "ADDSVL"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            // first phase (step 0)
            cstr_step_bv(s, u, ctx, sme.GPRs[3], 0ULL, 64); // zero out X[3]
            cstr_step_bv(s, u, ctx, sme.SP, 0x4ULL, 64); // SP = 0x4
            cstr_step_bv(s, u, ctx, sme.Rn, 0x1FULL, sme.Rn.bit_width()); // read SP
            cstr_step_bv(s, u, ctx, sme.Rd, 0x03ULL, sme.Rd.bit_width()); // write X[3]
            cstr_step_bv(s, u, ctx, sme.Imm6, 0xFDULL, sme.Imm6.bit_width()); // Imm6 = -3
            
            // next phase (step 1)
            cstr_step_bv(s, u, ctx, sme.Rn, 0x3ULL, sme.Rn.bit_width(), 1); // read X[3]
            cstr_step_bv(s, u, ctx, sme.Rd, 0x1FULL, sme.Rd.bit_width(), 1); // write SP
            // do not zero SP, because SP = 0x4 is defined by previous phase, otherwise unsat
            cstr_step_bv(s, u, ctx, sme.Imm6, 0xFAULL, sme.Imm6.bit_width(), 1); // Imm6 = -6
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            // first result (step 1)
            PRINT(sme.Imm6, 0, u, mdl, "Imm6 @ 0");
            PRINT(sme.GPRs[3], 0, u, mdl, "X[3] @ 0 (zeroed)");
            PRINT(sme.GPRs[3], 1, u, mdl, "X[3] @ 1");
            EXPECT_TRUE(TO_STR(sme.GPRs[3], 1, u, mdl) == "#xffffffffffffffd4"); // X[3] = -44
            
            // second result (step 2)
            PRINT(sme.Imm6, 1, u, mdl, "Imm6 @ 1");
            PRINT(sme.SP, 1, u, mdl, "SP @ 1 (zeroed)");
            PRINT(sme.SP, 2, u, mdl, "SP @ 2");
            EXPECT_TRUE(TO_STR(sme.SP, 2, u, mdl) == "#xffffffffffffff74"); // SP = -140
        }
    );

    CHECK("RDSVL reads value into X[3]", sme, {"RDSVL"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            cstr_step_bv(s, u, ctx, sme.GPRs[3], 0ULL, 64); // zero out X[3]
            cstr_step_bv(s, u, ctx, sme.Rd, 0x03ULL, sme.Rd.bit_width()); // write X[3]
            cstr_step_bv(s, u, ctx, sme.Imm6, 0xFDULL, sme.Imm6.bit_width()); // Imm6 = -3
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            PRINT(sme.Imm6, 0, u, mdl, "Imm6 @ 0");
            EXPECT_TRUE(TO_STR(sme.GPRs[3], 1, u, mdl) == "#xffffffffffffffd0"); // X[3] = -48
        }
    );
}
