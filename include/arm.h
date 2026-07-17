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
    const NumericType LOG2_SVL_B = std::log2(SVL_B);
    constexpr NumericType GPR_COUNT = 31;
    const NumericType GPR_ADDR_WIDTH = std::ceil(std::log2(GPR_COUNT));
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
    
    // TODO these need more thought
    // <pstatefield> is encoded in the following
    // ExprRef Op1;
    // ExprRef Op2;
    // ExprRef CRm;

    // <T> is encoded in tszh and tszl or size
    // ExprRef Tszh;
    // ExprRef Tszl;
    // ExprRef Size;
    
    // NOTE internal states
    ExprRef za;
    ExprRef pstate_sm;
    ExprRef pstate_za;
    std::vector<ExprRef> z_regs;
    std::vector<ExprRef> p_regs; // ASK how many P registers? 8 or 16?

    /** ASK check my understanding
     * @note there are 31 64-bit GPRs (X0-X30)
     * @note W registers (32 bit) are lower bits of X registers (64 bit)
     * @note IMPLEMENTATION: no restriction of which W12-W15 registers to access
     *       model can freely use any GPR during instruction update
     * @note zero extension happens when writing to W registers
     * @note XZR and WZR are zero registers (default value of optional fields)
     * @note 31-th GPR is either SP (64 bit) or XZR / WZR
     */
    ExprRef SP; // 64-bit stack pointer
    std::vector<ExprRef> GPRs; // X0-X30, W0-W30
    ExprRef XZR; // 64-bit zero register
    ExprRef WZR; // 32-bit zero register
    
    // NOTE Input States
    // TODO temporarily use mutually exclusive `cmd` codes for instr select
    ExprRef cmd;

    // Tile Selector
    // NOTE instructions MUST CALL ToConstrainedTileIndex() to get tile_idx
    ExprRef ZAda; // destination tile to accumulate to
    ExprRef ZAn; // source tile to move out of
    ExprRef ZAd; // destination tile to move into
    ExprRef ZAt; // target tile for DRAM load/store
    ExprRef HV; // horizontal BoolConst(false), vertical BoolConst(true)

    // GPR Names
    ExprRef Rs; // 32-bit W register (32 lower bits of X register)
    ExprRef Rv; // 32-bit W register (32 lower bits of X register)
    ExprRef Rn; // 64-bit X register
    ExprRef Rm; // 64-bit X register
    ExprRef Rd; // 64-bit destination register
    
    // Immediates (signed or unsigned depends on instruction interpretation)
    ExprRef Imm;
    // NOTE Imm is widest, all ImmN's below are lower-N-bits of Imm
    ExprRef Imm1; // (also known as i1)
    ExprRef Imm2;
    ExprRef Imm3;
    ExprRef Imm4;
    ExprRef Imm6;
    ExprRef Imm8;
    
    // Predicate Register Names
    ExprRef Pg; // governing
    ExprRef Pd; // destination
    ExprRef Pn; // first source
    ExprRef Pm; // second source
    
    // Scalable Vector Register Names (Z registers)
    ExprRef Zd; // destination
    ExprRef Zn; // first source
    ExprRef Zm; // second source

    void InitUninterpretedFunctions();
    void AddInstructions();

    // NOTE convert tile_idx into constrained range: 0 to num_tiles-1
    ExprRef ToConstrainedTileIndex(const ExprRef& tile_idx, const NumericType& esize);
    
    // NOTE very low-level, does not scale according to element_size_bits
    // @return ZA byte memory linear address
    // param[in] row, col must be ZA_ADDR_WIDTH-wide
    ExprRef _ToByteMemoryAddress(const ExprRef& row, const ExprRef& col);
    
    // @brief Load single element from memory
    // @param[in] mem the memory state to read from
    // @param[in] addr must be aligned to BYTE, HALF, WORD, DOUBLE, QUAD
    ExprRef _GetByte(const ExprRef& mem, const ExprRef& addr);
    ExprRef _GetHalf(const ExprRef& mem, const ExprRef& addr);
    ExprRef _GetWord(const ExprRef& mem, const ExprRef& addr);
    ExprRef _GetDouble(const ExprRef& mem, const ExprRef& addr);
    ExprRef _GetQuad(const ExprRef& mem, const ExprRef& addr);
    ExprRef GetElementAtAddress(const ExprRef& mem, const ExprRef& addr, const NumericType& element_size_bits);
    // TODO should've made GetAtRowCol() that uses _ToMemoryAddress() internally
    ExprRef GetElementAtRowCol(const ExprRef& mem, const ExprRef& tile_idx, const ExprRef& row, const ExprRef& col, const NumericType& element_size_bits);
    
    // @brief Store single element to ZA memory (does NOT store a whole vector)
    // @param[in] addr must be aligned to BYTE, HALF, WORD, DOUBLE, QUAD
    // @return New memory state
    ExprRef _SetByte(const ExprRef& mem, const ExprRef& addr, const ExprRef& data);
    ExprRef _SetHalf(const ExprRef& mem, const ExprRef& addr, const ExprRef& data);
    ExprRef _SetWord(const ExprRef& mem, const ExprRef& addr, const ExprRef& data);
    ExprRef _SetDouble(const ExprRef& mem, const ExprRef& addr, const ExprRef& data);
    ExprRef _SetQuad(const ExprRef& mem, const ExprRef& addr, const ExprRef& data);
    ExprRef SetElementAtAddress(const ExprRef& mem, const ExprRef& addr, const NumericType&element_size_bits, const ExprRef& data);
    // TODO should've made SetAtRowCol() that uses _ToMemoryAddress() internally
    ExprRef SetElementAtRowCol(const ExprRef& mem, const ExprRef& tile_idx, const ExprRef& row, const ExprRef& col, const NumericType& element_size_bits, const ExprRef& data);

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
    // TODO InstrUpdateRegister and InstrUpdateSlice should also have those that support updating an array of registers
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
    
    // NOTE return value and write value must be 32-bit or 64-bit respectively
    // @brief use_sp true if SP is used instead of XZR/WZR as GPR[31]
    ExprRef Get32BitGPR(const ExprRef& w_idx); // for W register access
    ExprRef Get64BitGPR(const ExprRef& x_idx, bool use_sp=false); // for X register access
    void UpdateSingle32BitGPR(InstrRef& instr, const ExprRef& w_idx, const ExprRef& val); // perform zero extension before writing to the whole 64-bit X register
    void UpdateSingle64BitGPR(InstrRef& instr, const ExprRef& x_idx, const ExprRef& val, bool use_sp=false);
    
    // TODO make BaseRegPlusImm(Ws, imm) and returns (Ws+imm), passed into our current helpers that does modulo internally (imm range must be constrained depending on the specific instruction too)
    // NOTE ARM actually does modulo by using Imm1, Imm2, ..., Imm8 derived from Imm
    ExprRef BaseRegPlusImm(const ExprRef& base_reg, const ExprRef& imm);
    
    // NOTE source and dest must have same bit-width, predicate from p_regs[p_idx]
    // @brief source replaces dest through a predicate mask
    // @return New expression (derived from source) to replace the entirety of dest
    // param[in] is_zero_mode takes: (defaults to merge_mode)
    // - BoolConst(true) source element inactive => destination element is zeroed
    // - BoolConst(false) source element inactive => destination element unmodified
    // ASK bool or BoolConst?
    ExprRef MaskWithSinglePredicate(const ExprRef& source, const ExprRef& dest, const NumericType& element_size_bits, const NumericType& vector_length_bits, const ExprRef& predicate, const ExprRef& is_zero_mode=BoolConst(false));

    ExprRef CombineTileWithHorizontalVector(const ExprRef& mem, const ExprRef& tile_idx, const ExprRef& vec, const ExprRef& row_pred, const ExprRef& col_pred, const NumericType& element_size_bits, const ExprRef& is_zero_mode, std::function<ExprRef(ExprRef old, ExprRef extra)> combine_fn);
    ExprRef CombineTileWithVerticalVector(const ExprRef& mem, const ExprRef& tile_idx, const ExprRef& vec, const ExprRef& row_pred, const ExprRef& col_pred, const NumericType& element_size_bits, const ExprRef& is_zero_mode, std::function<ExprRef(ExprRef old, ExprRef extra)> combine_fn);

    // TODO adapt to floating point
    ExprRef CombineTileWithMatrices(const ExprRef& mem, const ExprRef& tile_idx, const ExprRef& vec1, const ExprRef& vec2, const ExprRef& row_pred, const ExprRef& col_pred, const NumericType& element_size_bits, bool sub_instead_of_add, bool op1_unsigned, bool op2_unsigned); // SExt vs ZExt

  public:
    ArmSme();
    Ila& get() { return m; }
};

}  // namespace arm