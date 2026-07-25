#include "../include/test_helpers.h"
#include "../include/arm.h"

using namespace ilang;
using namespace arm;

void test_slice_helper(ArmSme& sme) {
    CHECK("GetHorizontalSlice constrains underlying ZA memory bytes", sme, {"ZERO"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            std::cout << "TIP: we use GetHorizontalSlice() to constrain underlying ZA memory bytes\n";
            cstr_step_bv(s, u, ctx, sme.Imm8, 0xFFULL, 8);
            auto hor_slice = sme.GetHorizontalSlice(sme.za, 0, 1, BYTE); // row 1
            cstr_step(s, u, ctx, hor_slice, bv_val_128(ctx, 0x0F0E0D0C0B0A0908ULL, 0x0706050403020100ULL), 0);
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            std::cout << " row 1 will be populated at step 0\n";
            PrintZaCsv(mdl, u, sme, 0);
            for (size_t col = 0; col < SVL_B; col++) {
                // match the bytes of row 1 with the ones we assigned
                std::string byte_val = mdl.eval(u.GetZ3Expr(GetByteAtRowCol(sme, 1, col), 0)).to_string();
                uint8_t expected_byte = 0x0F - col; // Big-endian: col 0 = MSB
                char buf[5];
                snprintf(buf, sizeof(buf), "#x%02x", expected_byte); // writes byte representation of uint8_t
                EXPECT_TRUE(byte_val == buf);
            }
            std::cout << " row 1 will be zeroed out at step 1\n";
            PrintZaCsv(mdl, u, sme, 1);
            for (size_t col = 0; col < SVL_B; col++) {
                std::string byte_val = mdl.eval(u.GetZ3Expr(GetByteAtRowCol(sme, 1, col), 1)).to_string();
                EXPECT_TRUE(byte_val == "#x00");
            }
        }
    );

    CHECK("GetVerticalSlice constrains underlying ZA memory bytes", sme, {"ZERO"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            cstr_step_bv(s, u, ctx, sme.Imm8, 0x00ULL, 8); // no zeroing happens
            auto vert_slice = sme.GetVerticalSlice(sme.za, 0, 0, BYTE);
            cstr_step(s, u, ctx, vert_slice, bv_val_128(ctx, 0x0F0E0D0C0B0A0908ULL, 0x0706050403020100ULL), 0);
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            std::cout << " rightmost vertical slice populated\n";
            PrintZaCsv(mdl, u, sme, 0);
            std::cout << " no zeroing happens because we set Imm8 = 0x00\n";
            PrintZaCsv(mdl, u, sme, 1);
            auto vert_slice = sme.GetVerticalSlice(sme.za, 0, 0, BYTE);
            std::string val = mdl.eval(u.GetZ3Expr(vert_slice, 1)).to_string();
            EXPECT_TRUE(val == "#x0f0e0d0c0b0a09080706050403020100");
        }
    );

    CHECK("Horizontal slices ZA0H.B[1], ZA1H.H[0] have equal bytes", sme, {"ZERO"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            cstr_step_bv(s, u, ctx, sme.Imm8, 0x00ULL, 8); // make ZERO not zero out anything
            auto hor_slice = sme.GetHorizontalSlice(sme.za, 0, 1, BYTE);
            cstr_step(s, u, ctx, hor_slice, bv_val_128(ctx, 0x0F0E0D0C0B0A0908ULL, 0x0706050403020100ULL), 0);
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            auto hor_slice_za0hb = u.GetZ3Expr(sme.GetHorizontalSlice(sme.za, 0, 1, BYTE), 1);
            auto hor_slice_za1hh = u.GetZ3Expr(sme.GetHorizontalSlice(sme.za, 1, 0, HALF), 1);
            EXPECT_TRUE(mdl.eval(hor_slice_za0hb).to_string() == mdl.eval(hor_slice_za1hh).to_string());
            EXPECT_TRUE(mdl.eval(hor_slice_za0hb).to_string() == "#x0f0e0d0c0b0a09080706050403020100");
        }
    );
}

void test_mova(ArmSme& sme) {
    // TODO: Implement real MOVA tests:
    // 1. Constrain ZA tile slices at step 0
    // 2. Execute MOVA instruction
    // 3. Verify the output at step 1
    // Example: MOVA Za0h.S[0], p0/m = move first row of Za0 tile to Z0
    // Check with PrintZaCsv(mdl, u, sme, 1) to see post-instruction state
    
    CHECK("MOVA_T2V.S vertical tile to vector register", sme, {"MOVA_T2V.S"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            auto ver_slice = sme.GetVerticalSlice(sme.za, 2, 1, WORD);
            cstr_step(s, u, ctx, ver_slice, bv_val(ctx, {0xffffffffffffffffULL, 0xffffffffffffffffULL}));
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            auto ver_slice = sme.GetVerticalSlice(sme.za, 2, 1, WORD);
            PRINT(ver_slice, 0, u, mdl, "vertical slice");
            PrintZaCsv(mdl, u, sme, 0);
        }
    );
}

