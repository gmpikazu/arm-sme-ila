#include "../include/test_helpers.h"
#include "../include/arm.h"

using namespace ilang;
using namespace arm;

void test_float_outer_prod(ArmSme& sme) {

    // NOTE: must pass in sub lambda to each CHECK() call otherwise UFs left unsubstituted
    auto sub = [](const z3::expr& tr, IlaZ3Unroller& u, ArmSme& s, z3::context& ctx) -> z3::expr {
        return substitute_fp_ufs(tr, u, s, ctx);
    };

    // fp64 10.0 = 0x4024000000000000
    constexpr uint64_t fp64_elem1 = 0x4024000000000000ULL; // [10]
    // fp64 20.0 = 0x4034000000000000
    constexpr uint64_t fp64_elem2 = 0x4034000000000000ULL; // [20]

    CHECK("FMOPA (fp64) computes correct matrix in ZA6.D", sme, {"FMOPA (fp64)"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            InitZaToZero(s, u, ctx, sme);
            cstr_step_bv(s, u, ctx, sme.ZAda, 0x06ULL, sme.ZAda.bit_width()); // tile 6
            // predicates: P[1] and P[2] all-ones so every (row, col) pair is active
            cstr_step_bv(s, u, ctx, sme.Pn, 0x01ULL, sme.Pn.bit_width()); // P[1]
            cstr_step(s, u, ctx, sme.p_regs[1], ctx.bv_val(-1, sme.P_REG_WIDTH));
            cstr_step_bv(s, u, ctx, sme.Pm, 0x02ULL, sme.Pm.bit_width()); // P[2]
            cstr_step(s, u, ctx, sme.p_regs[2], ctx.bv_val(-1, sme.P_REG_WIDTH));
            // vector registers: each 128-bit vector contains [10.0, 20.0]
            cstr_step_bv(s, u, ctx, sme.Zn, 0x01ULL, sme.Zn.bit_width()); // Z[1]
            cstr_step(s, u, ctx, sme.z_regs[1], bv_val_128(ctx, fp64_elem1, fp64_elem2));
            cstr_step_bv(s, u, ctx, sme.Zm, 0x02ULL, sme.Zm.bit_width()); // Z[2]
            cstr_step(s, u, ctx, sme.z_regs[2], bv_val_128(ctx, fp64_elem1, fp64_elem2));
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            std::cout << " input vector registers Z[1] and Z[2]\n";
            PRINT(sme.z_regs[1], 0, u, mdl, "Zn @ 0");
            PRINT(sme.z_regs[2], 0, u, mdl, "Zm @ 0");
            std::cout << " ZA initially zeroed out\n";
            PrintZa(mdl, u, sme, 0);
            std::cout << " then contains the resulting matrix\n";
            PrintZa(mdl, u, sme, 1);
            std::cout << " row slices of ZA2H.S from top to bottom\n";
            std::vector<ExprRef> row_slices;
            for (size_t i = 0; i < 2; i++){
                row_slices.push_back(sme.GetHorizontalSlice(sme.za, 6, i, DOUBLE));
            }
            PRINT(row_slices[0], 1, u, mdl, "ZA6H.D[0] @ 1");
            PRINT(row_slices[1], 1, u, mdl, "ZA6H.D[1] @ 1");
            EXPECT_TRUE(TO_STR(row_slices[0], 1, u, mdl) == "#x40690000000000004079000000000000");
            EXPECT_TRUE(TO_STR(row_slices[1], 1, u, mdl) == "#x40590000000000004069000000000000");
        },
        sub // NOTE: enable IEEE substitution
    );

    // fp32 1.0 = 0x3F800000, fp32 3.0 = 0x40400000
    constexpr uint64_t fp32_pair1 = 0x3F80000040400000ULL; // [1.0, 3.0]
    // fp32 5.0 = 0x40A00000, fp32 7.0 = 0x40E00000
    constexpr uint64_t fp32_pair2 = 0x40A0000040E00000ULL; // [5.0, 7.0]

    CHECK("FMOPS (fp32) subtracts diagonal matrix from zeroed ZA2.S", sme, {"FMOPS (fp32)"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            InitZaToZero(s, u, ctx, sme);
            cstr_step_bv(s, u, ctx, sme.ZAda, 0x02ULL, sme.ZAda.bit_width()); // tile 2
            // predicates: P[1] and P[2] all-ones so every (row, col) pair is active
            cstr_step_bv(s, u, ctx, sme.Pn, 0x01ULL, sme.Pn.bit_width()); // P[1]
            cstr_step(s, u, ctx, sme.p_regs[1], ctx.bv_val(-1, sme.P_REG_WIDTH));
            cstr_step_bv(s, u, ctx, sme.Pm, 0x02ULL, sme.Pm.bit_width()); // P[2]
            cstr_step(s, u, ctx, sme.p_regs[2], ctx.bv_val(-1, sme.P_REG_WIDTH));
            // vector registers: each 128-bit vector contains [1.0, 3.0, 5.0, 7.0]
            cstr_step_bv(s, u, ctx, sme.Zn, 0x01ULL, sme.Zn.bit_width()); // Z[1]
            cstr_step(s, u, ctx, sme.z_regs[1], bv_val_128(ctx, fp32_pair1, fp32_pair2));
            cstr_step_bv(s, u, ctx, sme.Zm, 0x02ULL, sme.Zm.bit_width()); // Z[2]
            cstr_step(s, u, ctx, sme.z_regs[2], bv_val_128(ctx, fp32_pair1, fp32_pair2));
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            std::cout << " input vector registers Z[1] and Z[2]\n";
            PRINT(sme.z_regs[1], 0, u, mdl, "Zn @ 0");
            PRINT(sme.z_regs[2], 0, u, mdl, "Zm @ 0");
            std::cout << " ZA initially zeroed out\n";
            PrintZa(mdl, u, sme, 0);
            std::cout << " then contains the resulting matrix\n";
            PrintZa(mdl, u, sme, 1);
            std::cout << " row slices of ZA2H.S from top to bottom\n";
            std::vector<ExprRef> row_slices;
            for (size_t i = 0; i < 4; i++){
                row_slices.push_back(sme.GetHorizontalSlice(sme.za, 2, i, WORD));
            }
            PRINT(row_slices[0], 1, u, mdl, "ZA2H.S[0] @ 1");
            PRINT(row_slices[1], 1, u, mdl, "ZA2H.S[1] @ 1");
            PRINT(row_slices[2], 1, u, mdl, "ZA2H.S[2] @ 1");
            PRINT(row_slices[3], 1, u, mdl, "ZA2H.S[3] @ 1");
            EXPECT_TRUE(TO_STR(row_slices[0], 1, u, mdl) == "#xc0e00000c1a80000c20c0000c2440000");
            EXPECT_TRUE(TO_STR(row_slices[1], 1, u, mdl) == "#xc0a00000c1700000c1c80000c20c0000");
            EXPECT_TRUE(TO_STR(row_slices[2], 1, u, mdl) == "#xc0400000c1100000c1700000c1a80000");
            EXPECT_TRUE(TO_STR(row_slices[3], 1, u, mdl) == "#xbf800000c0400000c0a00000c0e00000");
        },
        sub // NOTE: enable IEEE substitution
    );

    // fp16 1.0 = 0x3C00, fp16 2.0 = 0x4000
    constexpr uint64_t fp16_pair1 = 0x3C0040003C004000ULL;   // [1.0, 2.0, 1.0, 2.0]
    // fp16 3.0 = 0x4200, fp16 5.0 = 0x4500
    constexpr uint64_t fp16_pair2 = 0x4200450042004500ULL;   // [3.0, 5.0, 3.0, 5.0]
    // fp32 10.0 = 0x41200000
    constexpr uint64_t fp32_pattern = 0x4120000041200000ULL; // [10.0,     10.0    ]

    CHECK("FMOPS (fp16->fp32) subtracts and leaves plus (+) pattern untouched", sme, {"FMOPS (fp16->fp32)"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            // init ZA with pattern
            Tracker t;
            track_slice(t, bv_val_128(ctx, fp32_pattern, fp32_pattern), 1, 0, false, WORD, sme);
            track_slice(t, bv_val_128(ctx, fp32_pattern, fp32_pattern), 1, 1, false, WORD, sme);
            track_slice(t, bv_val_128(ctx, fp32_pattern, fp32_pattern), 1, 2, false, WORD, sme);
            track_slice(t, bv_val_128(ctx, fp32_pattern, fp32_pattern), 1, 3, false, WORD, sme);
            cstr_all_tracked_and_zero(s, u, ctx, t, sme);
            cstr_step_bv(s, u, ctx, sme.ZAda, 0x01ULL, sme.ZAda.bit_width()); // tile 1
            // predicates: P[1] = all except row 2 and P[2] = all except col 1
            cstr_step_bv(s, u, ctx, sme.Pn, 0x01ULL, sme.Pn.bit_width()); // P[1]
            cstr_step_bv(s, u, ctx, sme.p_regs[1], 0x5055ULL, sme.P_REG_WIDTH);
            cstr_step_bv(s, u, ctx, sme.Pm, 0x02ULL, sme.Pm.bit_width()); // P[2]
            cstr_step_bv(s, u, ctx, sme.p_regs[2], 0x5505ULL, sme.P_REG_WIDTH);
            // vector registers: each 128-bit vector contains [1.0, 2.0, 1.0, 2.0, 3.0, 5.0, 3.0, 5.0]
            cstr_step_bv(s, u, ctx, sme.Zn, 0x01ULL, sme.Zn.bit_width()); // Z[1]
            cstr_step(s, u, ctx, sme.z_regs[1], bv_val_128(ctx, fp16_pair1, fp16_pair2));
            cstr_step_bv(s, u, ctx, sme.Zm, 0x02ULL, sme.Zm.bit_width()); // Z[2]
            cstr_step(s, u, ctx, sme.z_regs[2], bv_val_128(ctx, fp16_pair1, fp16_pair2));
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            std::cout << " input vector registers Z[1] and Z[2]\n";
            PRINT(sme.z_regs[1], 0, u, mdl, "Zn @ 0");
            PRINT(sme.z_regs[2], 0, u, mdl, "Zm @ 0");
            std::cout << " ZA initially contains a pattern\n";
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
            // NOTE: if predicate was all ones and ZA initally zeroed, final output should be like this
            // EXPECT_TRUE(TO_STR(row_slices[0], 1, u, mdl) == "#xc1500000c1500000c2080000c2080000");
            // EXPECT_TRUE(TO_STR(row_slices[1], 1, u, mdl) == "#xc1500000c1500000c2080000c2080000");
            // EXPECT_TRUE(TO_STR(row_slices[2], 1, u, mdl) == "#xc0a00000c0a00000c1500000c1500000");
            // EXPECT_TRUE(TO_STR(row_slices[3], 1, u, mdl) == "#xc0a00000c0a00000c1500000c1500000");
            EXPECT_TRUE(TO_STR(row_slices[0], 1, u, mdl) == "#xc0400000c040000041200000c1c00000");
            EXPECT_TRUE(TO_STR(row_slices[1], 1, u, mdl) == "#xc0400000c040000041200000c1c00000");
            EXPECT_TRUE(TO_STR(row_slices[2], 1, u, mdl) == "#x41200000412000004120000041200000");
            EXPECT_TRUE(TO_STR(row_slices[3], 1, u, mdl) == "#x40a0000040a0000041200000c0400000");
        },
        sub // NOTE: enable IEEE substitution
    );

    // bf16 2.0 = 0x4000, bf16 4.0 = 0x4080
    constexpr uint64_t bf16_pair1 = 0x4000408040004080ULL; // [2.0, 4.0, 2.0, 4.0]
    // bf16 1.0 = 0x3F80, bf16 3.0 = 0x4040
    constexpr uint64_t bf16_pair2 = 0x3F8040403F804040ULL; // [1.0, 3.0, 1.0, 3.0]

    CHECK("BFMOPA produces computed matrix in zeroed ZA1.S", sme, {"BFMOPA (bf16->fp32)"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            InitZaToZero(s, u, ctx, sme);
            cstr_step_bv(s, u, ctx, sme.ZAda, 0x01ULL, sme.ZAda.bit_width()); // tile 1
            // predicates: P[1] and P[2] all-ones so every (row, col) pair is active
            cstr_step_bv(s, u, ctx, sme.Pn, 0x01ULL, sme.Pn.bit_width()); // P[1]
            cstr_step(s, u, ctx, sme.p_regs[1], ctx.bv_val(-1, sme.P_REG_WIDTH));
            cstr_step_bv(s, u, ctx, sme.Pm, 0x02ULL, sme.Pm.bit_width()); // P[2]
            cstr_step(s, u, ctx, sme.p_regs[2], ctx.bv_val(-1, sme.P_REG_WIDTH));
            // vector registers: each 128-bit vector contains [2.0, 4.0, 2.0, 4.0, 1.0, 3.0, 1.0, 3.0]
            cstr_step_bv(s, u, ctx, sme.Zn, 0x01ULL, sme.Zn.bit_width()); // Z[1]
            cstr_step(s, u, ctx, sme.z_regs[1], bv_val_128(ctx, bf16_pair1, bf16_pair2));
            cstr_step_bv(s, u, ctx, sme.Zm, 0x02ULL, sme.Zm.bit_width()); // Z[2]
            cstr_step(s, u, ctx, sme.z_regs[2], bv_val_128(ctx, bf16_pair1, bf16_pair2));
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
            EXPECT_TRUE(TO_STR(row_slices[0], 1, u, mdl) == "#x41600000416000004120000041200000");
            EXPECT_TRUE(TO_STR(row_slices[1], 1, u, mdl) == "#x41600000416000004120000041200000");
            EXPECT_TRUE(TO_STR(row_slices[2], 1, u, mdl) == "#x41a0000041a000004160000041600000");
            EXPECT_TRUE(TO_STR(row_slices[3], 1, u, mdl) == "#x41a0000041a000004160000041600000");
        },
        sub // NOTE: enable IEEE substitution
    );
}
