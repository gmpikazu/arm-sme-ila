#include "../include/test_helpers.h"
#include "../include/arm.h"

using namespace ilang;
using namespace arm;

void test_mova(ArmSme& sme) {
    
}

void test_zero(ArmSme &sme) {
    CHECK("ZERO Imm8=0xFF zeroes specific byte that was initialized to non-zero", sme, {"ZERO"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            // constrain Imm8 = 0xFF (all tiles activated)
            cstr_step_bv(s, u, ctx, sme.Imm8, 0xFF, 8); // 0xFF unsigned
            // constrain ZA[0] = 0xAA at step 0 (before calling ZERO)
            auto target_byte = Load(sme.za, BvConst(0, sme.za.addr_width()));
            cstr_step_bv(s, u, ctx, target_byte, 0xAA, BYTE); // fill with 0xAA unsigned
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            auto target_byte = Load(sme.za, BvConst(0, sme.za.addr_width()));
            auto tb_step0 = u.GetZ3Expr(target_byte, 0);
            auto tb_step1 = u.GetZ3Expr(target_byte, 1);
            // Z3 outputs bitvectors as #xAA (with #x prefix)
            EXPECT_TRUE(mdl.eval(tb_step0).to_string() == "#xaa");
            EXPECT_TRUE(mdl.eval(tb_step1).to_string() == "#x00"); // zeroed out
        }
    );

    CHECK("ZERO Imm8=0xFF zeroes entire ZA array that was initialized to non-zero", sme, {"ZERO"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            // constrain Imm8 = 0xFF (all tiles activated)
            cstr_step_bv(s, u, ctx, sme.Imm8, 0xFF, 8); // 0xFF unsigned
            // fill entire ZA with 0xAA at step 0
            for (size_t addr = 0; addr < ZA_BYTE_SIZE; addr++) {
                auto ila_byte = Load(sme.za, BvConst(addr, sme.za.addr_width()));
                cstr_step_bv(s, u, ctx, ila_byte, 0xAA, BYTE, 0);
            }
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            // verify ALL bytes are zero at step 1
            for (size_t addr = 0; addr < ZA_BYTE_SIZE; addr++) {
                auto ila_byte = Load(sme.za, BvConst(addr, sme.za.addr_width()));
                auto b_step0 = u.GetZ3Expr(ila_byte, 0);
                auto b_step1 = u.GetZ3Expr(ila_byte, 1);
                // Z3 outputs bitvectors as #x00 for zero
                EXPECT_TRUE(mdl.eval(b_step0).to_string() == "#xaa"); // not zeroed
                EXPECT_TRUE(mdl.eval(b_step1).to_string() == "#x00"); // zeroed out
            }
        }
    );
}
