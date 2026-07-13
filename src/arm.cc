#include "arm.h"

/*
 * TODO
 * DOES ILAng use Two's complement integers for all Bv operations?
 * - Set Element Helper
 * - Zero instruction
 * - SMSTART, SMSTOP zeroing behavior
 * 0. Load, Store, Mov (FOCUS)
 *  - MOV first, then LDR/STR external mem
 * 1. Predicate Masking
 * 2. Floating Point Instructions (using Uninterpreted Functions?)
 */

namespace arm {
ArmSme::ArmSme()
    : m(Ila("arm")), za(m.NewMemState("ZA", ZA_ADDR_WIDTH, BYTE)),
      pstate_sm(m.NewBoolState("PSTATE_SM")),
      pstate_za(m.NewBoolState("PSTATE_ZA")) {
  // initialize vector registers of length SVL bits
  for (size_t i = 0; i <= 31; i++) {
    z_regs.push_back(m.NewBvState("z" + std::to_string(i), SVL));
  }
  // ASK is it truly SVL_B bits? How are P registers used?
  // initialize predicate registers of length SVL_B bits
  for (size_t i = 0; i <= 15; i++) {
    p_regs.push_back(m.NewBvState("p" + std::to_string(i), SVL_B));
  }

  AddInstructions();
}

// TODO this one is ONLY FOR za, can be updated to take in `const ExprRef& mem`
ExprRef ArmSme::_GetByte(const ExprRef &addr) {
  // ASK I forgot where I read this
  // NOTE addr % SVL_B implemented using Extract()
  return Load(za, Extract(addr, ZA_ADDR_WIDTH - 1, 0));
}
ExprRef ArmSme::_GetHalf(const ExprRef &addr) {
  return Concat(_GetByte(addr), _GetByte(addr + BvConst(1, ZA_ADDR_WIDTH)));
}
ExprRef ArmSme::_GetWord(const ExprRef &addr) {
  return Concat(_GetHalf(addr), _GetHalf(addr + BvConst(2, ZA_ADDR_WIDTH)));
}
ExprRef ArmSme::_GetDouble(const ExprRef &addr) {
  return Concat(_GetWord(addr), _GetWord(addr + BvConst(4, ZA_ADDR_WIDTH)));
}
ExprRef ArmSme::_GetQuad(const ExprRef &addr) {
  return Concat(_GetDouble(addr), _GetDouble(addr + BvConst(8, ZA_ADDR_WIDTH)));
}
ExprRef ArmSme::_GetElement(const ExprRef &addr,
                            const NumericType &element_size_bits) {
  switch (element_size_bits) {
  case BYTE:
    return _GetByte(addr);
  case HALF:
    return _GetHalf(addr);
  case WORD:
    return _GetWord(addr);
  case DOUBLE:
    return _GetDouble(addr);
  case QUAD:
    return _GetQuad(addr);
  default:
    throw std::runtime_error("GetElement(): invalid element size_bits");
  }
}
// TODO SetElement for storing, returns the final memory symbolic expression

ExprRef ArmSme::_ToMemoryAddress(const ExprRef &row, const ExprRef &col) {
  return Extract(row * BvConst(SVL_B, ZA_ADDR_WIDTH) + col, ZA_ADDR_WIDTH - 1,
                 0);
  // ASK do I need to ZExt row and col to ZA_ADDR_WIDTH bits before doing
  // arithmetic?
}

// NOTE Topmost row is index 0
ExprRef ArmSme::_GetTypedHorizontalSlice(const ExprRef &row_idx,
                                         const ExprRef &tile_idx,
                                         const NumericType &element_size_bits) {
  // dim x dim elements in tile
  NumericType dim = SVL / element_size_bits;
  NumericType num_tiles = SVL_B / dim;

  // ARM SME: row_idx % dim (required by ARM)
  ExprRef wrapped_row_idx =
      Extract(row_idx, static_cast<int>(std::log2(dim)) - 1, 0);
  wrapped_row_idx = ZExt(wrapped_row_idx, ZA_ADDR_WIDTH);
  ExprRef row = tile_idx + wrapped_row_idx * BvConst(num_tiles, ZA_ADDR_WIDTH);

  ExprRef slice =
      _GetByte(_ToMemoryAddress(row, BvConst(0, ZA_ADDR_WIDTH))); // first byte
  for (size_t i = 1; i < SVL_B; i++) { // the remaining bytes
    // Section B2.3.3 concat order: Rigtmost element is index 0
    slice = Concat(slice,
                   _GetByte(_ToMemoryAddress(row, BvConst(i, ZA_ADDR_WIDTH))));
  }
  return slice;
}

// NOTE Rightmost col is index 0
ExprRef ArmSme::_GetTypedVerticalSlice(const ExprRef &col_idx,
                                       const ExprRef &tile_idx,
                                       const NumericType &element_size_bits) {
  // dim x dim elements in tile
  NumericType dim = SVL / element_size_bits;
  NumericType num_tiles = SVL_B / dim;
  ExprRef element_size_bytes = BvConst(element_size_bits / BYTE, ZA_ADDR_WIDTH);

  // ARM SME: col_idx % dim (required by ARM)
  ExprRef wrapped_col_idx =
      Extract(col_idx, static_cast<int>(std::log2(dim)) - 1, 0);
  wrapped_col_idx = ZExt(wrapped_col_idx, ZA_ADDR_WIDTH);
  ExprRef col =
      (BvConst(SVL_B, ZA_ADDR_WIDTH) - element_size_bytes) -
      (wrapped_col_idx *
       element_size_bytes); // (SVL_B - element_size_bytes) gives the column
                            // index of the rightmost element, (col_idx *
                            // element_size_bytes) moves back col_idx times

  ExprRef slice = _GetElement(_ToMemoryAddress(tile_idx, col),
                              element_size_bits); // first row

  // SVL / element_size_bits gives num_cols which equals num_rows (square)
  for (size_t i = 1; i < dim; i++) { // so loops all rows of this tile
    // Section B2.3.4 concat order: Topmost element is index 0
    slice = Concat(
        _GetElement(_ToMemoryAddress(
                        tile_idx + BvConst(i * num_tiles, ZA_ADDR_WIDTH), col),
                    element_size_bits),
        slice);
  }
  return slice;
}

ExprRef ArmSme::GetTypedSlice(const ExprRef &is_vertical,
                              const ExprRef &tile_idx, const ExprRef &slice_idx,
                              const NumericType &element_size_bits) {
  return Ite(is_vertical,
             _GetTypedVerticalSlice(slice_idx, tile_idx, element_size_bits),
             _GetTypedHorizontalSlice(slice_idx, tile_idx, element_size_bits));
}

ExprRef ArmSme::ExtractElement(const ExprRef &vector, const NumericType &idx,
                               const NumericType &element_size_bits) {
  NumericType rightmost_idx = idx * element_size_bits;
  return Extract(vector, rightmost_idx + element_size_bits - 1, rightmost_idx);
}

void ArmSme::AddInstructions() {
  { // BUG DUMMY INSTR
    InstrRef instr = m.NewInstr("DUMMY");
    auto decode = TEMP_DECODE;
    instr.SetDecode(decode);
    instr.SetUpdate(
        z_regs[0],
        ExtractElement(GetTypedSlice(BoolConst(true), BvConst(0, ZA_ADDR_WIDTH),
                                     BvConst(0, ZA_ADDR_WIDTH), BYTE),
                       0, BYTE));
  }

  { // SMSTART
    InstrRef instr = m.NewInstr("SMSTART");
    auto decode = TEMP_DECODE;
    instr.SetDecode(decode);
    instr.SetUpdate(pstate_sm, BoolConst(true));
    instr.SetUpdate(pstate_za, BoolConst(true));
    // TODO zero out vector and predicate registers
  }
  { // SMSTOP
    InstrRef instr = m.NewInstr("SMSTOP");
    auto decode = TEMP_DECODE;
    instr.SetDecode(decode);
    instr.SetUpdate(pstate_sm, BoolConst(false));
    instr.SetUpdate(pstate_za, BoolConst(false));
    // TODO zero out vector and predicate registers
    // TODO changing pstate_ZA may zero out the ZA storage (read B1.1.1.2
    // PSTATE.ZA)
  }

  // NOTE instructions below Streaming SVE mode
  ExprRef is_streaming_sve_mode = pstate_sm & pstate_za;
}

} // namespace arm
