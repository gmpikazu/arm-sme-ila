#include "arm.h"

/*
 * TODO
 * DOES ILAng use Two's complement integers for all Bv operations?
 * - Set Element Helper
 * - Zero instruction
 * 0. Load, Store, Mov (FOCUS)
 *  - MOV first, then LDR/STR external mem
 * 1. Predicate Masking
 * 2. Floating Point Instructions (using Uninterpreted Functions?)
 */

namespace arm {
    ArmSme::ArmSme() 
    :   m(Ila("arm")),
        za(m.NewMemState("ZA", ZA_ADDR_WIDTH, BYTE)),
        pstate_sm(m.NewBoolState("PSTATE_SM")),
        pstate_za(m.NewBoolState("PSTATE_ZA"))
        Opcode(m.NewBvInput("Opcode",)TEMP_BIT_WIDTH);
        Op1(m.NewBvInput("Op1",)3);
        Op2(m.NewBvInput("Op2",)3);
        CRm(m.NewBvInput("CRm",)4);
        ZAda(m.NewBvInput("ZAda",)TEMP_BIT_WIDTH);
        ZAt(m.NewBvInput("ZAt",)TEMP_BIT_WIDTH);
        V(m.NewBvInput("V",)1);
        I1(m.NewBvInput("I1",)1);
        Pm(m.NewBvInput("Pm",)TEMP_BIT_WIDTH);
        Pn(m.NewBvInput("Pn",)TEMP_BIT_WIDTH);
        Pd(m.NewBvInput("Pd",)TEMP_BIT_WIDTH); 
        Zn(m.NewBvInput("Zn",)TEMP_BIT_WIDTH);
        Zm(m.NewBvInput("Zm",)TEMP_BIT_WIDTH);
        Zd(m.NewBvInput("Zd",)TEMP_BIT_WIDTH);
        Rs(m.NewBvInput("Rs",)TEMP_BIT_WIDTH);
        Rd(m.NewBvInput("Rd",)TEMP_BIT_WIDTH);
        Rn(m.NewBvInput("Rn",)TEMP_BIT_WIDTH);
        Rm(m.NewBvInput("Rm",)TEMP_BIT_WIDTH);
        Rv(m.NewBvInput("Rv",)TEMP_BIT_WIDTH);
        Imm2(m.NewBvInput("Imm2",)2);
        Imm3(m.NewBvInput("Imm3",)3);
        Imm4(m.NewBvInput("Imm4",)4);
        Imm6(m.NewBvInput("Imm6",)6);
        Imm8(m.NewBvInput("Imm8",)8);
        Pg(m.NewBvInput("Pg",)3); // unsure if consistent
        Tszh(m.NewBvInput("Tszh",)1);
        Tszl(m.NewBvInput("Tszl",)3);
        Size(m.NewBvInput("Size",)2);
    {
        // initialize vector registers of length SVL bits
        for (size_t i = 0; i <= 31; i++) {
            z_regs.push_back(m.NewBvState("z"+std::to_string(i), SVL));
        }
        // ASK is it truly SVL_B bits? How are P registers used?
        // initialize predicate registers of length SVL_B bits
        for (size_t i = 0; i <= 15; i++) {
            p_regs.push_back(m.NewBvState("p"+std::to_string(i), SVL_B));
        }

        AddInstructions();
    }
    
    // TODO this one is ONLY FOR za, can be updated to take in `const ExprRef& mem`
    ExprRef ArmSme::_GetByte(const ExprRef& addr) { 
        // ASK I forgot where I read this
        // NOTE addr % SVL_B implemented using Extract()
        return Load(za, Extract(addr, ZA_ADDR_WIDTH-1, 0));
    }
    ExprRef ArmSme::_GetHalf(const ExprRef& addr) { 
        return Concat(_GetByte(addr), _GetByte(addr + BvConst(1, ZA_ADDR_WIDTH)));
    }
    ExprRef ArmSme::_GetWord(const ExprRef& addr) {
        return Concat(_GetHalf(addr), _GetHalf(addr + BvConst(2, ZA_ADDR_WIDTH)));
    }
    ExprRef ArmSme::_GetDouble(const ExprRef& addr) { 
        return Concat(_GetWord(addr), _GetWord(addr + BvConst(4, ZA_ADDR_WIDTH)));
    }
    ExprRef ArmSme::_GetQuad(const ExprRef& addr) { 
        return Concat(_GetDouble(addr), _GetDouble(addr + BvConst(8, ZA_ADDR_WIDTH)));
    }
    ExprRef ArmSme::_GetElementAtAddress(const ExprRef& addr, const NumericType& element_size_bits) {
        switch (element_size_bits) {
            case BYTE: return _GetByte(addr);
            case HALF: return _GetHalf(addr);
            case WORD: return _GetWord(addr);
            case DOUBLE: return _GetDouble(addr);
            case QUAD: return _GetQuad(addr);
            default: throw std::runtime_error("_GetElement(): invalid element size_bits");
        }
    }
    
