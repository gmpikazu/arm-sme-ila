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

    #define TEMP_BIT_WIDTH 0 // TODO CHANGE THIS LATER

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

    // Input states

    ExprRef Opcode;

    // <pstatefield> is encoded in the following
    ExprRef Op1;
    ExprRef Op2;
    ExprRef CRm;
    
    ExprRef ZAda;
    ExprRef ZAt;

    ExprRef V;
    ExprRef I1;

    ExprRef Pm;
    ExprRef Pn;
    ExprRef Pd; 
    
    ExprRef Zn;
    ExprRef Zm;
    ExprRef Zd;

    ExprRef Rs;
    ExprRef Rd;
    ExprRef Rn;
    ExprRef Rm;
    ExprRef Rv;

    // ExprRef Imm (not sure if actual field)
    ExprRef Imm2;
    ExprRef Imm3;
    ExprRef Imm4;
    ExprRef Imm6;
    ExprRef Imm8;

    ExprRef Pg;

    // <T> is encoded in tszh and tszl or size
    ExprRef Tszh;
    ExprRef Tszl;
    ExprRef Size;

    void AddInstructions();

    // TODO should've made GetAtRowCol() that uses _ToMemoryAddress() internally
    // @brief Load single element from ZA memory
    // @param[in] addr must be aligned to BYTE, HALF, WORD, DOUBLE, QUAD
    ExprRef _GetByte(const ExprRef& addr);
    ExprRef _GetHalf(const ExprRef& addr);
    ExprRef _GetWord(const ExprRef& addr);
    ExprRef _GetDouble(const ExprRef& addr);
    ExprRef _GetQuad(const ExprRef& addr);
    ExprRef _GetElementAtAddress(const ExprRef& addr, const NumericType& element_size_bits);
    
    // TODO should've made SetAtRowCol() that uses _ToMemoryAddress() internally
    // @brief Store single element to ZA memory (does NOT store a whole vector)
    // @param[in] addr must be aligned to BYTE, HALF, WORD, DOUBLE, QUAD
    // @return New memory state
    ExprRef _SetByte(const ExprRef& mem, const ExprRef& addr, const ExprRef& data);
    ExprRef _SetHalf(const ExprRef& mem, const ExprRef& addr, const ExprRef& data);
    ExprRef _SetWord(const ExprRef& mem, const ExprRef& addr, const ExprRef& data);
    ExprRef _SetDouble(const ExprRef& mem, const ExprRef& addr, const ExprRef& data);
    ExprRef _SetQuad(const ExprRef& mem, const ExprRef& addr, const ExprRef& data);
    ExprRef _SetElementAtAddress(const ExprRef& mem, const ExprRef& addr, const NumericType&element_size_bits, const ExprRef& data);

    // @return ZA memory linear address
    // param[in] row, col must be ZA_ADDR_WIDTH-wide
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
    
    // @return New memory state
    // NOTE ZA[N] equivalent to ZA0.B[N] but without Byte element interpretation
    // @param[in] element_size_bits of each vector element 
    ExprRef _SetTypedHorizontalSlice(const ExprRef& row_idx, const ExprRef& tile_idx, const NumericType& element_size_bits, const ExprRef& data); // topmost row is index 0
    ExprRef _SetTypedVerticalSlice(const ExprRef& col_idx, const ExprRef& tile_idx, const NumericType& element_size_bits, const ExprRef& data); // rightmost col is index 0

    // @param[in] is_vertical takes BoolConst(true) or BoolConst(false)
    ExprRef SetTypedSlice(const ExprRef& is_vertical, const ExprRef& tile_idx, const ExprRef& slice_idx, const NumericType& element_size_bits, const ExprRef& data);
    // TODO In SME instructions the tile slice is selected by the sum of a 32-bit general-purpose register (slice index register Ws) and an immediate, modulo the number of slices in the named tile.

    // NOTE vector length defaults to SVL-bits, but can override to any bit-length
    // @return Extracted element_size_bits-width element from vector slice [MSB...LSB]
    ExprRef VectorElementAtIndexFromLSB(const ExprRef& vector, const NumericType& idx, const NumericType& element_size_bits); // rightmost element is index 0
    // NOTE supports vector_length_bits parameter to mirror vector of any length
    ExprRef VectorElementAtIndexFromMSB(const ExprRef& vector, const NumericType& idx, const NumericType& element_size_bits, const NumericType& vector_length_bits=SVL); // leftmost element is index 0
public:
    ArmSme();
};

}  // namespace arm