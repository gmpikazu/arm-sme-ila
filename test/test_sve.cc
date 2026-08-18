#include "../include/test_helpers.h"
#include "../include/arm.h"

using namespace ilang;
using namespace arm;

// TODO: need test with bigger SVL
void test_revd(ArmSme& sme) {
    CHECK("asdkflasdfj", sme, {"REVD.Q"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            cstr_step_bv(s, u, ctx, sme.Zd, 3ULL, sme.Zd.bit_width()); // Zd = 3
            cstr_step(s, u, ctx, sme.z_regs[3], ctx.bv_val(-1, Z_REG_WIDTH)); // repeated F initially
            cstr_step_bv(s, u, ctx, sme.Pg, 5ULL, sme.Pg.bit_width());
            cstr_step(s, u, ctx, sme.p_regs[5], ctx.bv_val(-1, P_REG_WIDTH)); // all ones
            cstr_step_bv(s, u, ctx, sme.Zn, 7ULL, sme.Zn.bit_width());
            cstr_step(s, u, ctx, sme.z_regs[7], bv_val_128(ctx, 0x0011223344556677, 0x8899AABBCCDDEEFF));
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            PRINT(sme.z_regs[3], 0, u, mdl);
            PRINT(sme.z_regs[3], 1, u, mdl);
            EXPECT_TRUE(false); // TODO: test with bigger SVL
        }
    );
}

void test_clamp(ArmSme& sme) {
    CHECK("", sme, {""},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
        }
    );
}

void test_psel(ArmSme& sme) {
    CHECK("PSEL.D fills predicate, then PSEL.S zeroes out", sme, {"PSEL.D", "PSEL.S"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            // step 0
            cstr_step_bv(s, u, ctx, sme.Pd, 7ULL, sme.Pd.bit_width()); // dest = P[7]
            cstr_step_bv(s, u, ctx, sme.p_regs[7], 0x6767, P_REG_WIDTH); // init ugly number
            cstr_step_bv(s, u, ctx, sme.Pn, 1ULL, sme.Pn.bit_width()); // op1 = P[1]
            cstr_step(s, u, ctx, sme.p_regs[1], ctx.bv_val(-1, P_REG_WIDTH)); // F pattern
            cstr_step_bv(s, u, ctx, sme.Pm, 2ULL, sme.Pm.bit_width()); // op2 = P[2]
            cstr_step_bv(s, u, ctx, sme.p_regs[2], 0x0306, P_REG_WIDTH); // only some are ones
            // index into predicate
            cstr_step_bv(s, u, ctx, sme.Rv, 13ULL, sme.Rv.bit_width()); // index = W[13]
            cstr_step_bv(s, u, ctx, sme.Get32BitGPR(13), 0ULL, 32); // W[13] = 0
            cstr_step_bv(s, u, ctx, sme.Imm1, 1ULL, sme.Imm1.bit_width()); // imm = 1

            // step 1
            cstr_step_bv(s, u, ctx, sme.Pd, 7ULL, sme.Pd.bit_width(), 1); // dest = P[7]
            cstr_step_bv(s, u, ctx, sme.Pn, 1ULL, sme.Pn.bit_width(), 1); // op1 = P[1]
            cstr_step_bv(s, u, ctx, sme.Pm, 2ULL, sme.Pm.bit_width(), 1); // op2 = P[2]
            // index into predicate
            cstr_step_bv(s, u, ctx, sme.Rv, 13ULL, sme.Rv.bit_width(), 1); // index = W[13]
            // W[13] already set in previous step
            cstr_step_bv(s, u, ctx, sme.Imm2, 0ULL, sme.Imm2.bit_width(), 1); // imm = 0
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            std::cout << " Z3 INVARIANT: P[1] and P[2] should remain the same throughout\n";
            EXPECT_TRUE(TO_STR(sme.p_regs[1], 0, u, mdl) == TO_STR(sme.p_regs[1], 1, u, mdl));
            EXPECT_TRUE(TO_STR(sme.p_regs[2], 0, u, mdl) == TO_STR(sme.p_regs[2], 1, u, mdl));

auto pred = sme.GetPredicateRegister(7);
            PRINT(pred, 0, u, mdl, "Pd @ 0 (ugly)");
            EXPECT_TRUE(TO_STR(pred, 0, u, mdl) == "#x6767");
            PRINT(pred, 1, u, mdl, "Pd @ 1 (pattern)");
            EXPECT_TRUE(TO_STR(pred, 1, u, mdl) == "#xffff");
            PRINT(pred, 2, u, mdl, "Pd @ 2 (zeroed)");
            EXPECT_TRUE(TO_STR(pred, 2, u, mdl) == "#x0000");
        }
    );
}