    // TODO this one is ONLY FOR za, can be updated to take in `CUSTOM_ADDR_WIDTH`
    ExprRef ArmSme::_SetByte(const ExprRef& mem, const ExprRef& addr, const ExprRef& data) {
        return Store(mem, Extract(addr, ZA_ADDR_WIDTH-1, 0), Extract(data, BYTE-1, 0));
    }
    ExprRef ArmSme::_SetElementAtAddress(const ExprRef& mem, const ExprRef& addr, const NumericType& element_size_bits, const ExprRef& data) {
        switch (element_size_bits) {
            case BYTE: case HALF: case WORD: case DOUBLE: case QUAD: break;
            default: throw std::runtime_error("_SetElement(): invalid element size_bits");
        }
        ExprRef new_mem = mem;
        // sequentially places each byte from left to right starting at addr
        for (size_t i = 0; i < (element_size_bits / BYTE); i++){
            ExprRef byte_to_place = VectorElementAtIndexFromMSB(data, i, BYTE, element_size_bits);
            new_mem = Store(new_mem, addr + BvConst(i, ZA_ADDR_WIDTH), byte_to_place);
        }
        return new_mem;
    }

    ExprRef ArmSme::_ToMemoryAddress(const ExprRef& row, const ExprRef& col) {
        return Extract(row * BvConst(SVL_B, ZA_ADDR_WIDTH) + col, ZA_ADDR_WIDTH-1, 0);
        // ASK do I need to ZExt row and col to ZA_ADDR_WIDTH bits before doing arithmetic? 
    }

    // NOTE Topmost row is index 0
    ExprRef ArmSme::_GetTypedHorizontalSlice(const ExprRef& row_idx, const ExprRef& tile_idx, const NumericType& element_size_bits) {
        // dim x dim elements in tile
        NumericType dim = SVL / element_size_bits;
        NumericType num_tiles = SVL_B / dim;

        // ARM SME: row_idx % dim (required by ARM)
        ExprRef wrapped_row_idx = Extract(row_idx, static_cast<int>(std::log2(dim))-1, 0);
        wrapped_row_idx = ZExt(wrapped_row_idx, ZA_ADDR_WIDTH);
        ExprRef row = tile_idx + wrapped_row_idx * BvConst(num_tiles, ZA_ADDR_WIDTH);

        ExprRef slice = _GetByte(_ToMemoryAddress(row, BvConst(0, ZA_ADDR_WIDTH))); // first byte
        for (size_t i = 1; i < SVL_B; i++) { // the remaining bytes
            // Section B2.3.3 concat order: Rigtmost element is index 0
            slice = Concat(slice , _GetByte(_ToMemoryAddress(row, BvConst(i, ZA_ADDR_WIDTH))));
        }
        return slice;
    }
    
    // NOTE Rightmost col is index 0
    ExprRef ArmSme::_GetTypedVerticalSlice(const ExprRef& col_idx, const ExprRef& tile_idx, const NumericType& element_size_bits) {
        // dim x dim elements in tile
        NumericType dim = SVL / element_size_bits;
        NumericType num_tiles = SVL_B / dim;
        ExprRef element_size_bytes = BvConst(element_size_bits / BYTE, ZA_ADDR_WIDTH);
        
        // ARM SME: col_idx % dim (required by ARM)
        ExprRef wrapped_col_idx = Extract(col_idx, static_cast<int>(std::log2(dim))-1, 0);
        wrapped_col_idx = ZExt(wrapped_col_idx, ZA_ADDR_WIDTH);
        ExprRef col = (BvConst(SVL_B, ZA_ADDR_WIDTH) - element_size_bytes) - (wrapped_col_idx * element_size_bytes); // (SVL_B - element_size_bytes) gives the column index of the rightmost element, (col_idx * element_size_bytes) moves back col_idx times
        
        ExprRef slice = _GetElementAtAddress(_ToMemoryAddress(tile_idx, col), element_size_bits); // first row

        // SVL / element_size_bits gives num_cols which equals num_rows (square)
        for (size_t i = 1; i < dim; i++) { // so loops all rows of this tile
            // Section B2.3.4 concat order: Topmost element is index 0
            slice = Concat(_GetElementAtAddress(_ToMemoryAddress(tile_idx + BvConst(i * num_tiles, ZA_ADDR_WIDTH), col), element_size_bits), slice);
        }
        return slice;
    }
    
    ExprRef ArmSme::GetTypedSlice(const ExprRef& is_vertical, const ExprRef& tile_idx, const ExprRef& slice_idx, const NumericType& element_size_bits) {
        return Ite(is_vertical, _GetTypedVerticalSlice(slice_idx, tile_idx, element_size_bits), _GetTypedHorizontalSlice(slice_idx, tile_idx, element_size_bits));
    }
    
