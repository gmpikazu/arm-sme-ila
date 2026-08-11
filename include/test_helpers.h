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
// NOTE: only supports 64 bit length, for bigger lengths just use cstr_step and ctx.concat(bv_val(),bv_val()) manually
void cstr_step_bv(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, const ilang::ExprRef &ila_expr, uint64_t value, size_t bit_width, int step=0);
void cstr_step(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, const ilang::ExprRef &ila_expr, const z3::expr &value_expr, int step=0);
void cstr_step_ila(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, const ilang::ExprRef &ila_expr1, int step1, const ilang::ExprRef &ila_expr2, int step2, bool equal=true);
// --------------------------------------------------------------

// --------------------------------------------------------------
// ZA tile helper functions
// --------------------------------------------------------------
// Create a 128-bit Z3 expression from two 64-bit halves
// Z3's ctx.bv_val only supports up to 64-bit, so we need this helper
z3::expr bv_val_128(z3::context &ctx, uint64_t high_half, uint64_t low_half);
z3::expr bv_val(z3::context &ctx, std::vector<uint64_t> values);

// Get byte at specific row and column in ZA tile (row-major internal storage)
// row, col: 0-indexed, range [0, SVL_B-1] = [0, 15]
ilang::ExprRef GetByteAtRowCol(ArmSme& sme, int row, int col);

// Print ZA in a formatted ASCII table (dark mode friendly)
// step parameter allows inspection at any timestep (default: step 0)
// Format: row 0 at top, column 0 at left, each cell shows hex value
void PrintDRAM(z3::model &mdl, ilang::IlaZ3Unroller &u, ArmSme& sme, int start_addr, int step, int num_bytes);
void PrintZa(z3::model &mdl, ilang::IlaZ3Unroller &u, ArmSme& sme, int step=0);
void InitZaToZero(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, ArmSme& sme, int step=0);

// NOTE: Prefer track_slice + cstr_all_tracked_and_zero idiom
// Constrain a slice (horizontal or vertical) to a value and automatically zero all other ZA bytes
// This handles Z3 model completion by ensuring unconstrained bytes show as 0x00
// and automatically computes the slice expression internally to constrain it
void cstr_step_slice(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, ArmSme& sme,
                     const z3::expr &value_expr,
                     int tile_idx, int slice_idx, bool is_vertical, const ilang::NumericType& element_size_bits,
                     int step=0);

typedef std::unordered_map<size_t, z3::expr> Tracker;
// Does not constrain, just tracks a mapping (override) between address and z3::expr
// Must finally call cstr_all_tracked() to constrain and zero out unconstrained addresses
void track_slice(Tracker& tracker, const z3::expr& value_expr, int tile_idx, int slice_idx, bool is_vertical, const ilang::NumericType& element_size_bits);
void cstr_all_tracked_and_zero(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, const Tracker& tracker, ArmSme& sme, int step=0); // constrains the tracker and zeroes out the rest

// TO_STR converts the ila expression into an evaluated string
std::string TO_STR(const ilang::ExprRef &ila_expr, int step, ilang::IlaZ3Unroller &u, z3::model &mdl);

// PRINT function
// @brief prints ila_expr in human-readable format
void PRINT(const ilang::ExprRef &ila_expr, int step, ilang::IlaZ3Unroller &u, z3::model &mdl, std::string label = "");

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
