#include "../include/test_helpers.h"
#include "../include/arm.h"
#include "config.h" // NOTE: for USE_DRAM_MEMSTATE macro, PrintDram only works for MemState

using namespace ilang;
using namespace arm;

// BUG: not working, since UF step 0 and step 1 is same node
// maybe can use array of UFs for each step, std::vector<FuncRef> arr
// so arr[0] represents UF step 0, arr[1] represents UF step 1
namespace DRAM_Rough_Helper {

typedef std::unordered_map<size_t, ExprRef> DramTracker;

static void update_tracker(DramTracker& t, size_t base_addr, const ExprRef svl_vector, ArmSme& sme) {
    assert(svl_vector.bit_width() == sme.SVL);
    auto GetVecByteLSB = [&](size_t idx) -> ExprRef {
        // get element of vector from LSB
        size_t rightmost = BYTE * idx;
        size_t leftmost = rightmost + BYTE - 1;
        return Extract(svl_vector, leftmost, rightmost);
    };
    auto GetVecByteMSB = [&](size_t idx) -> ExprRef {
        // get element of vector from MSB
        size_t mirrored_idx = sme.SVL_B - 1 - idx;
        return GetVecByteLSB(mirrored_idx);
    };

    for (size_t i = 0; i < sme.SVL_B; i++) {
        auto byte = GetVecByteMSB(i);
        t.insert_or_assign(base_addr+i, byte);
    }
}

// modifies the reference to FuncRef& DRAM_UF
// write values are available at THE SAME STEP where we need to read it (apply step to both ila_expr)
static void cstr_tracked_dram(IlaZ3Unroller& u, z3::solver& s, z3::context& ctx,
                              FuncRef& DRAM_UF, const DramTracker& t, int step, ArmSme& sme)
{
    assert(step > 0);
    for (auto& [addr, ila_expr] : t) {
        cstr_step_ila(s, u, ctx, DRAM_UF(BvConst(addr, sme.DRAM_ADDR_WIDTH)), step, ila_expr, step);
    }
}

} // namespace DRAM_Helper

void test_uf_dram(ArmSme& sme_DramLE, ArmSme& sme_DramBE) {
    // TODO: quick testing UF
    #define sme sme_DramBE
    CHECK("UF testing", sme, {"ST1.H"},
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

            // NOTE: weird constraints
            cstr_step(s, u, ctx, sme.DRAM_UF(BvConst(1, sme.DRAM_ADDR_WIDTH)), ctx.bv_val(1, BYTE), 0);
            cstr_step(s, u, ctx, sme.DRAM_UF(BvConst(1, sme.DRAM_ADDR_WIDTH)), ctx.bv_val(2, BYTE), 1);
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            // NOTE: SP alignment fault only happens when IsAnyActivePredicate == false
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
        }
    );
    #undef sme
}

