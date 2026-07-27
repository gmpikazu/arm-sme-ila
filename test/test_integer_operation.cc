#include "../include/test_helpers.h"
#include "../include/arm.h"

using namespace ilang;
using namespace arm;

void test_integer_outer_prod(ArmSme& sme) {
    // TODO continue
}

void test_addha_addva(ArmSme& sme) {
    CHECK("ADDHA.S accumulates horizontally except first row of ZA2.S", sme, {"ADDHA.S"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            cstr_step_bv(s, u, ctx, sme.ZAda, 2ULL, sme.ZAda.bit_width()); // tile 2
            // predicates
            cstr_step_bv(s, u, ctx, sme.Pn, 0ULL, sme.Pn.bit_width()); // P[0]
            // cstr_step(s, u, ctx, sme.p_regs[0], ctx.bv_val(-1, P_REG_WIDTH)); // all ones
            cstr_step_bv(s, u, ctx, sme.p_regs[0], 0xFEULL, P_REG_WIDTH); // except first row
            cstr_step_bv(s, u, ctx, sme.Pm, 1ULL, sme.Pm.bit_width()); // P[1]
            cstr_step(s, u, ctx, sme.p_regs[1], ctx.bv_val(-1, P_REG_WIDTH)); // all ones
            // source vector
            cstr_step_bv(s, u, ctx, sme.Zn, 3ULL, sme.Zn.bit_width()); // Z[3]
            // add 5 on left, 6 on right
            cstr_step(s, u, ctx, sme.z_regs[3], bv_val_128(ctx, 0x0000000500000005, 0x0000000600000006));
            // populate row 0-2 of tile 2
            cstr_step(s, u, ctx, sme.GetHorizontalSlice(sme.za, 2, 0, WORD), bv_val_128(ctx, 0x0000000100000001, 0x0000000100000001));
            cstr_step(s, u, ctx, sme.GetHorizontalSlice(sme.za, 2, 1, WORD), bv_val_128(ctx, 0x0000000200000002, 0x0000000200000002));
            cstr_step(s, u, ctx, sme.GetHorizontalSlice(sme.za, 2, 2, WORD), bv_val_128(ctx, 0x0000000300000003, 0x0000000300000003));
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            auto slice0 = sme.GetHorizontalSlice(sme.za, 2, 0, WORD);
            auto slice1 = sme.GetHorizontalSlice(sme.za, 2, 1, WORD);
            auto slice2 = sme.GetHorizontalSlice(sme.za, 2, 2, WORD);
            PrintZa(mdl, u, sme, 0);
            std::cout << " added 5 to the left column, added 6 to the right column\n";
            PrintZa(mdl, u, sme, 1);
            EXPECT_TRUE(TO_STR(slice0, 1, u, mdl) == TO_STR(slice0, 0, u, mdl));
            EXPECT_TRUE(TO_STR(slice1, 1, u, mdl) == "#x00000007000000070000000800000008");
            EXPECT_TRUE(TO_STR(slice2, 1, u, mdl) == "#x00000008000000080000000900000009");
        }
    );
    CHECK("ADDVA.D accumulates vertically on ZA7.D", sme, {"ADDVA.D"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            cstr_step_bv(s, u, ctx, sme.ZAda, 2ULL, sme.ZAda.bit_width()); // tile 7
            // predicates
            cstr_step_bv(s, u, ctx, sme.Pn, 0ULL, sme.Pn.bit_width()); // P[0]
            cstr_step(s, u, ctx, sme.p_regs[0], ctx.bv_val(-1, P_REG_WIDTH)); // all ones
            cstr_step_bv(s, u, ctx, sme.Pm, 1ULL, sme.Pm.bit_width()); // P[1]
            cstr_step(s, u, ctx, sme.p_regs[1], ctx.bv_val(-1, P_REG_WIDTH)); // all ones
            // source vector
            cstr_step_bv(s, u, ctx, sme.Zn, 3ULL, sme.Zn.bit_width()); // Z[3]
            // add 5 on bottom, 6 on top
            cstr_step(s, u, ctx, sme.z_regs[3], bv_val_128(ctx, 0x0000000000000005, 0x0000000000000006));
            // populate col 0-1 of tile 7
            cstr_step(s, u, ctx, sme.GetVerticalSlice(sme.za, 2, 0, DOUBLE), bv_val_128(ctx, 0x0000000000000001, 0x0000000000000001));
            cstr_step(s, u, ctx, sme.GetVerticalSlice(sme.za, 2, 1, DOUBLE), bv_val_128(ctx, 0x0000000000000002, 0x0000000000000002));
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            PrintZa(mdl, u, sme, 0);
            std::cout << " added 5 to bottom row, 6 to top row\n";
            PrintZa(mdl, u, sme, 1);
            auto slice0 = sme.GetVerticalSlice(sme.za, 2, 0, DOUBLE);
            auto slice1 = sme.GetVerticalSlice(sme.za, 2, 1, DOUBLE);
            PRINT(slice0, 1, u, mdl, "vertical slice on right");
            PRINT(slice1, 1, u, mdl, "vertical slice on left");
            EXPECT_TRUE(TO_STR(slice0, 1, u, mdl) == "#x00000000000000060000000000000007");
            EXPECT_TRUE(TO_STR(slice1, 1, u, mdl) == "#x00000000000000070000000000000008");
        }
    );
}