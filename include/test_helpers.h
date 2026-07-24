#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <cstddef>
#include <vector>
#include <string>
#include <functional>
#include <ilang/ilang++.h>

namespace arm {

class ArmSme; // forward declaration

struct TestResult {
    std::string test_name;
    bool passed;
};

// global test result tracking
extern std::vector<TestResult> g_test_results;

void record_failure(const std::string& msg);

// EXPECT_EQ for booleans
#define EXPECT_EQ_BOOL(expr, expected) \
    do { \
        bool actual = (expr); \
        if (actual != (expected)) { \
            record_failure("EXPECT_EQ: " #expr " = " + std::to_string(actual) + ", expected " + std::to_string(expected)); \
        } \
    } while(0)

// EXPECT_EQ for uint64_t
#define EXPECT_EQ_UINT(expr, expected) \
    do { \
        uint64_t actual = (expr); \
        if (actual != (expected)) { \
            record_failure("EXPECT_EQ: " #expr " = " + std::to_string(actual) + ", expected " + std::to_string(expected)); \
        } \
    } while(0)

// EXPECT_TRUE
#define EXPECT_TRUE(expr) \
    do { \
        bool actual = (expr); \
        if (!actual) { \
            record_failure("EXPECT_TRUE: " #expr " is false"); \
        } \
    } while(0)

// EXPECT_FALSE
#define EXPECT_FALSE(expr) \
    do { \
        bool actual = (expr); \
        if (actual) { \
            record_failure("EXPECT_FALSE: " #expr " is true"); \
        } \
    } while(0)

// --------------------------------------------------------------
// Helper to constrain an ILA state variable at a specific step and add to solver
// Call AFTER unrolling, adds constraint directly to solver
// step defaults to step 0 (initial step)
void cstr_step_bool(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, const ilang::ExprRef &ila_expr, bool value, int step=0);
void cstr_step_int(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, const ilang::ExprRef &ila_expr, int value, int step=0);
// NOTE only supports 64 bit length, for bigger lengths just use cstr_step and ctx.bv_val() manually
void cstr_step_bv(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, const ilang::ExprRef &ila_expr, uint64_t value, size_t bit_width, int step=0);
void cstr_step(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, const ilang::ExprRef &ila_expr, const z3::expr &value_expr, int step=0);
// --------------------------------------------------------------

// PRINT function
// @brief prints ila_expr in human-readable format
void PRINT(const ilang::ExprRef &ila_expr, int step, ilang::IlaZ3Unroller &u);

// CHECK function
// @brief handles instruction lookup by name, unroll, timeout, solving, error
// @param[in] sme: reference to ArmSme instance 
// @param[in] instr_names: list of instruction names to unroll (in order) 
// @param[in] setup_fn: lambda that adds constraints (called AFTER unrolling) 
// @param[in] verify_fn: lambda that verifies results (called AFTER solving if SAT)
void CHECK(
    const std::string &test_name, ArmSme &sme,
    const std::vector<std::string> &instr_names,
    std::function<void(ilang::IlaZ3Unroller &, z3::solver &, z3::context &)> setup_fn,
    std::function<void(z3::model &, ilang::IlaZ3Unroller &)> verify_fn);

// Ctest-inspired summary of all tests
void print_test_summary();

}  // namespace arm

#endif // TEST_HELPERS_H