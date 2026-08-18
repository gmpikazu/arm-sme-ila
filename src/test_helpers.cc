#include <cmath>
#include <iostream>
#include <iomanip>
#include <chrono>
#include "../include/test_helpers.h"
#include "../include/arm.h"
#include "ilang/ilang++.h"

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

// ILA States
void cstr_step_ila(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, const ilang::ExprRef &ila_expr1, int step1, const ilang::ExprRef &ila_expr2, int step2, bool equal) {
    auto expr1 = u.GetZ3Expr(ila_expr1, step1);
    auto expr2 = u.GetZ3Expr(ila_expr2, step2);
    if (equal) { s.add(expr1 == expr2); }
    else { s.add(expr1 != expr2); }
}

// Create a 128-bit Z3 expression from two 64-bit halves
z3::expr bv_val_128(z3::context &ctx, uint64_t high_half, uint64_t low_half) {
    return z3::concat(ctx.bv_val(high_half, 64), ctx.bv_val(low_half, 64));
}

z3::expr bv_val_N(z3::context &ctx, std::vector<uint64_t> list) {
    assert(list.size() > 0);
    auto res = ctx.bv_val(list[0], 64);
    for (size_t i = 1; i < list.size(); i++) {
        res = z3::concat(res, ctx.bv_val(list[i], 64));
    }
    return res;
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
    return Load(sme.za, BvConst(row * sme.SVL_B + col, sme.za.addr_width()));
}

#define MAX_BYTES_PER_LINE 16
void PrintDRAM(z3::model &mdl, ilang::IlaZ3Unroller &u, ArmSme& sme, int start_addr, int step, int num_bytes) {
    // top border
    for (int i = 0; i < MAX_BYTES_PER_LINE*5; i++) { std::cout << "-"; }
    std::cout << std::endl << " DRAM - Step " << step << std::endl;
    for (int i = 0; i < MAX_BYTES_PER_LINE*5; i++) { std::cout << "-"; }
    std::cout << std::endl;

    // segmenting num_bytes using MAX_BYTES_PER_LINE
    int addr = start_addr;
    int remaining_bytes = num_bytes;
    while (addr < start_addr + num_bytes) {
        // TODO: find more descriptive name
        auto upper_bound = std::min(remaining_bytes, MAX_BYTES_PER_LINE);
        
        // print addresses
        for (int i = 0; i < upper_bound; i++) {
            std::cout << " " << std::left << std::setw(4) << (addr + i);
        }
        std::cout << std::endl;

        // print DRAM bytes
        for (int i = 0; i < upper_bound; i++) {
            auto byte = sme.DRAM_GetByteNoEndian(addr + i);
            std::string val = mdl.eval(u.GetZ3Expr(byte, step)).to_string();
            std::cout << " " << std::setw(4) << val;
        }
        std::cout << std::endl;

        // low border
        for (int i = 0; i < MAX_BYTES_PER_LINE*5; i++) { std::cout << "-"; }
        std::cout << std::endl;

        addr += MAX_BYTES_PER_LINE;
        remaining_bytes -= MAX_BYTES_PER_LINE;
    }
}

// Print ZA in a formatted ASCII table
void PrintZa(z3::model &mdl, ilang::IlaZ3Unroller &u, ArmSme& sme, int step) {
    const int cell_width = 3; // includes '\0'

    std::cout << "┌";
    for (size_t col = 0; col < sme.SVL_B; col++) {
        std::cout << std::string(cell_width, '-');
    }
    std::cout << "─┐" << std::endl;
    int step_len = std::to_string(step).length();
    int spaces = 16 * cell_width - 38 - step_len; // pad to align right border
    std::cout << "│ ZA TILE MEMORY LAYOUT (16x16) - Step " << step << " ";
    std::cout << std::string(spaces, ' ') << "│" << std::endl;
    std::cout << "├";
    for (size_t col = 0; col < sme.SVL_B; col++) {
        std::cout << std::string(cell_width, '-');
    }
    std::cout << "─┤" << std::endl;

    // Print column headers
    std::cout << "│";
    for (ssize_t col = sme.SVL_B-1; col >= 0; col--) {
        std::cout << std::setw(cell_width) << std::right << col;
    }
    std::cout << " │" << std::endl;
    std::cout << "├";
    for (size_t col = 0; col < sme.SVL_B; col++) {
        std::cout << std::string(cell_width, '-');
    }
    std::cout << "─┤" << std::endl;

    // Print each row
    for (size_t row = 0; row < sme.SVL_B; row++) {
        std::cout << "│ ";
        for (size_t col = 0; col < sme.SVL_B; col++) {
            size_t addr = row * sme.SVL_B + col;
            auto byte_expr = Load(sme.za, BvConst(addr, sme.za.addr_width()));
            auto byte_val = mdl.eval(u.GetZ3Expr(byte_expr, step)).to_string();

            // Remove #x prefix if present
            if (byte_val.size() > 2 && byte_val.substr(0, 2) == "#x") {
                byte_val = byte_val.substr(2);
            }

            // NOTE: make 00 into __
            if (byte_val == "00") { byte_val = "__"; }

            // without prefix
            std::cout << std::setw(2) << std::setfill('0') << std::uppercase << byte_val << " ";
            std::cout << std::setfill(' ');
        }
        std::cout << "│ R" << std::setw(2) << std::left << row << " " << std::endl;;
    }

    std::cout << "└";
    for (size_t col = 0; col < sme.SVL_B; col++) {
        std::cout << std::string(cell_width, '-');
    }
    std::cout << "─┘" << std::endl;
}