void test_zero(ArmSme &sme) {
    CHECK("ZERO Imm8=0xFF zeroes specific byte that was initialized to non-zero", sme, {"ZERO"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            cstr_step_bv(s, u, ctx, sme.Imm8, 0xFFULL, 8);
            auto target_byte = Load(sme.za, BvConst(0, sme.za.addr_width()));
            cstr_step_bv(s, u, ctx, target_byte, 0xAA, BYTE);
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            auto target_byte = Load(sme.za, BvConst(0, sme.za.addr_width()));
            auto tb_step0 = u.GetZ3Expr(target_byte, 0);
            auto tb_step1 = u.GetZ3Expr(target_byte, 1);
            EXPECT_TRUE(mdl.eval(tb_step0).to_string() == "#xaa");
            EXPECT_TRUE(mdl.eval(tb_step1).to_string() == "#x00");
        }
    );

    CHECK("ZERO Imm8=0xFF zeroes entire ZA array that was initialized to non-zero", sme, {"ZERO"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            cstr_step_bv(s, u, ctx, sme.Imm8, 0xFFULL, 8);
            for (size_t addr = 0; addr < ZA_BYTE_SIZE; addr++) {
                auto ila_byte = Load(sme.za, BvConst(addr, sme.za.addr_width()));
                cstr_step_bv(s, u, ctx, ila_byte, 0xAA, BYTE, 0);
            }
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            for (size_t addr = 0; addr < ZA_BYTE_SIZE; addr++) {
                auto ila_byte = Load(sme.za, BvConst(addr, sme.za.addr_width()));
                auto b_step0 = u.GetZ3Expr(ila_byte, 0);
                auto b_step1 = u.GetZ3Expr(ila_byte, 1);
                EXPECT_TRUE(mdl.eval(b_step0).to_string() == "#xaa");
                EXPECT_TRUE(mdl.eval(b_step1).to_string() == "#x00");
            }
        }
    );

    CHECK("ZERO Imm8=0x55 zeroes 16-bit element tile ZA0.H which was previously non-zero", sme, {"ZERO"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            cstr_step_bv(s, u, ctx, sme.Imm8, 0x55ULL, 8);
            for (size_t addr = 0; addr < ZA_BYTE_SIZE; addr++) {
                auto ila_byte = Load(sme.za, BvConst(addr, sme.za.addr_width()));
                cstr_step_bv(s, u, ctx, ila_byte, 0xFF, BYTE, 0);
            }
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            std::cout << " everything set to 0xff initially\n";
            PrintZaCsv(mdl, u, sme, 0);
            std::cout << " now all ZA0.H bytes are zeroed\n";
            PrintZaCsv(mdl, u, sme, 1);
            size_t dim = SVL_B / HALF; // since we're working with .H suffix
            for (size_t row = 0; row < dim; row++) {
                auto ila_row = sme.GetHorizontalSlice(sme.za, 0, row, HALF);
                std::string val = mdl.eval(u.GetZ3Expr(ila_row, 1)).to_string();
                EXPECT_TRUE(val == "#x00000000000000000000000000000000");
            }
        }
    );    

    CHECK("ZERO Imm8=0x84 zeroes 32-bit element tile ZA3.S which was previously non-zero", sme, {"ZERO"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            cstr_step_bv(s, u, ctx, sme.Imm8, 0x84ULL, 8);
            for (size_t addr = 0; addr < ZA_BYTE_SIZE; addr++) {
                auto ila_byte = Load(sme.za, BvConst(addr, sme.za.addr_width()));
                cstr_step_bv(s, u, ctx, ila_byte, 0xFF, BYTE, 0);
            }
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            std::cout << " everything set to 0xff initially\n";
            PrintZaCsv(mdl, u, sme, 0);
            std::cout << " now all ZA3.S bytes are zeroed\n";
            PrintZaCsv(mdl, u, sme, 1);
            size_t dim = SVL_B / WORD; // since it's .S suffix
            for (size_t row = 0; row < dim; row++) {
                auto ila_row = sme.GetHorizontalSlice(sme.za, 0, row, WORD);
                std::string val = mdl.eval(u.GetZ3Expr(ila_row, 1)).to_string();
                EXPECT_TRUE(val == "#x00000000000000000000000000000000");
            }
        }
    );

}
