#pragma once

#include <ilang/ilang++.h>
#include <cstdint>
#include <vector>
#include <cmath>

namespace arm {
    
    // TODO need to change all usage sites
    #define TEMP_DECODE BoolConst(true)
    #define TEMP_OPCODE 0x01
    #define TEMP_BIT_WIDTH 128
    #define TEMP_LARGEST_ADDR_WIDTH 256

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
    constexpr NumericType Z_REG_COUNT = 32;
    const NumericType Z_ADDR_WIDTH = std::log2(Z_REG_COUNT);
    const NumericType Z_REG_WIDTH = SVL;
    constexpr NumericType P_REG_COUNT = 8;
    // ASK is it truly SVL_B bits? 8 or 16 p-registers for SME??
    // ASK are they indexed from LSB or MSB?
    const NumericType P_ADDR_WIDTH = std::log2(P_REG_COUNT);
    const NumericType P_REG_WIDTH = SVL_B;
    
using namespace ilang;

class ArmSme {
    Ila m;
    ExprRef za;
    ExprRef pstate_sm;
    ExprRef pstate_za;
    std::vector<ExprRef> z_regs;
    std::vector<ExprRef> p_regs;
    // X registers (64 bit)
    // W registers (32 bit)
    
    // NOTE Input States and Alias Names
    ExprRef cmd; // TODO for opcode
    ExprRef ZAd; // tile_idx
    ExprRef HV; // horizontal BoolConst(false), vertical BoolConst(true)
    ExprRef Ws; // TODO 32 bit name of the base register
    ExprRef Imm; // TODO constrained immediate offset of Ws (NEED CHANGE)
    ExprRef Pn; // first predicate register name (overloaded for Pg)
    ExprRef Pm; // second predicate register name
    ExprRef Zn; // first vector register name
    ExprRef imm8; // ASK used as <mask> by ZERO instruction

    void AddInstructions();

    // TODO should've made GetAtRowCol() that uses _ToMemoryAddress() internally
    // @brief Load single element from memory
    // @param[in] mem the memory state to read from
    // @param[in] addr must be aligned to BYTE, HALF, WORD, DOUBLE, QUAD
    ExprRef _GetByte(const ExprRef& mem, const ExprRef& addr);
    ExprRef _GetHalf(const ExprRef& mem, const ExprRef& addr);
    ExprRef _GetWord(const ExprRef& mem, const ExprRef& addr);
    ExprRef _GetDouble(const ExprRef& mem, const ExprRef& addr);
    ExprRef _GetQuad(const ExprRef& mem, const ExprRef& addr);
    ExprRef _GetElementAtAddress(const ExprRef& mem, const ExprRef& addr, const NumericType& element_size_bits);
    
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
    // TODO tile_idx MUST BE in range [0, SVL_B-1] during instr decode: WE ACHIEVE THIS BY RESTRICTING THE BIT WIDTH OF THE tile_idx INPUT
    ExprRef _GetTypedHorizontalSlice(const ExprRef& mem, const ExprRef& row_idx, const ExprRef& tile_idx, const NumericType& element_size_bits); // topmost row is index 0
    ExprRef _GetTypedVerticalSlice(const ExprRef& mem, const ExprRef& col_idx, const ExprRef& tile_idx, const NumericType& element_size_bits); // rightmost col is index 0

    // NOTE ZA[N] equivalent to ZA0.B[N] but without Byte element interpretation
    // @param[in] mem the memory state to read from
    // @param[in] is_vertical takes BoolConst(true) or BoolConst(false)
    // ASK bool or BoolConst?
    ExprRef GetTypedSlice(const ExprRef& mem, const ExprRef& is_vertical, const ExprRef& tile_idx, const ExprRef& slice_idx, const NumericType& element_size_bits);
    // TODO In SME instructions the tile slice is selected by the sum of a 32-bit general-purpose register (slice index register Ws) and an immediate, modulo the number of slices in the named tile.
    