void InitZaToZero(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, ArmSme& sme, int step) {
    for (size_t addr = 0; addr < sme.ZA_BYTE_SIZE; addr++) {
        auto byte_expr = Load(sme.za, BvConst(addr, sme.za.addr_width()));
        cstr_step_bv(s, u, ctx, byte_expr, 0x00, BYTE, step);
    }
}


void cstr_step_slice(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, ArmSme& sme,
                     const z3::expr &value_expr,
                     int tile_idx, int slice_idx, bool is_vertical, const ilang::NumericType& element_size_bits,
                     int step) {
    // get all addresses touched by this slice
    auto touched_addrs = sme.GetSliceAddresses(tile_idx, slice_idx, is_vertical, element_size_bits);
    // zero all bytes NOT touched by the slice
    for (size_t addr = 0; addr < sme.ZA_BYTE_SIZE; addr++) {
        bool is_touched = false;
        for (size_t touched_addr : touched_addrs) {
            if (addr == touched_addr) {
                is_touched = true;
                break;
            }
        }
        if (!is_touched) {
            auto byte_expr = Load(sme.za, BvConst(addr, sme.za.addr_width()));
            cstr_step_bv(s, u, ctx, byte_expr, 0x00, BYTE, step);
        }
    }
    // compute the slice expression internally and constrain it
    if (is_vertical) {
        auto slice_expr = sme.GetVerticalSlice(sme.za, tile_idx, slice_idx, element_size_bits);
        cstr_step(s, u, ctx, slice_expr, value_expr, step);
    } else {
        auto slice_expr = sme.GetHorizontalSlice(sme.za, tile_idx, slice_idx, element_size_bits);
        cstr_step(s, u, ctx, slice_expr, value_expr, step);
    }
}

void track_slice(Tracker& tracker, const z3::expr& value_expr, int tile_idx, int slice_idx, bool is_vertical, const ilang::NumericType& element_size_bits, ArmSme& sme) {
    NumericType dim = sme.SVL / element_size_bits;
    NumericType num_tiles = sme.SVL_B / dim;
    int element_size_bytes = element_size_bits / BYTE;

    assert(value_expr.get_sort().bv_size() == sme.SVL);
    assert(sme.SVL == sme.Z_REG_WIDTH);
    auto GetVecByteLSB = [&](size_t idx) -> z3::expr {
        // get element of vector from LSB
        size_t rightmost = BYTE * idx;
        size_t leftmost = rightmost + BYTE - 1;
        return value_expr.extract(leftmost, rightmost);
    };
    auto GetVecByteMSB = [&](size_t idx) -> z3::expr {
        // get element of vector from MSB
        size_t mirrored_idx = sme.SVL_B - 1 - idx;
        return GetVecByteLSB(mirrored_idx);
    };

    if (is_vertical) {
        // ARM SME: col_idx % dim (required by ARM)
        int wrapped_col_idx = (dim == 1) ? 0 : (slice_idx & (dim - 1)); // & (dim-1) fast modulo
        int col = (sme.SVL_B - element_size_bytes) - (wrapped_col_idx * element_size_bytes);

        // bottom up direction for ARM SME vertical slice concatenation behavior
        for (int i = 0; i < dim; i++) {
            size_t row = tile_idx + i * num_tiles;
            size_t base_addr = row * sme.SVL_B + col;
            for (int b = 0; b < element_size_bytes; b++) {
                tracker.insert_or_assign(base_addr + b, GetVecByteMSB((dim-1-i) * element_size_bytes + b));
            }
        }
    } else {
        // ARM SME: row_idx % dim (required by ARM)
        int wrapped_row_idx = (dim == 1) ? 0 : (slice_idx & (dim - 1)); // & (dim-1) fast modulo
        size_t row = tile_idx + wrapped_row_idx * num_tiles;

        for (size_t col = 0; col < sme.SVL_B; col++) {
            tracker.insert_or_assign(row * sme.SVL_B + col, GetVecByteMSB(col));
        }
    }
}