void test_store(ArmSme& sme_DramLE, ArmSme& sme_DramBE) {
    #define sme sme_DramBE // doesn't matter for STR
    CHECK("STR from ZA[5] (equivalent to ZA0H[5]) to DRAM (BE) with offset", sme, {"STR"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            cstr_step_bv(s, u, ctx, sme.Rv, 12ULL, sme.Rv.bit_width()); // W[12]
            cstr_step_bv(s, u, ctx, sme.Get32BitGPR(12), 3ULL, 32); // W[12] = 3
            cstr_step_bv(s, u, ctx, sme.Rn, 31ULL, sme.Rn.bit_width()); // base = SP
            cstr_step_bv(s, u, ctx, sme.Get64BitGPR(31, true), 16ULL, 64); // SP=16
            // STR starts at (base + Imm4*SVL_B) and row_index = W[12]+Imm4
            cstr_step_bv(s, u, ctx, sme.Imm4, 2ULL, sme.Imm4.bit_width()); // Imm4=2
            Tracker t;
            track_slice(t, bv_val_128(ctx, 0x0011223344556677ULL, 0x8899AABBCCDDEEFFULL), 0, 5, false, BYTE, sme);
            cstr_all_tracked_and_zero(s, u, ctx, t, sme);
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            auto base = sme.Get64BitGPR(31, true);
            EXPECT_TRUE(TO_STR(base, 0, u, mdl) == TO_STR(sme.SP, 0, u, mdl)); // base must be SP
            std::cout << " row 5 (ZA0H.B[5]) initialized\n";
            PrintZa(mdl, u, sme, 0);

        #if USE_DRAM_MEMSTATE
            std::cout << " horizontal row 2 starting at address 16 now filled\n";
            PrintDRAM(mdl, u, sme, 16, 1, 4*sme.SVL_B);
            auto dram_slice = sme.DRAM_GetVectorAsZaEndian(48, BYTE, sme.SVL, false); // start at addr 48
            auto hor_slice = sme.GetHorizontalSlice(sme.za, 0, 5, BYTE);
            std::cout << " these two are BYTE-swapped\n";
            PRINT(dram_slice, 1, u, mdl, "DRAM slice as ZA endian");
            PRINT(sme.WB_svl_vector, 1, u, mdl, "WB vector");
            EXPECT_TRUE(TO_STR(dram_slice, 1, u, mdl) == "#x00112233445566778899aabbccddeeff");
            EXPECT_TRUE(TO_STR(dram_slice, 1, u, mdl) == TO_STR(hor_slice, 1, u, mdl));

            std::cout << " WB vector was written to address 0x30 (48 in decimal)\n";
            PRINT(sme.WB_svl_vector, 1, u, mdl, "WB vector");
            EXPECT_TRUE(TO_STR(sme.WB_base_addr, 1, u, mdl) == "#x00000000000000000000000000000030");
            EXPECT_TRUE(TO_STR(sme.WB_svl_vector, 1, u, mdl) == "#xffeeddccbbaa99887766554433221100");
        #else
            std::cout << " WB vector was written to address 0x30 (48 in decimal)\n";
            PRINT(sme.WB_svl_vector, 1, u, mdl, "WB vector");
            EXPECT_TRUE(TO_STR(sme.WB_base_addr, 1, u, mdl) == "#x00000000000000000000000000000030");
            EXPECT_TRUE(TO_STR(sme.WB_svl_vector, 1, u, mdl) == "#xffeeddccbbaa99887766554433221100");
        #endif
        }
    );
    #undef sme

    #define sme sme_DramBE
    CHECK("ST1.H stores ZA1V.H[3] to DRAM (BE) with offset 1", sme, {"ST1.H"},
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
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            // NOTE: SP alignment fault only happens when IsAnyActivePredicate == false
            std::cout << " vertical slice initialized\n";
            PrintZa(mdl, u, sme, 0);
            auto slice = sme.GetVerticalSlice(sme.za, 1, 3, HALF);
            PRINT(slice, 0, u, mdl, "Vertical Slice");

        #if USE_DRAM_MEMSTATE
            auto wb_vec = sme.WB_svl_vector;
            auto wb_addr = sme.WB_base_addr;
            PRINT(wb_addr, 1, u, mdl, "WB addr");
            EXPECT_TRUE(TO_STR(wb_addr, 1, u, mdl) == "#x00000000000000000000000000000002");

            PrintDRAM(mdl, u, sme, 0, 1, 3*sme.SVL_B);
            auto dram_vec_za_endian = sme.DRAM_GetVectorAsZaEndian(2, HALF, sme.SVL, true);

            std::cout << " these two are HALF-swapped\n";
            PRINT(dram_vec_za_endian, 1, u, mdl, "DRAM vector");
            PRINT(wb_vec, 1, u, mdl, "WB vector");
            EXPECT_TRUE(TO_STR(dram_vec_za_endian, 1, u, mdl) == "#x00112233445566778899aabbccddeeff");
            EXPECT_TRUE(TO_STR(dram_vec_za_endian, 1, u, mdl) == TO_STR(slice, 1, u, mdl)); // invariant
            EXPECT_TRUE(TO_STR(wb_vec, 1, u, mdl) == "#xeeffccddaabb88996677445522330011");
        #else
            auto wb_vec = sme.WB_svl_vector;
            auto wb_addr = sme.WB_base_addr;
            PRINT(wb_addr, 1, u, mdl, "WB addr");
            EXPECT_TRUE(TO_STR(wb_addr, 1, u, mdl) == "#x00000000000000000000000000000002");
            EXPECT_TRUE(TO_STR(wb_vec, 1, u, mdl) == "#xeeffccddaabb88996677445522330011");
        #endif
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
            for (size_t i = 0; i < 3*sme.SVL_B; i++) {
                cstr_step(s, u, ctx, sme.DRAM_GetByteNoEndian(i), ctx.bv_val(i, BYTE));
            }
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            auto base = sme.Get64BitGPR(31, true);
            EXPECT_TRUE(TO_STR(base, 0, u, mdl) == TO_STR(sme.SP, 0, u, mdl)); // base must be SP
            std::cout << " initialize a monotonic sequence in DRAM\n";
            PrintDRAM(mdl, u, sme, 0, 0, 3*sme.SVL_B);
            // PrintDRAM(mdl, u, sme, sme.SVL_B, 0, sme.SVL_B);
            // PrintDRAM(mdl, u, sme, 2*sme.SVL_B, 0, sme.SVL_B);

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
            cstr_step_bv(s, u, ctx, sme.p_regs[1], 0x5555ULL, sme.P_REG_WIDTH); // all active
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
            std::cout << " check endianness of DRAM\n";
            PrintDRAM(mdl, u, sme, 0, 0, 3*sme.SVL_B);
            // 0xfffd stored in reverse-byte-order
            EXPECT_TRUE(TO_STR(sme.DRAM_GetByteNoEndian(6), 1, u, mdl) == "#xfd");
            EXPECT_TRUE(TO_STR(sme.DRAM_GetByteNoEndian(7), 1, u, mdl) == "#xff");

            std::cout << " vertical slice filled\n";
            PRINT(sme.DRAM_GetVectorAsZaEndian(6, HALF, sme.SVL, true), 1, u, mdl, "DRAM VECTOR");
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
            cstr_step_bv(s, u, ctx, sme.p_regs[1], 0x0101ULL, sme.P_REG_WIDTH); // predicate
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
            auto dram_vector_as_za_endian = sme.DRAM_GetVectorAsZaEndian(14, DOUBLE, sme.SVL, true);
            auto ver_slice = sme.GetVerticalSlice(sme.za, 5, 1, DOUBLE);
            PRINT(dram_vector_as_za_endian, 1, u, mdl, "DRAM");
            PRINT(ver_slice, 1, u, mdl, "slice");
            EXPECT_TRUE(TO_STR(dram_vector_as_za_endian, 1, u, mdl) == TO_STR(ver_slice, 1, u, mdl));
        }
    );
    #undef sme
}
