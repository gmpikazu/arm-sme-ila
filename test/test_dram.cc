#include "../include/test_helpers.h"
#include "../include/arm.h"
#include <string>

using namespace ilang;
using namespace arm;

void test_store(ArmSme& sme_DramLE, ArmSme& sme_DramBE) { 
    // TODO: STR
    #define sme sme_DramBE
    CHECK("ST1.H stores ZA1V.H[3] to DRAM (BE) with offset 1", sme, {"ST1.H"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            // tile
            cstr_step_bv(s, u, ctx, sme.ZAt, 1ULL, sme.ZAt.bit_width()); // tile 1
            Tracker t;
            track_slice(t, bv_val_128(ctx, 0x0011223344556677ULL, 0x8899AABBCCDDEEFFULL), 1, 3, true, HALF);
            cstr_all_tracked_and_zero(s, u, ctx, t, sme);
            // slice
            cstr_step_bool(s, u, ctx, sme.HV, true); // vertical
            cstr_step_bv(s, u, ctx, sme.Rs, 2ULL, sme.Rs.bit_width());
            cstr_step_bv(s, u, ctx, sme.Get32BitGPR(2), 0ULL, 32); // W[2] = 0
            cstr_step_bv(s, u, ctx, sme.Imm3, 3ULL, sme.Imm3.bit_width()); // slice 3
            // predicates
            cstr_step_bv(s, u, ctx, sme.Pg, 1ULL, sme.Pg.bit_width());
            cstr_step_bv(s, u, ctx, sme.p_regs[1], 0x5555ULL, P_REG_WIDTH); // all active
            // base & offset
            cstr_step_bv(s, u, ctx, sme.Rn, 31ULL, sme.Rn.bit_width()); // base = SP
            cstr_step_bv(s, u, ctx, sme.SP, 0ULL, 64); // value of SP
            cstr_step_bv(s, u, ctx, sme.Rm, 30ULL, sme.Rm.bit_width()); // X[30]
            cstr_step_bv(s, u, ctx, sme.GPRs[30], 1ULL, 64); // offset = 1 (starts at index 1 from base)
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            // NOTE: SP alignment fault only happens when IsAnyActivePredicate == false
            std::cout << " vertical slice filled\n";
            PrintZa(mdl, u, sme, 0);
            auto slice = sme.GetVerticalSlice(sme.za, 1, 3, HALF);
            PRINT(slice, 0, u, mdl, "Vertical Slice");
            PrintDRAM(mdl, u, sme, 0, 1, 3*SVL_B);
            auto dram_vec_za_endian = sme.DRAM_GetVectorAsZaEndian(2, HALF, SVL, true);
            auto wb_vec = sme.WB_svl_vector;
            auto wb_addr = sme.WB_base_addr;
            PRINT(wb_addr, 1, u, mdl, "WB addr");
            std::cout << " these two are HALF-swapped\n";
            PRINT(dram_vec_za_endian, 1, u, mdl, "DRAM vector");
            PRINT(wb_vec, 1, u, mdl, "WB vector");
            EXPECT_TRUE(TO_STR(dram_vec_za_endian, 1, u, mdl) == "#x00112233445566778899aabbccddeeff");
            EXPECT_TRUE(TO_STR(dram_vec_za_endian, 1, u, mdl) == TO_STR(slice, 1, u, mdl)); // invariant
        }
    );
    #undef sme
}

