#include <iostream>
#include <iomanip>
#include "../include/test_helpers.h"
#include "../include/arm.h"

namespace arm {

std::vector<TestResult> g_test_results;

thread_local int g_current_failures = 0;

void record_failure(const std::string& msg) {
    std::cerr << " (!) FAIL: " << msg << std::endl;
    g_current_failures++;
}

// Helper to constrain an ILA state variable at a specific step
// Call AFTER unrolling, adds constraint directly to solver
// step defaults to step 0 (initial step)
// --------------------------------------------------------------
// Bool
void cstr_step_bool(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, const ilang::ExprRef &ila_expr, bool value, int step) {
    auto expr = u.GetZ3Expr(ila_expr, step);
    s.add(expr == ctx.bool_val(value));
}

// Z3 Int
void cstr_step_int(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, const ilang::ExprRef &ila_expr, int value, int step) {
    auto expr = u.GetZ3Expr(ila_expr, step);
    s.add(expr == ctx.int_val(value));
}

// Bit Vector
// matches Z3's bv_val(uint64_t, unsigned) overload
void cstr_step_bv(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, const ilang::ExprRef &ila_expr, uint64_t value, size_t bit_width, int step) {
    auto expr = u.GetZ3Expr(ila_expr, step);
    s.add(expr == ctx.bv_val(value, bit_width));
}

// Generic Z3 Expression
void cstr_step(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, const ilang::ExprRef &ila_expr, const z3::expr &value_expr, int step) {
    auto expr = u.GetZ3Expr(ila_expr, step);
    s.add(expr == value_expr);
}

// --------------------------------------------------------------
// ZA tile helper functions
// --------------------------------------------------------------
// Create a 128-bit Z3 expression from two 64-bit halves
z3::expr bv_val_128(z3::context &ctx, uint64_t high_half, uint64_t low_half) {
    return z3::concat(ctx.bv_val(high_half, 64), ctx.bv_val(low_half, 64));
}

// Turns array of 64-bit hexadecimal into a bigger one through concatenation
// values[0] becomes MSB
z3::expr bv_val(z3::context &ctx, std::vector<uint64_t> values) {
    assert(values.size() != 0);
    auto expr = ctx.bv_val(values[0], 64);
    for (size_t i = 1; i < values.size(); i++) {
        expr = z3::concat(expr, ctx.bv_val(values[i], 64));
    }
    return expr;
}

// Get byte at specific row and column in ZA tile
ilang::ExprRef GetByteAtRowCol(ArmSme& sme, int row, int col) {
    // ZA linear memory layout: row-major, each row = SVL_B bytes
    // address = row * SVL_B + col
    return Load(sme.za, BvConst(row * SVL_B + col, sme.za.addr_width()));
}

// Print ZA in a formatted ASCII table (dark mode friendly)
void PrintZaCsv(z3::model &mdl, ilang::IlaZ3Unroller &u, ArmSme& sme, int step) {
    const int cell_width = 3; // includes '\0'
    
    std::cout << "┌";
    for (size_t col = 0; col < SVL_B; col++) {
        std::cout << std::string(cell_width, '-');
    }
    std::cout << "─┐" << std::endl;
    int step_len = std::to_string(step).length();
    int spaces = 16 * cell_width - 38 - step_len; // pad to align right border
    std::cout << "│ ZA TILE MEMORY LAYOUT (16x16) - Step " << step << " ";
    std::cout << std::string(spaces, ' ') << "│" << std::endl;
    std::cout << "├";
    for (size_t col = 0; col < SVL_B; col++) {
        std::cout << std::string(cell_width, '-');
    }
    std::cout << "─┤" << std::endl;
    
    // Print column headers
    std::cout << "│";
    for (size_t col = 0; col < SVL_B; col++) {
        std::cout << std::setw(cell_width) << std::right << col;
    }
    std::cout << " │" << std::endl;
    std::cout << "├";
    for (size_t col = 0; col < SVL_B; col++) {
        std::cout << std::string(cell_width, '-');
    }
    std::cout << "─┤" << std::endl;
    
    // Print each row
    for (size_t row = 0; row < SVL_B; row++) {
        std::cout << "│ ";
        for (size_t col = 0; col < SVL_B; col++) {
            size_t addr = row * SVL_B + col;
            auto byte_expr = Load(sme.za, BvConst(addr, sme.za.addr_width()));
            auto byte_val = mdl.eval(u.GetZ3Expr(byte_expr, step)).to_string();
            
            // Remove #x prefix if present
            if (byte_val.size() > 2 && byte_val.substr(0, 2) == "#x") {
                byte_val = byte_val.substr(2);
            }
            
            // without prefix
            std::cout << std::setw(2) << std::setfill('0') << std::uppercase << byte_val << " ";
            std::cout << std::setfill(' ');
        }
        std::cout << "│ R" << std::setw(2) << std::left << row << " " << std::endl;;
    }
    
    std::cout << "└";
    for (size_t col = 0; col < SVL_B; col++) {
        std::cout << std::string(cell_width, '-');
    }
    std::cout << "─┘" << std::endl;
}

void PRINT(const ilang::ExprRef &ila_expr, int step, ilang::IlaZ3Unroller &u, z3::model &mdl, std::string label) {
    static size_t counter = 0;
    auto expr = u.GetZ3Expr(ila_expr, step);
    auto eval = mdl.eval(expr);
    std::cout << " LOG[" << counter++ << "] '" << label << "' "<< eval.to_string() << std::endl;
}

void CHECK(const std::string& test_name, ArmSme& sme, const std::vector<std::string>& instr_names,
           std::function<void(ilang::IlaZ3Unroller&, z3::solver&, z3::context&)> setup_fn,
           std::function<void(z3::model&, ilang::IlaZ3Unroller&)> verify_fn) {
    std::cout << "\n=== Test: " << test_name << " ===" << std::endl;
    bool test_passed = true;
    
    // reset failure count for this test
    g_current_failures = 0;
    
    try {
        ilang::Ila m = sme.get();
        
        // find instructions by name
        std::vector<ilang::InstrRef> instrs;
        for (const auto& name : instr_names) {
            bool found = false;
            for (size_t i = 0; i < m.instr_num(); i++) {
                if (m.instr(i).name() == name) {
                    instrs.push_back(m.instr(i));
                    found = true;
                    break;
                }
            }
            if (!found) {
                throw std::runtime_error("Instruction '" + name + "' not found");
            }
        }
        
        z3::context ctx;
        ilang::IlaZ3Unroller u(ctx);
        z3::solver s(ctx);
        
        // unroll the instruction path FIRST
        auto tr = u.UnrollPathConn(instrs, 0);
        s.add(tr);
        
        // call setup lambda to add constraints AFTER unrolling
        setup_fn(u, s, ctx);
        
        // set timeout (30 seconds)
        z3::params p(ctx);
        p.set("timeout", (unsigned)30000);
        s.set(p);
        
        // solve
        auto result = s.check();
        
        if (result == z3::sat) {
            // call verify lambda with the model
            auto mdl = s.get_model();
            verify_fn(mdl, u);
        } else if (result == z3::unsat) {
            record_failure("Solver returned UNSAT - no valid execution path");
        } else {
            record_failure("Solver returned UNKNOWN/timeout");
        }
        
        // check if any assertions failed
        if (g_current_failures > 0) {
            test_passed = false;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        test_passed = false;
    }
    
    // record result
    TestResult result;
    result.test_name = test_name;
    result.passed = test_passed;
    g_test_results.push_back(result);
    
    std::cout << "=== " << (test_passed ? "PASS" : "FAIL") << ": " << test_name << " ===" << std::endl;
}

void print_test_summary() {
    std::cout << "\n\n===========================================" << std::endl;
    std::cout << "            TEST SUMMARY" << std::endl;
    std::cout << "===========================================" << std::endl;
    
    int passed = 0;
    int failed = 0;
    
    for (const auto& result : g_test_results) {
        std::cout << "  " << (result.passed ? "[PASS]" : "[FAIL]") 
                  << " " << result.test_name << std::endl;
        if (result.passed) passed++;
        else failed++;
    }
    
    std::cout << "\n  Total: " << g_test_results.size() << " tests" << std::endl;
    std::cout << "  Passed: " << passed << std::endl;
    std::cout << "  Failed: " << failed << std::endl;
    
    if (failed == 0) {
        std::cout << "\n  All tests passed! 🎉" << std::endl;
    }
    
    std::cout << "===========================================" << std::endl;
}

}  // namespace arm