    // @return New memory state
    // @param[in] mem the memory state to update
    // @param[in] element_size_bits of each vector element 
    ExprRef _SetTypedHorizontalSlice(const ExprRef& mem, const ExprRef& row_idx, const ExprRef& tile_idx, const NumericType& element_size_bits, const ExprRef& data); // topmost row is index 0
    ExprRef _SetTypedVerticalSlice(const ExprRef& mem, const ExprRef& col_idx, const ExprRef& tile_idx, const NumericType& element_size_bits, const ExprRef& data); // rightmost col is index 0

    // NOTE ZA[N] equivalent to ZA0.B[N] but without Byte element interpretation
    // @param[in] is_vertical takes BoolConst(true) or BoolConst(false)
    // ASK bool or BoolConst?
    // BUG InstrUpdateRegister and InstrUpdateSlice should also have those that support updating an array of registers
    void UpdateSingleTypedSlice(InstrRef& instr, const ExprRef& is_vertical, const ExprRef& tile_idx, const ExprRef& slice_idx, const NumericType& element_size_bits, const ExprRef& data);
    // TODO In SME instructions the tile slice is selected by the sum of a 32-bit general-purpose register (slice index register Ws) and an immediate, modulo the number of slices in the named tile.
    
    
    // @brief Concatenates, left-to-right, list of elements into a single bit-vector
    ExprRef Concatenate(const std::vector<ExprRef> elements);
    
    // @return Extracted element_size_bits-width element from vector slice [MSB...LSB]
    ExprRef GetElementInVectorFromLSB(const ExprRef& vector, const NumericType& idx, const NumericType& element_size_bits); // rightmost element is index 0
    // NOTE supports vector_length_bits parameter to mirror vector of any length
    ExprRef GetElementInVectorFromMSB(const ExprRef& vector, const NumericType& idx, const NumericType& element_size_bits, const NumericType& vector_length_bits); // leftmost element is index 0

    // @brief Uses GetElementInVectorFromLSB() to extract bit from vector
    ExprRef GetBitFromLSB(const ExprRef& vector, const NumericType& idx);
    
    // @return New vector where element at idx is replaced with new_element
    ExprRef SetElementInVectorFromLSB(const ExprRef& vector, const NumericType& idx, const NumericType& element_size_bits, const ExprRef& new_element, const NumericType& vector_length_bits); // rightmost element is index 0
    ExprRef SetElementInVectorFromMSB(const ExprRef& vector, const NumericType& idx, const NumericType& element_size_bits, const ExprRef& new_element, const NumericType& vector_length_bits); // leftmost element is index 0
    
    // TODO (z|p)_idx must be constrained to only describe 0-31 (Z), and 0-15 (P) using Z_ADDR_WIDTH and P_ADDR_WIDTH bits
    // @return Vector register or Predicate register bit-vector
    ExprRef GetVectorRegister(const ExprRef& z_idx);
    ExprRef GetPredicateRegister(const ExprRef& p_idx);
    // NOTE make sure val's bit-width matches the target register
    void UpdateSingleVectorRegister(InstrRef& instr, const ExprRef& z_idx, const ExprRef& val);
    void UpdateSinglePredicateRegister(InstrRef& instr, const ExprRef& p_idx, const ExprRef& val);
    
    // TODO make BaseRegPlusImm(Ws, imm) and returns (Ws+imm), passed into our current helpers that does modulo internally (imm range must be constrained depending on the specific instruction too)
    ExprRef BaseRegPlusImm(const ExprRef& base_reg, const ExprRef& imm);
    
    // NOTE source and dest must have same bit-width, predicate from p_regs[p_idx]
    // @brief source replaces dest through a predicate mask
    // @return New expression (derived from source) to replace the entirety of dest
    // param[in] is_zero_mode takes: (defaults to merge_mode)
    // - BoolConst(true) source element inactive => destination element is zeroed
    // - BoolConst(false) source element inactive => destination element unmodified
    // ASK bool or BoolConst?
    ExprRef MaskWithSinglePredicate(const ExprRef& source, const ExprRef& dest, const NumericType& element_size_bits, const NumericType& vector_length_bits, const ExprRef& predicate, const ExprRef& is_zero_mode=BoolConst(false));
    
public:
    ArmSme();
    Ila& get() { return m; }
};

}  // namespace arm