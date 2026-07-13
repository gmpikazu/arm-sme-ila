#pragma once

#include <ilang/ilang++.h>
#include <cstdint>
#include <vector>
#include <cmath>

namespace arm {
    
    // ASK need to change all usage sites
    #define TEMP_DECODE BoolConst(true)

    typedef uint64_t NumericType; // same as ilang++.h

    #define BYTE 8
    #define HALF 16
    #define WORD 32
    #define DOUBLE 64
    #define QUAD 128

    constexpr NumericType SVL = 128;
    constexpr NumericType SVL_B = SVL / BYTE;
    constexpr NumericType ZA_BYTE_SIZE = SVL_B * SVL_B;
    const NumericType ZA_ADDR_WIDTH = std::log2(ZA_BYTE_SIZE);
    
using namespace ilang;

class ArmSme {
    Ila m;
    ExprRef za;
    ExprRef pstate_sm;
    ExprRef pstate_za;
    std::vector<ExprRef> z_regs;
    std::vector<ExprRef> p_regs;

    void AddInstructions();

    // @brief Load concatenated multi-byte value from ZA memory
    // @param[in] addr must be aligned to BYTE, HALF, WORD, DOUBLE, QUAD
    ExprRef _GetByte(const ExprRef& addr);
    ExprRef _GetHalf(const ExprRef& addr);
    ExprRef _GetWord(const ExprRef& addr);
    ExprRef _GetDouble(const ExprRef& addr);
    ExprRef _GetQuad(const ExprRef& addr);
    ExprRef _GetElement(const ExprRef& addr, const NumericType& element_size_bits);
    
    
    
    // @return ZA memory linear address
    // param[in] row, col should be ZA_ADDR_WIDTH-wide
    ExprRef _ToMemoryAddress(const ExprRef& row, const ExprRef& col);

    // @brief will be called by more sophisticated ZA access methods
    // @return Concatenated bytes into a single slice
    // @param[in] tile_idx is the tile we want to access, range [0-SVL_B]
    // NOTE ZA[N] equivalent to ZA0.B[N] but without Byte element interpretation
    // TODO tile_idx MUST BE in range [0, SVL_B-1] during instr decode: WE ACHIEVE THIS BY RESTRICTING THE BIT WIDTH OF THE tile_idx INPUT
    ExprRef _GetTypedHorizontalSlice(const ExprRef& row_idx, const ExprRef& tile_idx, const NumericType& element_size_bits); // topmost row is index 0
    ExprRef _GetTypedVerticalSlice(const ExprRef& col_idx, const ExprRef& tile_idx, const NumericType& element_size_bits); // rightmost col is index 0

    // @param[in] is_vertical takes BoolConst(true) or BoolConst(false)
    ExprRef GetTypedSlice(const ExprRef& is_vertical, const ExprRef& tile_idx, const ExprRef& slice_idx, const NumericType& element_size_bits);
    // TODO In SME instructions the tile slice is selected by the sum of a 32-bit general-purpose register (slice index register Ws) and an immediate, modulo the number of slices in the named tile.
    
    // @brief extracts element from slice [MSB...LSB], rightmost element is index 0
    ExprRef ExtractElement(const ExprRef& vector, const NumericType& idx, const NumericType& element_size_bits);
public:
    ArmSme();
};

}  // namespace arm