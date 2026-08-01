#include "../include/test_helpers.h"
#include "../include/arm.h"

using namespace ilang;
using namespace arm;

void test_integer_outer_prod(ArmSme& sme) {
    CHECK("UMOPA (8b->32b) correctly computes new diagonal matrix sum using predicates", sme, {"UMOPA (8b->32b)"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            InitZaToZero(s, u, ctx, sme);
            cstr_step_bv(s, u, ctx, sme.ZAda, 0x01ULL, sme.ZAda.bit_width()); // tile 1
            // predicates: 0x1248 = 0b_0001_0010_0100_1000 for selection, cross terms disappear
            cstr_step_bv(s, u, ctx, sme.Pn, 0x01ULL, sme.Pn.bit_width()); // P[1]
            cstr_step_bv(s, u, ctx, sme.p_regs[1], 0x1248ULL, P_REG_WIDTH); // 0x1248
            cstr_step_bv(s, u, ctx, sme.Pm, 0x02ULL, sme.Pm.bit_width()); // P[2]
            cstr_step_bv(s, u, ctx, sme.p_regs[2], 0x1248ULL, P_REG_WIDTH); // 0x1248
            // vector registers
            cstr_step_bv(s, u, ctx, sme.Zn, 0x01ULL, sme.Zn.bit_width()); // Z[1]
            cstr_step(s, u, ctx, sme.z_regs[1], bv_val_128(ctx, 0x0001020304050607ULL, 0x08090A0B0C0D0E0FULL));
            cstr_step_bv(s, u, ctx, sme.Zm, 0x02ULL, sme.Zm.bit_width()); // Z[2]
            cstr_step(s, u, ctx, sme.z_regs[2], bv_val_128(ctx, 0x0001020304050607ULL, 0x08090A0B0C0D0E0FULL));
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            std::cout << " input vector registers Z[1] and Z[2]\n";
            PRINT(sme.z_regs[1], 0, u, mdl, "Zn @ 0");
            PRINT(sme.z_regs[2], 0, u, mdl, "Zm @ 0");
            std::cout << " ZA initially zeroed out\n";
            PrintZa(mdl, u, sme, 0);
            std::cout << " then contains the resulting matrix\n";
            PrintZa(mdl, u, sme, 1);
            std::cout << " row slices of ZA1H.S from top to bottom\n";
            std::vector<ExprRef> row_slices;
            for (size_t i = 0; i < 4; i++){
                row_slices.push_back(sme.GetHorizontalSlice(sme.za, 1, i, WORD));
            }
            PRINT(row_slices[0], 1, u, mdl, "ZA1H.S[0] @ 1");
            PRINT(row_slices[1], 1, u, mdl, "ZA1H.S[1] @ 1");
            PRINT(row_slices[2], 1, u, mdl, "ZA1H.S[2] @ 1");
            PRINT(row_slices[3], 1, u, mdl, "ZA1H.S[3] @ 1");
            EXPECT_TRUE(TO_STR(row_slices[0], 1, u, mdl) == "#x00000000000000000000000000000090");
            EXPECT_TRUE(TO_STR(row_slices[1], 1, u, mdl) == "#x00000000000000000000005100000000");
            EXPECT_TRUE(TO_STR(row_slices[2], 1, u, mdl) == "#x00000000000000240000000000000000");
            EXPECT_TRUE(TO_STR(row_slices[3], 1, u, mdl) == "#x00000009000000000000000000000000");
        }
    );

    CHECK("SMOPS (16b->64b) subtracts from original zero matrix (input is unsigned) with alternating predicates", sme, {"SMOPS (16b->64b)"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            InitZaToZero(s, u, ctx, sme);
            cstr_step_bv(s, u, ctx, sme.ZAda, 0x03ULL, sme.ZAda.bit_width()); // tile 3
            // predicates
            cstr_step_bv(s, u, ctx, sme.Pn, 0x01ULL, sme.Pn.bit_width()); // P[1]
            cstr_step(s, u, ctx, sme.p_regs[1], ctx.bv_val(-1, P_REG_WIDTH)); // all ones
            cstr_step_bv(s, u, ctx, sme.Pm, 0x02ULL, sme.Pm.bit_width()); // P[2]
            cstr_step(s, u, ctx, sme.p_regs[2], ctx.bv_val(-1, P_REG_WIDTH)); // all ones
            // vector registers
            cstr_step_bv(s, u, ctx, sme.Zn, 0x01ULL, sme.Zn.bit_width()); // Z[1]
            cstr_step(s, u, ctx, sme.z_regs[1], bv_val_128(ctx, 0x0001000200030004ULL, 0x0005000600070008ULL));
            cstr_step_bv(s, u, ctx, sme.Zm, 0x02ULL, sme.Zm.bit_width()); // Z[2]
            cstr_step(s, u, ctx, sme.z_regs[2], bv_val_128(ctx, 0x0001000200030004ULL, 0x0005000600070008ULL));
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            std::cout << " input vector registers Z[1] and Z[2]\n";
            PRINT(sme.z_regs[1], 0, u, mdl, "Zn @ 0");
            PRINT(sme.z_regs[2], 0, u, mdl, "Zm @ 0");
            std::cout << " ZA initially zeroed out\n";
            PrintZa(mdl, u, sme, 0);
            std::cout << " then contains the resulting matrix\n";
            PrintZa(mdl, u, sme, 1);
            std::cout << " row slices of ZA3H.D from top to bottom\n";
            std::vector<ExprRef> row_slices;
            for (size_t i = 0; i < 2; i++){
                row_slices.push_back(sme.GetHorizontalSlice(sme.za, 3, i, DOUBLE));
            }
            PRINT(row_slices[0], 1, u, mdl, "ZA3H.D[0] @ 1");
            PRINT(row_slices[1], 1, u, mdl, "ZA3H.D[1] @ 1");
            EXPECT_TRUE(TO_STR(row_slices[0], 1, u, mdl) == "#xffffffffffffffbaffffffffffffff52");
            EXPECT_TRUE(TO_STR(row_slices[1], 1, u, mdl) == "#xffffffffffffffe2ffffffffffffffba");
        }
    );

    CHECK("SMOPS (16b->64b) adds and subtracts original zero matrix (input signed and unsigned) with alternating predicates", sme, {"SMOPS (16b->64b)"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            InitZaToZero(s, u, ctx, sme);
            cstr_step_bv(s, u, ctx, sme.ZAda, 0x03ULL, sme.ZAda.bit_width()); // tile 3
            // predicates
            cstr_step_bv(s, u, ctx, sme.Pn, 0x01ULL, sme.Pn.bit_width()); // P[1]
            // cstr_step(s, u, ctx, sme.p_regs[1], ctx.bv_val(-1, P_REG_WIDTH)); // all ones
            cstr_step_bv(s, u, ctx, sme.p_regs[1], 0x1111ULL, P_REG_WIDTH); // alternating activation
            cstr_step_bv(s, u, ctx, sme.Pm, 0x02ULL, sme.Pm.bit_width()); // P[2]
            // cstr_step(s, u, ctx, sme.p_regs[2], ctx.bv_val(-1, P_REG_WIDTH)); // all ones
            cstr_step_bv(s, u, ctx, sme.p_regs[2], 0x1111ULL, P_REG_WIDTH); // alternating activation
            // vector registers
            cstr_step_bv(s, u, ctx, sme.Zn, 0x01ULL, sme.Zn.bit_width()); // Z[1]
            cstr_step(s, u, ctx, sme.z_regs[1], bv_val_128(ctx, 0xFFFFFFFEFFFDFFFCULL, 0x0001000200030004ULL));
            cstr_step_bv(s, u, ctx, sme.Zm, 0x02ULL, sme.Zm.bit_width()); // Z[2]
            cstr_step(s, u, ctx, sme.z_regs[2], bv_val_128(ctx, 0xFFFFFFFEFFFDFFFCULL, 0x0001000200030004ULL));
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            std::cout << " input vector registers Z[1] and Z[2]\n";
            PRINT(sme.z_regs[1], 0, u, mdl, "Zn @ 0");
            PRINT(sme.z_regs[2], 0, u, mdl, "Zm @ 0");
            std::cout << " ZA initially zeroed out\n";
            PrintZa(mdl, u, sme, 0);
            std::cout << " then contains the resulting matrix\n";
            PrintZa(mdl, u, sme, 1);
            std::cout << " row slices of ZA3H.D from top to bottom\n";
            std::vector<ExprRef> row_slices;
            for (size_t i = 0; i < 2; i++){
                row_slices.push_back(sme.GetHorizontalSlice(sme.za, 3, i, DOUBLE));
            }
            PRINT(row_slices[0], 1, u, mdl, "ZA3H.D[0] @ 1");
            PRINT(row_slices[1], 1, u, mdl, "ZA3H.D[1] @ 1");
            EXPECT_TRUE(TO_STR(row_slices[0], 1, u, mdl) == "#x0000000000000014ffffffffffffffec");
            EXPECT_TRUE(TO_STR(row_slices[1], 1, u, mdl) == "#xffffffffffffffec0000000000000014");
        }
    );
}

void test_addha_addva(ArmSme& sme) {
    CHECK("ADDHA.S accumulates horizontally except first row of ZA2.S", sme, {"ADDHA.S"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            cstr_step_bv(s, u, ctx, sme.ZAda, 2ULL, sme.ZAda.bit_width()); // tile 2
            // predicates
            cstr_step_bv(s, u, ctx, sme.Pn, 0ULL, sme.Pn.bit_width()); // P[0]
            // cstr_step(s, u, ctx, sme.p_regs[0], ctx.bv_val(-1, P_REG_WIDTH)); // all ones
            cstr_step_bv(s, u, ctx, sme.p_regs[0], 0xFFF0ULL, P_REG_WIDTH); // except first row
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
