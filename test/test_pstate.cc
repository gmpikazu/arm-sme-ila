#include "../include/test_helpers.h"
#include "../include/arm.h"

using namespace ilang;
using namespace arm;

void test_pstate(ArmSme& sme) {
    CHECK("SMSTART sets pstate_sm and pstate_za to true", sme, {"SMSTART"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx) {
            cstr_step_bool(s, u, ctx, sme.pstate_sm, false);
            cstr_step_bool(s, u, ctx, sme.pstate_za, false);
        },
        [&](z3::model& mdl, IlaZ3Unroller& u) {
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_sm, 0)).to_string() == "false");
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_za, 0)).to_string() == "false");
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_sm, 1)).to_string() == "true");
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_za, 1)).to_string() == "true");
        }
    );
    
    CHECK("SMSTOP sets pstate_sm and pstate_za to false", sme, {"SMSTOP"},
        [&](IlaZ3Unroller& u, z3::solver& s, z3::context& ctx){
            cstr_step_bool(s, u, ctx, sme.pstate_sm, true);
            cstr_step_bool(s, u, ctx, sme.pstate_za, true);
        },
        [&](z3::model& mdl, IlaZ3Unroller& u){
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_sm, 0)).to_string() == "true");
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_za, 0)).to_string() == "true");
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_sm, 1)).to_string() == "false");
            EXPECT_TRUE(mdl.eval(u.GetZ3Expr(sme.pstate_za, 1)).to_string() == "false");
        }
    );
}