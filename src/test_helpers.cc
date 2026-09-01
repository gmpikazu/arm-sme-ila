#include <cmath>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <sys/types.h>
#include <unordered_map>
#include <utility>
#include <z3++.h>
#include "../include/test_helpers.h"
#include "../include/arm.h"
#include "ilang/ilang++.h"

namespace arm {

std::vector<TestResult> g_test_results;

using SubstituteList =  std::vector<std::pair<std::string, z3::expr>>; // internal only, not in header

// internal manual AST traversal to replace all UFs in bottom-up manner
static z3::expr _recursive_substitute(const z3::expr& ast_node, const SubstituteList& sub_list, std::unordered_map<Z3_ast, z3::expr>& cache) {
    // check cache first
    Z3_ast key = (Z3_ast)ast_node;
    auto it = cache.find(key);
    if (it != cache.end()) { return it->second; } // quick base case

    z3::context& ctx = ast_node.ctx(); // must reference
    z3::expr result = ast_node; // initially the original node

    if (ast_node.is_app()) {
        z3::func_decl decl = ast_node.decl();
        auto num_args = ast_node.num_args();

        // replace all UFs recurisvely in all the arguments
        std::vector<z3::expr> new_args;
        new_args.reserve(num_args);
        bool args_changed = false;
        for (auto i = 0; i < num_args; i++) {
            auto arg = ast_node.arg(i); // must is_app to call .arg()
            auto new_arg = _recursive_substitute(arg, sub_list, cache);
            new_args.push_back(new_arg);
            if (new_arg.id() != arg.id()) { args_changed = true; }
        }

        // replace current node itself too if UF
        bool match = false; // check that name matches in sub_list
        for (const auto& [target_name, template_body] : sub_list) {
            if (target_name == decl.name().str()) { 
                match = true; 
                z3::expr_vector sub_args(ctx);
                for (const auto& arg : new_args) { sub_args.push_back(arg); }
                auto body_copy = template_body;
                // fill placeholder template with concrete args
                result = body_copy.substitute(sub_args);
                break; 
            }
        }

        // a function was not our target but arguments changed, must rebuild pointers
        if (!match && args_changed) {
            result = decl(new_args.size(), new_args.data()); // operator() overload
            // rewires the child pointers to the new arguments instead of old ones containing UFs
        }
    } // else not an application, hence leaf

    cache.emplace(key, result); // memoize in place, no default constructor present
    return result; // three return cases: filled body, original, rebuilt node
}

// NOTE: tr is an AST, this wrapper calls a recursive traversal over this tree and uses a cache memo
static z3::expr substitute_funs(const z3::expr& ast_node, const SubstituteList& sub_list) {
    std::unordered_map<Z3_ast, z3::expr> cache; // memo, passed by reference
    // return substitute_funs_manual(ast_node, sub_list, cache);
    return _recursive_substitute(ast_node, sub_list, cache);
}

// use this helper to not forget syntax
static inline z3::expr mk_bound_var(z3::context& ctx, unsigned idx, z3::sort const& s) {
    return z3::expr(ctx, Z3_mk_bound(ctx, idx, s));
}

// NOTE: must call this during CHECK() to replace UFs with IEEE Z3 Floating Point Theory
// TODO: dotadd functions still DO NOT escape intermediate roundings
z3::expr substitute_fp_ufs(const z3::expr& ast_root, ilang::IlaZ3Unroller& u, ArmSme& sme, z3::context& ctx) {
    // default Round Nearest Even
    ctx.set_rounding_mode(z3::RNE);
    z3::expr rm = ctx.fpa_rounding_mode();
    // bit vector sort
    z3::sort bv16 = ctx.bv_sort(16);
    z3::sort bv32 = ctx.bv_sort(32);
    z3::sort bv64 = ctx.bv_sort(64);
    // floating point sort
    z3::sort fp16 = ctx.fpa_sort<16>();
    z3::sort fp32 = ctx.fpa_sort<32>();
    z3::sort fp64 = ctx.fpa_sort<64>();

    // UF names must match the strings passed to FuncRef constructor in arm.cc.
    SubstituteList sub_list;

    // bfneg16
    // NOTE: can just flip sign bit
    {
        z3::expr var0 = mk_bound_var(ctx, 0, bv16);
        z3::expr body = var0 ^ ctx.bv_val(0x8000, 16);
        sub_list.push_back({sme.bfneg16.name(), body});
    }
    // fpneg16
    {
        z3::expr var0 = mk_bound_var(ctx, 0, bv16);
        z3::expr body = (-var0.mk_from_ieee_bv(fp16)).mk_to_ieee_bv();
        sub_list.push_back({sme.fpneg16.name(), body});
    }
    // fpneg32
    {
        z3::expr var0 = mk_bound_var(ctx, 0, bv32);
        z3::expr body = (-var0.mk_from_ieee_bv(fp32)).mk_to_ieee_bv();
        sub_list.push_back({sme.fpneg32.name(), body});
    }
    // fpneg64
    {
        z3::expr var0 = mk_bound_var(ctx, 0, bv64);
        z3::expr body = (-var0.mk_from_ieee_bv(fp64)).mk_to_ieee_bv();
        sub_list.push_back({sme.fpneg64.name(), body});
    }
    // fpmac32(acc, a, b) = acc + a * b (FMA, one rounding)
    {
        z3::expr var_acc = mk_bound_var(ctx, 0, bv32);
        z3::expr var_a   = mk_bound_var(ctx, 1, bv32);
        z3::expr var_b   = mk_bound_var(ctx, 2, bv32);
        z3::expr body = z3::fma(var_a.mk_from_ieee_bv(fp32),
                                var_b.mk_from_ieee_bv(fp32),
                                var_acc.mk_from_ieee_bv(fp32),
                                rm).mk_to_ieee_bv();
        sub_list.push_back({sme.fpmac32.name(), body});
    }
    // fpmac64(acc, a, b) = acc + a * b (FMA, one rounding)
    {
        z3::expr var_acc = mk_bound_var(ctx, 0, bv64);
        z3::expr var_a   = mk_bound_var(ctx, 1, bv64);
        z3::expr var_b   = mk_bound_var(ctx, 2, bv64);
        z3::expr body = z3::fma(var_a.mk_from_ieee_bv(fp64),
                                var_b.mk_from_ieee_bv(fp64),
                                var_acc.mk_from_ieee_bv(fp64),
                                rm).mk_to_ieee_bv();
        sub_list.push_back({sme.fpmac64.name(), body});
    }
    // fpdotadd32to32(acc, a0, a1, b0, b1) = acc + a0 * b0 + a1 * b1
    {
        z3::expr var_acc = mk_bound_var(ctx, 0, bv32);
        z3::expr var_a0  = mk_bound_var(ctx, 1, bv32);
        z3::expr var_a1  = mk_bound_var(ctx, 2, bv32);
        z3::expr var_b0  = mk_bound_var(ctx, 3, bv32);
        z3::expr var_b1  = mk_bound_var(ctx, 4, bv32);

        // ASK: how to do single rounding? widen all to fp64, multiply, sum, round once to fp32?
        z3::expr acc_fp64 = z3::fpa_to_fpa(var_acc.mk_from_ieee_bv(fp32), fp64);
        z3::expr a0_fp64  = z3::fpa_to_fpa(var_a0 .mk_from_ieee_bv(fp32), fp64);
        z3::expr a1_fp64  = z3::fpa_to_fpa(var_a1 .mk_from_ieee_bv(fp32), fp64);
        z3::expr b0_fp64  = z3::fpa_to_fpa(var_b0 .mk_from_ieee_bv(fp32), fp64);
        z3::expr b1_fp64  = z3::fpa_to_fpa(var_b1 .mk_from_ieee_bv(fp32), fp64);

        z3::expr sum_fp64 = acc_fp64 + (a0_fp64 * b0_fp64) + (a1_fp64 * b1_fp64);
        z3::expr body = z3::fpa_to_fpa(sum_fp64, fp32).mk_to_ieee_bv();
        sub_list.push_back({sme.fpdotadd32to32.name(), body});
    }
    // fpdotadd16to32(acc, a0, a1, b0, b1) = acc + a0 * b0 + a1 * b1
    {
        z3::expr var_acc = mk_bound_var(ctx, 0, bv32);
        z3::expr var_a0  = mk_bound_var(ctx, 1, bv16);
        z3::expr var_a1  = mk_bound_var(ctx, 2, bv16);
        z3::expr var_b0  = mk_bound_var(ctx, 3, bv16);
        z3::expr var_b1  = mk_bound_var(ctx, 4, bv16);

        // ASK: how to do single rounding? widen all to fp64, multiply, sum, round once to fp32?
        z3::expr acc_fp64 = z3::fpa_to_fpa(var_acc.mk_from_ieee_bv(fp32), fp64);
        z3::expr a0_fp64  = z3::fpa_to_fpa(var_a0 .mk_from_ieee_bv(fp16), fp64);
        z3::expr a1_fp64  = z3::fpa_to_fpa(var_a1 .mk_from_ieee_bv(fp16), fp64);
        z3::expr b0_fp64  = z3::fpa_to_fpa(var_b0 .mk_from_ieee_bv(fp16), fp64);
        z3::expr b1_fp64  = z3::fpa_to_fpa(var_b1 .mk_from_ieee_bv(fp16), fp64);

        z3::expr sum_fp64 = acc_fp64 + (a0_fp64 * b0_fp64) + (a1_fp64 * b1_fp64);
        z3::expr body = z3::fpa_to_fpa(sum_fp64, fp32).mk_to_ieee_bv();
        sub_list.push_back({sme.fpdotadd16to32.name(), body});
    }
    // bfdotadd16to32(acc, a0, a1, b0, b1) = acc + a0 * b0 + a1 * b1
    {
        /*
        * Source from PDF Section E2.2 BFDotAdd
        * 13 if !HaveEBF16() || fpcr.EBF == '0' then // Standard BFloat16 behaviors
        * 14    prod = BFAdd(BFMul(op1_a, op2_a), BFMul(op1_b, op2_b)); 
        * 15    result = BFAdd(addend, prod);
        * ASK: assuming EBF (extended bfloat16 behaviors) is not modelled
        */
        z3::expr var_acc = mk_bound_var(ctx, 0, bv32);
        z3::expr var_a0  = mk_bound_var(ctx, 1, bv16);
        z3::expr var_a1  = mk_bound_var(ctx, 2, bv16);
        z3::expr var_b0  = mk_bound_var(ctx, 3, bv16);
        z3::expr var_b1  = mk_bound_var(ctx, 4, bv16);

        // NOTE: a0, a1, b0, b1 are Bfloat16 (upper 16 bits of fp32)
        // widen bf16 -> fp32 by concatenating 16 zero bits in the low half
        auto bf16_to_fp64 = [&](const z3::expr& bf16_bv) -> z3::expr {
            // bf16 IS the upper 16 bits of fp32: high half = bf16, low half = 0
            z3::expr fp32_val = z3::concat(bf16_bv, ctx.bv_val(0, 16)).mk_from_ieee_bv(fp32);
            return z3::fpa_to_fpa(fp32_val, fp64);
        };

        // ASK: how to do single rounding? widen all to fp64, multiply, sum, round once to fp32?
        z3::expr acc_fp64 = z3::fpa_to_fpa(var_acc.mk_from_ieee_bv(fp32), fp64);
        z3::expr a0_fp64  = bf16_to_fp64(var_a0);
        z3::expr a1_fp64  = bf16_to_fp64(var_a1);
        z3::expr b0_fp64  = bf16_to_fp64(var_b0);
        z3::expr b1_fp64  = bf16_to_fp64(var_b1);

        z3::expr sum_fp64 = acc_fp64 + (a0_fp64 * b0_fp64) + (a1_fp64 * b1_fp64);
        z3::expr body = z3::fpa_to_fpa(sum_fp64, fp32).mk_to_ieee_bv();
        sub_list.push_back({sme.bfdotadd16to32.name(), body});
    }

    // pass in the root of the Z3 AST, starting the recursion
    // sub_list is a vector of (name, body) pairs
    return substitute_funs(ast_root, sub_list);
}

thread_local int g_current_failures = 0;

void record_failure(const std::string& msg) {
    std::cerr << " (!) FAIL: " << msg << std::endl;
    g_current_failures++;
}

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
    int spaces = sme.SVL_B * cell_width - 38 - step_len; // pad to align right border
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
           std::function<void(z3::model&, ilang::IlaZ3Unroller&)> verify_fn,
           SubstituteFn sub_fn) {
    std::cout << "\n\n\n=== Test: " << test_name << " ===" << std::endl;
    bool test_passed = true;

    // reset failure count for this test
    g_current_failures = 0;

    // print instruction pipeline (to ensure correct instruction was passed into std::vector)
    std::cout << "  [INSTRS] start --> ";
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

        auto ms = [](auto a, auto b){ return (int)std::chrono::duration_cast<std::chrono::milliseconds>(b-a).count(); };
        using clk = std::chrono::high_resolution_clock;

        // unroll the instruction path FIRST
        auto t_before_unroll = clk::now();
        std::cout << "  [UNROLL] unrolling... ";
        auto tr = u.UnrollPathConn(instrs, 0);
        auto t_after_unroll = clk::now();
        std::cout << "DONE (took " << ms(t_before_unroll,t_after_unroll) << " ms)" << std::endl;

        // optionally replaces UFs with Z3 FPA expressions
        if (sub_fn != nullptr) {
            std::cout << "  [SUB UF] IEEE substitution... ";
            auto t_before_sub = clk::now();
            tr = sub_fn(tr, u, sme, ctx);
            auto t_after_sub = clk::now();
            std::cout << "ENABLED (took " << ms(t_before_sub,t_after_sub) << " ms)" << std::endl;
        }
        std::cout << "  [SUB UF] IEEE substitution disabled (uninterpreted)" << std::endl;
        s.add(tr);

        // call setup lambda to add constraints AFTER unrolling
        setup_fn(u, s, ctx);

        // NOTE: initialize sme.faults to zero before solving
        cstr_step(s, u, ctx, sme.faults, ctx.bv_val(0, sme.faults.bit_width()), 0); // step 0
        
        // set timeout (30 seconds)
        z3::params p(ctx);
        p.set("timeout", (unsigned)30000);
        s.set(p);
        
        // solve
        auto t_before_solve = clk::now();
        std::cout << "  [SOLVER] solving... ";
        auto result = s.check();
        auto t_after_solve = clk::now();
        std::cout << "DONE (took " << ms(t_before_solve,t_after_solve) << " ms)" << std::endl;

        if (result == z3::sat) {

            // call verify lambda with the model
            auto mdl = s.get_model();
            verify_fn(mdl, u);

            // NOTE: ensure no fault occurred throughout execution pipeline
            std::cout << "--- CHECKING FOR FAULTS ---" << std::endl;
            for (size_t step = 0; step < 1 + instr_names.size(); step++) { // extra 1 for result step
                auto got = TO_STR(sme.faults, step, u, mdl);
                auto expected = TO_STR(BvConst(0, sme.faults.bit_width()), step, u, mdl);
                bool fault_found = (got != expected);
                std::cout << "  step: " << step << " faults: " << got << " " << std::endl;
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
