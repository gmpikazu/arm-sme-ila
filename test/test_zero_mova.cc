#include "../include/test_helpers.h"
#include "../include/arm.h"

using namespace ilang;
using namespace arm;

void test_cstr_helper(ArmSme& sme) {
    CHECK("SHOWCASE: track_slice() + cstr_all_tracked_and_zero() idiom", sme, {"ZERO"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            Tracker t;
            // each new layer is applied on top of previously applied layer
            track_slice(t, bv_val_128(ctx, 0x0001020304050607ULL, 0x08090A0B0C0D0E0F), 0, 0, false, BYTE);
            track_slice(t, bv_val_128(ctx, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFF), 0, 2, true, BYTE);
            track_slice(t, bv_val_128(ctx, 0x0001020304050607ULL, 0x08090A0B0C0D0E0F), 7, 0, false, BYTE);
            cstr_all_tracked_and_zero(s, u, ctx, t, sme);
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            PrintZa(mdl, u, sme, 0);
        }
    );

    CHECK("constrain horizontal slice with BYTE", sme, {"ZERO"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            cstr_step_bv(s, u, ctx, sme.Imm8, 0x00ULL, 8); // make sure ZERO doesn't trigger
            cstr_step_slice(s, u, ctx, sme, bv_val_128(ctx, 0xAAAAAAAAAAAAAAAALL, 0x5555555555555555ULL), 0, 0, false, BYTE, 0);
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            auto hor_slice = sme.GetHorizontalSlice(sme.za, 0, 0, BYTE);
            auto hor_slice_val = mdl.eval(u.GetZ3Expr(hor_slice, 0), false).to_string();
            std::cout << " cstr_step_slice() constrains horizontal row\n";
            PrintZa(mdl, u, sme, 0);
            std::cout << " hor_slice step 0 = " << hor_slice_val << std::endl;
        }
    );

    CHECK("constrain vertical slice with WORD", sme, {"ZERO"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            cstr_step_bv(s, u, ctx, sme.Imm8, 0x00ULL, 8); // no ZERO
            cstr_step_slice(s, u, ctx, sme, bv_val_128(ctx, 0x4444444433333333ULL, 0x2222222211111111ULL), 0, 0, true, WORD, 0);
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            auto ver_slice = sme.GetVerticalSlice(sme.za, 0, 0, WORD);
            auto ver_slice_val = mdl.eval(u.GetZ3Expr(ver_slice, 0), false).to_string();
            std::cout << " cstr_step_slice() populates a WORD at every other row\n";
            PrintZa(mdl, u, sme, 0);
            std::cout << " ver_slice step 0 = " << ver_slice_val << std::endl;
        }
    );
}

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
            PrintZa(mdl, u, sme, 0);
            for (size_t col = 0; col < SVL_B; col++) {
                // match the bytes of row 1 with the ones we assigned
                std::string byte_val = mdl.eval(u.GetZ3Expr(GetByteAtRowCol(sme, 1, col), 0)).to_string();
                uint8_t expected_byte = 0x0F - col; // Big-endian: col 0 = MSB
                char buf[5];
                snprintf(buf, sizeof(buf), "#x%02x", expected_byte); // writes byte representation of uint8_t
                EXPECT_TRUE(byte_val == buf);
            }
            std::cout << " row 1 will be zeroed out at step 1\n";
            PrintZa(mdl, u, sme, 1);
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
            PrintZa(mdl, u, sme, 0);
            std::cout << " no zeroing happens because we set Imm8 = 0x00\n";
            PrintZa(mdl, u, sme, 1);
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
    CHECK("MOVA_T2V.S (tile to vector) move ZA3V.S[1] to Z[10] using P[5] (only first and third element)", sme, {"MOVA_T2V.S"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            // constrain target vertical slice (ZA3V.S[1])
            cstr_step_slice(s, u, ctx, sme, bv_val_128(ctx, 0x1111111122222222ULL, 0x3333333344444444ULL), 3, 1, true, WORD);
            // constrain vector register, z_reg_idx = 10
            cstr_step_bv(s, u, ctx, sme.Zd, 10ULL, sme.Zd.bit_width());
            // constrain the predicate reg and its value
            cstr_step_bv(s, u, ctx, sme.Pg, 5ULL, sme.Pg.bit_width());

            cstr_step_bv(s, u, ctx, sme.p_regs[5], 0x5ULL, P_REG_WIDTH); // only first and third element
            // cstr_step(s, u, ctx, sme.p_regs[5], ctx.bv_val(-1, P_REG_WIDTH)); // all ones
            
            // constrain tile selection, tile_idx == 3
            cstr_step_bv(s, u, ctx, sme.ZAn, 3ULL, sme.ZAn.bit_width());
            cstr_step_bool(s, u, ctx, sme.HV, true); // vertical == true
            // constrain slice index by fixing W[2] and Imm, col_idx == 1
            cstr_step_bv(s, u, ctx, sme.Rs, 2ULL, sme.Rs.bit_width());

            cstr_step_bv(s, u, ctx, sme.Get32BitGPR(2), 0ULL, 32); // NOTE equivalent with below
            // cstr_step(s, u, ctx, sme.Get32BitGPR(2), ctx.bv_val(0, 32));
            
            cstr_step_bv(s, u, ctx, sme.Imm, 1ULL, sme.Imm.bit_width());
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            std::cout << " initialized a WORD at every other row, forming vertical slice\n";
            PrintZa(mdl, u, sme, 0);
            auto ver_slice = u.GetZ3Expr(sme.GetVerticalSlice(sme.za, 3, 1, WORD), 1);
            auto z_reg_val = u.GetZ3Expr(sme.GetVectorRegister(10), 1);
            PRINT(sme.z_regs[10], 0, u, mdl, "Z reg before MOVA_T2V.S");
            PRINT(sme.z_regs[10], 1, u, mdl, "Z reg after MOVA_T2V.S");
            std::string ver_slice_eval = mdl.eval(ver_slice).to_string();
            std::string z_reg_val_eval = mdl.eval(z_reg_val).to_string();
            // EXPECT_TRUE(ver_slice_eval == z_reg_val_eval); // only if predicate was all ones
            EXPECT_TRUE(z_reg_val_eval == "#x00000000222222220000000044444444");
        }
    );

    CHECK("MOVA_V2T.D (vector to tile) move Z[10] to ZA7V.D[1] using P[2]", sme, {"MOVA_V2T.D"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            InitZaToZero(s, u, ctx, sme);
            cstr_step_bv(s, u, ctx, sme.ZAd, 7ULL, sme.ZAd.bit_width()); // ZA tile 7
            cstr_step_bool(s, u, ctx, sme.HV, true); // vertical
            cstr_step_bv(s, u, ctx, sme.Rs, 3ULL, sme.Rs.bit_width()); // W[3]
            cstr_step_bv(s, u, ctx, sme.Get32BitGPR(3), 0ULL, 32); // W[3] == all zeroes
            cstr_step_bv(s, u, ctx, sme.Imm1, 1ULL, sme.Imm1.bit_width()); // slice 1, leftmost
            cstr_step_bv(s, u, ctx, sme.Pg, 2ULL, sme.Pg.bit_width()); // P[2]
            cstr_step(s, u, ctx, sme.p_regs[2], ctx.bv_val(-1, P_REG_WIDTH)); // all ones
            cstr_step_bv(s, u, ctx, sme.Zn, 10ULL, sme.Zn.bit_width()); // Z[10]
            cstr_step(s, u, ctx, sme.z_regs[10], bv_val_128(ctx, 0x1111111122222222ULL, 0x3333333344444444ULL)); // populate the source vector register
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            PRINT(sme.z_regs[10], 0, u, mdl, "Z[10] at step 0");
            PrintZa(mdl, u, sme, 0);
            std::cout << " ZA updated to contain Z[10] vertically\n";
            PrintZa(mdl, u, sme, 1);
            auto ver_slice = sme.GetVerticalSlice(sme.za, 7, 1, DOUBLE);
            std::string eval = mdl.eval(u.GetZ3Expr(ver_slice, 1)).to_string();
            PRINT(ver_slice, 1, u, mdl, "ZA7V.D[1] vertical slice at step 1");
            EXPECT_TRUE(eval == "#x11111111222222223333333344444444");
        }
    );

}