    // NOTE Topmost row is index 0
    ExprRef ArmSme::_SetTypedHorizontalSlice(const ExprRef& row_idx, const ExprRef& tile_idx, const NumericType& element_size_bits, const ExprRef& data) {
        // dim x dim elements in tile
        NumericType dim = SVL / element_size_bits;
        NumericType num_tiles = SVL_B / dim;

        // ARM SME: row_idx % dim (required by ARM)
        ExprRef wrapped_row_idx = Extract(row_idx, static_cast<int>(std::log2(dim))-1, 0);
        wrapped_row_idx = ZExt(wrapped_row_idx, ZA_ADDR_WIDTH);
        ExprRef row = tile_idx + wrapped_row_idx * BvConst(num_tiles, ZA_ADDR_WIDTH);

        // accumulates new_mem = Updated(new_mem)
        ExprRef new_mem = za;
        for (size_t i = 0; i < SVL_B; i++){
            new_mem = _SetByte(new_mem, _ToMemoryAddress(row, BvConst(i, ZA_ADDR_WIDTH)), VectorElementAtIndexFromMSB(data, i, BYTE));
        }
        return new_mem;
    }

    // NOTE Rightmost col is index 0
    ExprRef ArmSme::_SetTypedVerticalSlice(const ExprRef& col_idx, const ExprRef& tile_idx, const NumericType& element_size_bits, const ExprRef& data) {
        // dim x dim elements in tile
        NumericType dim = SVL / element_size_bits;
        NumericType num_tiles = SVL_B / dim;
        ExprRef element_size_bytes = BvConst(element_size_bits / BYTE, ZA_ADDR_WIDTH);
        
        // ARM SME: col_idx % dim (required by ARM)
        ExprRef wrapped_col_idx = Extract(col_idx, static_cast<int>(std::log2(dim))-1, 0);
        wrapped_col_idx = ZExt(wrapped_col_idx, ZA_ADDR_WIDTH);
        ExprRef col = (BvConst(SVL_B, ZA_ADDR_WIDTH) - element_size_bytes) - (wrapped_col_idx * element_size_bytes); // (SVL_B - element_size_bytes) gives the column index of the rightmost element, (col_idx * element_size_bytes) moves back col_idx times
        
        // accumulates new_mem = Updated(new_mem)
        ExprRef new_mem = _SetElementAtAddress(za, _ToMemoryAddress(tile_idx, col), element_size_bits, VectorElementAtIndexFromLSB(data, 0, element_size_bits)); // first row
        // SVL / element_size_bits gives num_cols which equals num_rows (square)
        for (size_t i = 1; i < dim; i++) { // so loops all rows of this tile
            // Section B2.3.4 concat order: Topmost element is index 0
            new_mem = _SetElementAtAddress(new_mem, _ToMemoryAddress(tile_idx + BvConst(i * num_tiles, ZA_ADDR_WIDTH), col), element_size_bits, VectorElementAtIndexFromLSB(data, i, element_size_bits));
        }
        return new_mem;
    }
    
    ExprRef ArmSme::SetTypedSlice(const ExprRef& is_vertical, const ExprRef& tile_idx, const ExprRef& slice_idx, const NumericType& element_size_bits, const ExprRef& data) {
        return Ite(is_vertical, _SetTypedVerticalSlice(slice_idx, tile_idx, element_size_bits, data), _SetTypedHorizontalSlice(slice_idx, tile_idx, element_size_bits, data));
    }
    
    ExprRef ArmSme::VectorElementAtIndexFromLSB(const ExprRef& vector, const NumericType& idx, const NumericType& element_size_bits) {
        NumericType rightmost_idx = idx * element_size_bits;
        return Extract(vector, rightmost_idx + element_size_bits - 1, rightmost_idx);
    }

    ExprRef ArmSme::VectorElementAtIndexFromMSB(const ExprRef& vector, const NumericType& idx, const NumericType& element_size_bits, const NumericType& vector_length_bits) {
        NumericType mirrored_idx = (vector_length_bits / element_size_bits) - 1 - idx;
        return VectorElementAtIndexFromLSB(vector, mirrored_idx, element_size_bits);
    }
    
    void ArmSme::AddInstructions() {
        { // BUG DUMMY INSTR
            InstrRef instr = m.NewInstr("DUMMY");
            auto decode = TEMP_DECODE;
            instr.SetDecode(decode);
            instr.SetUpdate(z_regs[0], VectorElementAtIndexFromLSB(GetTypedSlice(BoolConst(true), BvConst(0, ZA_ADDR_WIDTH), BvConst(0, ZA_ADDR_WIDTH), BYTE), 0, BYTE));
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
            // TODO changing pstate_ZA may zero out the ZA storage (read B1.1.1.2 PSTATE.ZA)
            instr.SetUpdate(z_regs[0], GetTypedSlice(BoolConst(true), BvConst(0, ZA_ADDR_WIDTH), BvConst(0, ZA_ADDR_WIDTH), BYTE));
        }
        
        // NOTE instructions below Streaming SVE mode
        ExprRef is_streaming_sve_mode = pstate_sm & pstate_za;
}

}  // namespace arm
