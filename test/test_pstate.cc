#include "../include/test_helpers.h"
#include "../include/arm.h"

using namespace ilang;
using namespace arm;

void test_pstate(ArmSme& sme) {
    CHECK("SMSTART sets pstate and resets SME/SVE states", sme, {"SMSTART"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            // step 0 fill ZA with nonzero garbage
            for (size_t addr = 0; addr < sme.ZA_BYTE_SIZE; addr++) {
                auto byte_expr = Load(sme.za, BvConst(addr, sme.za.addr_width()));
                cstr_step(s, u, ctx, byte_expr, ctx.bv_val(-1, BYTE));
            }
            // fill P[1], Z[1] with nonzero garbage
            cstr_step(s, u, ctx, sme.p_regs[1], ctx.bv_val(-1, sme.P_REG_WIDTH));
            cstr_step(s, u, ctx, sme.z_regs[1], ctx.bv_val(-1, sme.Z_REG_WIDTH));
            // disable both pstates initially
            cstr_step_bool(s, u, ctx, sme.pstate_sm, false);
            cstr_step_bool(s, u, ctx, sme.pstate_za, false);
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            // pstate changes
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_sm, 0)).to_string() == "false");
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_za, 0)).to_string() == "false");
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_sm, 1)).to_string() == "true");
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_za, 1)).to_string() == "true");
            // garbage are all zeroed
            std::cout << " initial ZA\n";
            PrintZa(mdl, u, sme, 0);
            std::cout << " should be zeroed out\n";
            PrintZa(mdl, u, sme, 1);
            for (size_t addr = 0; addr < sme.ZA_BYTE_SIZE; addr++) {
                auto byte_expr = Load(sme.za, BvConst(addr, sme.za.addr_width()));
                EXPECT_TRUE(TO_STR(byte_expr, 1, u, mdl) == "#x00");
            }
            std::cout << " garbage vectors\n";
            PRINT(sme.p_regs[1], 0, u, mdl, "P[1] @ 0");
            PRINT(sme.z_regs[1], 0, u, mdl, "Z[1] @ 0");
            std::cout << " now zeroed out\n";
            PRINT(sme.p_regs[1], 1, u, mdl, "P[1] @ 1");
            PRINT(sme.z_regs[1], 1, u, mdl, "Z[1] @ 1");
            EXPECT_TRUE(TO_STR(sme.p_regs[1], 1, u, mdl) == "#x0000");
            EXPECT_TRUE(TO_STR(sme.z_regs[1], 1, u, mdl) == "#x00000000000000000000000000000000");
        }
    );
    
    CHECK("SMSTOP clears pstate and resets ONLY SVE state", sme, {"SMSTOP"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx){
            // step 0 fill ZA with nonzero garbage
            for (size_t addr = 0; addr < sme.ZA_BYTE_SIZE; addr++) {
                auto byte_expr = Load(sme.za, BvConst(addr, sme.za.addr_width()));
                cstr_step(s, u, ctx, byte_expr, ctx.bv_val(-1, BYTE));
            }
            // fill P[1], Z[1] with nonzero garbage
            cstr_step(s, u, ctx, sme.p_regs[1], ctx.bv_val(-1, sme.P_REG_WIDTH));
            cstr_step(s, u, ctx, sme.z_regs[1], ctx.bv_val(-1, sme.Z_REG_WIDTH));
            // disable both pstates initially
            cstr_step_bool(s, u, ctx, sme.pstate_sm, true);
            cstr_step_bool(s, u, ctx, sme.pstate_za, true);
        },
        [&](z3::model& mdl, IlaZ3Unroller& u){
            // pstate changes
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_sm, 0)).to_string() == "true");
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_za, 0)).to_string() == "true");
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_sm, 1)).to_string() == "false");
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_za, 1)).to_string() == "false");
            // garbage persists
            std::cout << " initial ZA\n";
            PrintZa(mdl, u, sme, 0);
            std::cout << " should still be same as original\n";
            PrintZa(mdl, u, sme, 1);
            for (size_t addr = 0; addr < sme.ZA_BYTE_SIZE; addr++) {
                auto byte_expr = Load(sme.za, BvConst(addr, sme.za.addr_width()));
                // step 0 and step 1 garbage should be exactly same
                EXPECT_TRUE(TO_STR(byte_expr, 1, u, mdl) == TO_STR(byte_expr, 0, u, mdl));
            }
            // garbage are all zeroed
            std::cout << " garbage vectors\n";
            PRINT(sme.p_regs[1], 0, u, mdl, "P[1] @ 0");
            PRINT(sme.z_regs[1], 0, u, mdl, "Z[1] @ 0");
            std::cout << " now zeroed out\n";
            PRINT(sme.p_regs[1], 1, u, mdl, "P[1] @ 1");
            PRINT(sme.z_regs[1], 1, u, mdl, "Z[1] @ 1");
            EXPECT_TRUE(TO_STR(sme.p_regs[1], 1, u, mdl) == "#x0000");
            EXPECT_TRUE(TO_STR(sme.z_regs[1], 1, u, mdl) == "#x00000000000000000000000000000000");
        }
    );
}