void test_zero(ArmSme &sme) {
    CHECK("ZERO Imm8=0xFF zeroes entire ZA array that was initialized to non-zero", sme, {"ZERO"},
        [&](IlaZ3Unroller &u, z3::solver &s, z3::context &ctx) {
            cstr_step_bv(s, u, ctx, sme.Imm8, 0xFFULL, 8);
            for (size_t addr = 0; addr < ZA_BYTE_SIZE; addr++) {
                auto ila_byte = Load(sme.za, BvConst(addr, sme.za.addr_width()));
                cstr_step_bv(s, u, ctx, ila_byte, 0xAA, BYTE, 0);
            }
        },
        [&](z3::model &mdl, IlaZ3Unroller &u) {
            std::cout << " before zeroing\n";
            PrintZa(mdl, u, sme, 0);
            std::cout << " after zeroing\n";
            PrintZa(mdl, u, sme, 1);
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
            PrintZa(mdl, u, sme, 0);
            std::cout << " now all ZA0.H bytes are zeroed\n";
            PrintZa(mdl, u, sme, 1);
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
            PrintZa(mdl, u, sme, 0);
            std::cout << " now all ZA3.S bytes are zeroed\n";
            PrintZa(mdl, u, sme, 1);
            size_t dim = SVL_B / WORD; // since it's .S suffix
            for (size_t row = 0; row < dim; row++) {
                auto ila_row = sme.GetHorizontalSlice(sme.za, 0, row, WORD);
                std::string val = mdl.eval(u.GetZ3Expr(ila_row, 1)).to_string();
                EXPECT_TRUE(val == "#x00000000000000000000000000000000");
            }
        }
    );

}