// NOTE: base (either SP or X reg) needs to be 16-byte aligned or else faults
// for LDR, offset will never cause misalignment because it is a multiple of 16
void test_load(ArmSme& sme_DramLE, ArmSme& sme_DramBE) { 
    #define sme sme_DramBE // doesn't matter for LDR
    CHECK("LDR to ZA[3] (equivalent to ZA0H[3]) starting at an offset", sme, {"LDR"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            cstr_step_bv(s, u, ctx, sme.Rv, 12ULL, sme.Rv.bit_width()); // W[12]
            cstr_step_bv(s, u, ctx, sme.Get32BitGPR(12), 2ULL, 32); // W[12] = 2
            cstr_step_bv(s, u, ctx, sme.Rn, 31ULL, sme.Rn.bit_width()); // base = SP
            cstr_step_bv(s, u, ctx, sme.Get64BitGPR(31, true), 0ULL, 64); // SP=0
            // LDR starts at (base + Imm4*SVL_B) and row_index = W[12]+Imm4
            cstr_step_bv(s, u, ctx, sme.Imm4, 1ULL, sme.Imm4.bit_width()); // Imm4=1
            for (size_t i = 0; i < 3*SVL_B; i++) {
                cstr_step(s, u, ctx, sme.DRAM_GetByteNoEndian(i), ctx.bv_val(i, BYTE));
            }
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            auto base = sme.Get64BitGPR(31, true);
            EXPECT_TRUE(TO_STR(base, 0, u, mdl) == TO_STR(sme.SP, 0, u, mdl)); // base must be SP
            PrintDRAM(mdl, u, sme, 0, 0, 3*SVL_B);
            // PrintDRAM(mdl, u, sme, SVL_B, 0, SVL_B);
            // PrintDRAM(mdl, u, sme, 2*SVL_B, 0, SVL_B);
            
            std::cout << " row 3 has been filled\n";
            PrintZa(mdl, u, sme, 1);
            auto slice = sme.GetHorizontalSlice(sme.za, 0, 3, BYTE);
            EXPECT_TRUE(TO_STR(slice, 1, u, mdl) == "#x1f1e1d1c1b1a19181716151413121110");
        }
    );
    #undef sme

    #define sme sme_DramLE // NOTE: controls endiannesss
    CHECK("LD1.H loads into ZA1V.H[3] from DRAM (LE) starting at an offset", sme, {"LD1.H"},
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
            cstr_step_bv(s, u, ctx, sme.Rn, 31ULL, sme.Rn.bit_width()); // base = SP
            // NOTE: SP alignment fault only happens when IsAnyActivePredicate == false
            cstr_step_bv(s, u, ctx, sme.SP, 0ULL, 64); // value of SP
            cstr_step_bv(s, u, ctx, sme.Rm, 30ULL, sme.Rm.bit_width()); // X[30]
            cstr_step_bv(s, u, ctx, sme.GPRs[30], 3ULL, 64); // offset = 3 (index 3 starting from SP)
            // DRAM
            auto mbytes = HALF / BYTE;
            for (size_t i = 0; i < 16; i++) {
                cstr_step(s, u, ctx, sme.DRAM_GetElementAsZaEndian(2 + i * mbytes, HALF, true), ctx.bv_val(-1-i, HALF));
            }
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            std::cout << " vertical slice filled except one\n";
            PRINT(sme.DRAM_GetElementAsZaEndian(2, HALF, true), 0, u, mdl);
            PRINT(sme.DRAM_GetElementAsZaEndian(4, HALF, true), 0, u, mdl);
            PRINT(sme.DRAM_GetElementAsZaEndian(6, HALF, true), 0, u, mdl);
            PRINT(sme.DRAM_GetVectorAsZaEndian(6, HALF, SVL, true), 1, u, mdl, "DRAM VECTOR");
            PRINT(sme.GetVerticalSlice(sme.za, 1, 3, HALF), 1, u, mdl, "SLICE");
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

            std::cout << " check endianness of DRAM\n";
            PrintDRAM(mdl, u, sme, 0, 0, SVL_B);
            // 0xfffd stored in reverse-byte-order
            EXPECT_TRUE(TO_STR(sme.DRAM_GetByteNoEndian(6), 1, u, mdl) == "#xfd");
            EXPECT_TRUE(TO_STR(sme.DRAM_GetByteNoEndian(7), 1, u, mdl) == "#xff");
        }
    );
    #undef sme

    #define sme sme_DramLE // NOTE: controls endiannesss
    CHECK("LD1.D loads to ZA5V.D[1] from DRAM (LE) address 14 with all predicates", sme, {"LD1.D"},
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
            cstr_step_bv(s, u, ctx, sme.p_regs[1], 0x0101ULL, P_REG_WIDTH); // predicate
            // base & offset
            cstr_step_bv(s, u, ctx, sme.Rn, 3ULL, sme.Rn.bit_width()); // X[3]
            cstr_step_bv(s, u, ctx, sme.GPRs[3], 14ULL, 64); // base = 14
            cstr_step_bv(s, u, ctx, sme.Rm, 31ULL, sme.Rm.bit_width()); // XZR
            // DRAM
            for (size_t i = 0; i < 16; i++) {
                cstr_step(s, u, ctx, sme.DRAM_GetByteNoEndian(14+i), ctx.bv_val(i, BYTE));
            }
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            std::cout << " vertical slice filled\n";
            auto top = sme.GetHorizontalSlice(sme.za, 5, 0, DOUBLE);
            auto bot = sme.GetHorizontalSlice(sme.za, 5, 1, DOUBLE);
            PrintZa(mdl ,u, sme, 1);
            PRINT(top, 1, u, mdl, "top @ 1");
            PRINT(bot, 1, u, mdl, "bottom @ 1");
            EXPECT_TRUE(TO_STR(top, 1, u, mdl) == "#x07060504030201000000000000000000");
            EXPECT_TRUE(TO_STR(bot, 1, u, mdl) == "#x0f0e0d0c0b0a09080000000000000000");

            // NOTE: if ZA and DRAM differs in endianness, DRAM_GetVectorAsZaEndian returns the exact same SVL bits regardless of element_size_bits because the SVL bit vector rotates 180 degrees
            // when ZA and DRAM shares the same endianness, then the return vector is not merely a mirror
            std::cout << " INVARIANT: checking DRAM_GetElement and DRAM_GetVector helpers (must equal)\n";
            auto dram_vector_as_za_endian = sme.DRAM_GetVectorAsZaEndian(14, DOUBLE, SVL, true);
            auto ver_slice = sme.GetVerticalSlice(sme.za, 5, 1, DOUBLE);
            PRINT(dram_vector_as_za_endian, 1, u, mdl, "DRAM");
            PRINT(ver_slice, 1, u, mdl, "slice");
            EXPECT_TRUE(TO_STR(dram_vector_as_za_endian, 1, u, mdl) == TO_STR(ver_slice, 1, u, mdl));
        }
    );
    #undef sme
}