void cstr_all_tracked_and_zero(z3::solver &s, ilang::IlaZ3Unroller &u, z3::context &ctx, const Tracker& tracker, ArmSme& sme, int step) {
    for (size_t addr = 0; addr < sme.ZA_BYTE_SIZE; addr++) {
        auto byte_expr = Load(sme.za, BvConst(addr, sme.za.addr_width()));
        auto it = tracker.find(addr);
        if (it != tracker.end()) {
            z3::expr val = it->second;
            cstr_step(s, u, ctx, byte_expr, val, step);
        }
        else {
            cstr_step_bv(s, u, ctx, byte_expr, 0x00, BYTE, step);
        }
    }
}

std::string TO_STR(const ilang::ExprRef &ila_expr, int step, ilang::IlaZ3Unroller &u, z3::model &mdl) {
    auto expr = u.GetZ3Expr(ila_expr, step);
    auto eval = mdl.eval(expr);
    return eval.to_string();
}

void PRINT(const ilang::ExprRef &ila_expr, int step, ilang::IlaZ3Unroller &u, z3::model &mdl, std::string label) {
    auto expr = u.GetZ3Expr(ila_expr, step);
    auto eval = mdl.eval(expr);
    std::cout << "LOG[\"" << label << "\"] : " << eval.to_string() << std::endl;
}

void CHECK(const std::string& test_name, ArmSme& sme, const std::vector<std::string>& instr_names,
           std::function<void(ilang::IlaZ3Unroller&, z3::solver&, z3::context&)> setup_fn,
           std::function<void(z3::model&, ilang::IlaZ3Unroller&)> verify_fn) {
    std::cout << "\n\n\n=== Test: " << test_name << " ===" << std::endl;
    bool test_passed = true;
    
    // reset failure count for this test
    g_current_failures = 0;

    // print instruction pipeline (to ensure correct instruction was passed into std::vector)
    std::cout << "  [INSTRUCTIONS] start --> ";
    for (size_t i = 0; i < instr_names.size(); i++) {
        std::cout << instr_names[i] << " --> ";
    }
    std::cout << "done" << std::endl;

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
        
        using clk = std::chrono::high_resolution_clock;
        auto t0 = clk::now();

        // unroll the instruction path FIRST
        auto tr = u.UnrollPathConn(instrs, 0);
        s.add(tr);
        auto t1 = clk::now();

        // call setup lambda to add constraints AFTER unrolling
        setup_fn(u, s, ctx);
        auto t2 = clk::now();

        // NOTE: initialize sme.faults to zero before solving
        cstr_step(s, u, ctx, sme.faults, ctx.bv_val(0, sme.faults.bit_width()), 0); // step 0
        
        // set timeout (30 seconds)
        z3::params p(ctx);
        p.set("timeout", (unsigned)30000);
        s.set(p);
        
        // solve
        auto result = s.check();
        auto t3 = clk::now();

        auto ms = [](auto a, auto b){ return (int)std::chrono::duration_cast<std::chrono::milliseconds>(b-a).count(); };
        std::cout << "  [TIME] unroll=" << ms(t0,t1) << "ms  setup=" << ms(t1,t2) << "ms  solve=" << ms(t2,t3) << "ms" << std::endl;

        if (result == z3::sat) {

            // call verify lambda with the model
            auto mdl = s.get_model();
            verify_fn(mdl, u);
            auto t4 = clk::now();
            std::cout << "  [TIME] verify+print=" << ms(t3,t4) << "ms" << std::endl;

            // NOTE: ensure no fault occurred throughout execution pipeline
            std::cout << "--- CHECKING FOR FAULTS ---" << std::endl;
            for (size_t step = 0; step < 1 + instr_names.size(); step++) { // extra 1 for result step
                auto got = TO_STR(sme.faults, step, u, mdl);
                auto expected = TO_STR(BvConst(0, sme.faults.bit_width()), step, u, mdl);
                bool fault_found = (got != expected);
                std::cout << "step: " << step << " faults: " << got << " " << std::endl;
                if (fault_found) {
                    record_failure("FAULT OCCURRED!!!");
                }
            }

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
