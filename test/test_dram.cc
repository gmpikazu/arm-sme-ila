#include "../include/test_helpers.h"
#include "../include/arm.h"

using namespace ilang;
using namespace arm;

void test_store(ArmSme& sme) {
    // TODO:
}

void test_load(ArmSme& sme) {
    CHECK("LD1.H loads into ZA1V.H[3] from DRAM starting at zero with offset", sme, {"LD1.H"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            // tile
            cstr_step_bv(s, u, ctx, sme.ZAt, 1ULL, sme.ZAt.bit_width()); // tile 1
            // slice
            cstr_step_bool(s, u, ctx, sme.HV, true); // vertical
            cstr_step_bv(s, u, ctx, sme.Rs, 2ULL, sme.Rs.bit_width());
            cstr_step_bv(s, u, ctx, sme.Get32BitGPR(2), 0ULL, 32); // W[2] = 0
            cstr_step_bv(s, u, ctx, sme.Imm3, 3ULL, sme.Imm3.bit_width()); // slice 3
            // predicates
            cstr_step_bv(s, u, ctx, sme.Pg, 1ULL, sme.Pg.bit_width());
            cstr_step_bv(s, u, ctx, sme.p_regs[1], 0x5555ULL, P_REG_WIDTH); // all active
            // base & offset
            cstr_step_bv(s, u, ctx, sme.Rn, 3ULL, sme.Rn.bit_width()); // X[3]
            cstr_step_bv(s, u, ctx, sme.GPRs[3], 0ULL, 64); // base = 0
            cstr_step_bv(s, u, ctx, sme.Rm, 30ULL, sme.Rm.bit_width()); // X[30]
            cstr_step_bv(s, u, ctx, sme.GPRs[30], 3ULL, 64); // offset = 3 (starts at 3rd elem in DRAM)
            // DRAM
            auto mbytes = HALF / BYTE;
            for (size_t i = 0; i < 16; i++) {
                cstr_step(s, u, ctx, sme.DRAM_GetElementBytes(2 + i * mbytes, mbytes), ctx.bv_val(-1-i, HALF));
            }
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            std::cout << " vertical slice filled except one\n";
            PrintZa(mdl, u, sme, 1);
            std::vector<ExprRef> slices;
            for (size_t i = 0; i < 8; i++) {
                slices.push_back(sme.GetHorizontalSlice(sme.za, 1, i, HALF));
            }
            EXPECT_TRUE(TO_STR(slices[0], 1, u, mdl) == "#x0000000000000000fffd000000000000");
            EXPECT_TRUE(TO_STR(slices[1], 1, u, mdl) == "#x0000000000000000fffc000000000000");
            EXPECT_TRUE(TO_STR(slices[2], 1, u, mdl) == "#x0000000000000000fffb000000000000");
            EXPECT_TRUE(TO_STR(slices[3], 1, u, mdl) == "#x0000000000000000fffa000000000000");
            EXPECT_TRUE(TO_STR(slices[4], 1, u, mdl) == "#x0000000000000000fff9000000000000");
            EXPECT_TRUE(TO_STR(slices[5], 1, u, mdl) == "#x0000000000000000fff8000000000000");
            EXPECT_TRUE(TO_STR(slices[6], 1, u, mdl) == "#x0000000000000000fff7000000000000");
            EXPECT_TRUE(TO_STR(slices[7], 1, u, mdl) == "#x0000000000000000fff6000000000000");
        }
    );

    CHECK("LD1.D loads to ZA5V.D[1] from DRAM address 14 with predicate", sme, {"LD1.D"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            // tile
            cstr_step_bv(s, u, ctx, sme.ZAt, 5ULL, sme.ZAt.bit_width()); // tile 5
            // slice
            cstr_step_bool(s, u, ctx, sme.HV, true); // vertical
            cstr_step_bv(s, u, ctx, sme.Rs, 2ULL, sme.Rs.bit_width());
            cstr_step_bv(s, u, ctx, sme.Get32BitGPR(2), 0ULL, 32); // W[2] = 0
            cstr_step_bv(s, u, ctx, sme.Imm4, 1ULL, sme.Imm4.bit_width()); // slice 1 (left)
            // predicates
            cstr_step_bv(s, u, ctx, sme.Pg, 1ULL, sme.Pg.bit_width());
            cstr_step_bv(s, u, ctx, sme.p_regs[1], 0x0100ULL, P_REG_WIDTH); // predicate
            // base & offset
            cstr_step_bv(s, u, ctx, sme.Rn, 3ULL, sme.Rn.bit_width()); // X[3]
            cstr_step_bv(s, u, ctx, sme.GPRs[3], 14ULL, 64); // base = 14
            cstr_step_bv(s, u, ctx, sme.Rm, 31ULL, sme.Rm.bit_width()); // XZR
            // DRAM
            auto mbytes = DOUBLE / BYTE;
            for (size_t i = 0; i < 2; i++) {
                cstr_step(s, u, ctx, sme.DRAM_GetElementBytes(14 + i * mbytes, mbytes), ctx.bv_val(-1, DOUBLE));
            }
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            std::cout << " vertical slice filled\n";
            auto top = sme.GetHorizontalSlice(sme.za, 5, 0, DOUBLE);
            auto bot = sme.GetHorizontalSlice(sme.za, 5, 1, DOUBLE);
            PrintZa(mdl ,u, sme, 1);
            PRINT(top, 1, u, mdl, "top @ 1");
            PRINT(bot, 1, u, mdl, "bottom @ 1");
            EXPECT_TRUE(TO_STR(top, 1, u, mdl) == "#x00000000000000000000000000000000");
            EXPECT_TRUE(TO_STR(bot, 1, u, mdl) == "#xffffffffffffffff0000000000000000");
        }
    );

    // ASK: stack pointer must be aligned, SP % 16 == 0
}
