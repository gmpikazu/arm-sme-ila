#include "../include/arm.h"
#include "../include/test_helpers.h"

using namespace arm;
using namespace ilang;

// NOTE: scratchpad to test new verification ideas
void test_quick(ArmSme& sme) {
    CHECK("small proof", sme, {"ST1.H", "ZERO"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            // tile
            cstr_step_bv(s, u, ctx, sme.ZAt, 1ULL, sme.ZAt.bit_width()); // tile 1
            Tracker t;
            track_slice(t, bv_val_128(ctx, 0x0011223344556677ULL, 0x8899AABBCCDDEEFFULL), 1, 3, true, HALF, sme);
            cstr_all_tracked_and_zero(s, u, ctx, t, sme);
            // slice
            cstr_step_bool(s, u, ctx, sme.HV, true); // vertical
            cstr_step_bv(s, u, ctx, sme.Rs, 2ULL, sme.Rs.bit_width());
            cstr_step_bv(s, u, ctx, sme.Get32BitGPR(2), 0ULL, 32); // W[2] = 0
            cstr_step_bv(s, u, ctx, sme.Imm3, 3ULL, sme.Imm3.bit_width()); // slice 3
            // predicates
            cstr_step_bv(s, u, ctx, sme.Pg, 1ULL, sme.Pg.bit_width());
            cstr_step_bv(s, u, ctx, sme.p_regs[1], 0x5555ULL, sme.P_REG_WIDTH); // all active
            // base & offset
            cstr_step_bv(s, u, ctx, sme.Rn, 31ULL, sme.Rn.bit_width()); // base = SP
            cstr_step_bv(s, u, ctx, sme.SP, 0ULL, 64); // value of SP
            cstr_step_bv(s, u, ctx, sme.Rm, 30ULL, sme.Rm.bit_width()); // X[30]
            cstr_step_bv(s, u, ctx, sme.GPRs[30], 1ULL, 64); // offset = 1 (starts at index 1 from base)

            // next step
            cstr_step_ila(s, u, ctx, sme.DRAM_UF(BvConst(10, sme.DRAM_ADDR_WIDTH)), 1, Extract(sme.WB_svl_vector, 15, 8), 1);
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            std::cout << " vertical slice filled\n";
            PrintZa(mdl, u, sme, 0);
            auto slice = sme.GetVerticalSlice(sme.za, 1, 3, HALF);
            PRINT(slice, 0, u, mdl, "Vertical Slice");
            PrintDRAM(mdl, u, sme, 0, 1, 3*sme.SVL_B);
            auto dram_vec_za_endian = sme.DRAM_GetVectorAsZaEndian(2, HALF, sme.SVL, true);
            auto wb_vec = sme.WB_svl_vector;
            auto wb_addr = sme.WB_base_addr;
            PRINT(wb_addr, 1, u, mdl, "WB addr");
            std::cout << " these two are HALF-swapped\n";
            PRINT(dram_vec_za_endian, 1, u, mdl, "DRAM vector");
            PRINT(wb_vec, 1, u, mdl, "WB vector");
            EXPECT_TRUE(TO_STR(dram_vec_za_endian, 1, u, mdl) == "#x00112233445566778899aabbccddeeff");
            EXPECT_TRUE(TO_STR(dram_vec_za_endian, 1, u, mdl) == TO_STR(slice, 1, u, mdl)); // invariant

            // next step
            std::cout << " =========== NEXT STEP ============= " << std::endl;
            PrintZa(mdl, u, sme, 1);
            PRINT(sme.DRAM_GetByteNoEndian(10), 1, u, mdl, "BYTE read, from prev store");
        }
    );
}
