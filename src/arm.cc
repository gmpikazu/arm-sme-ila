#include "arm.h"

namespace arm {
    ArmSme::ArmSme() :
        // NOTE internal states
        m(Ila("arm")),
        za(m.NewMemState("ZA", ZA_ADDR_WIDTH, BYTE)),
        pstate_sm(m.NewBoolState("PSTATE_SM")),
        pstate_za(m.NewBoolState("PSTATE_ZA")),
    
        // NOTE input states
        cmd(m.NewBvState("cmd", TEMP_LARGEST_ADDR_WIDTH)),
        ZAd(m.NewBvState("ZAd", std::log2(SVL_B))),
        HV(m.NewBoolState("HV")),
        Ws(m.NewBvState("Ws", std::log2(32))), // TODO idk bit width
        Imm(m.NewBvState("Imm", TEMP_BIT_WIDTH)), // TODO idk bit width
        Pn(m.NewBvState("Pn", P_ADDR_WIDTH)),
        Pm(m.NewBvState("Pm", P_ADDR_WIDTH)),
        Zn(m.NewBvState("Zn", Z_ADDR_WIDTH))
    {
        // initialize vector registers of length SVL bits
        for (size_t i = 0; i < Z_REG_COUNT; i++) {
            z_regs.push_back(m.NewBvState("z"+std::to_string(i), Z_REG_WIDTH));
        }
        // initialize predicate registers of length SVL_B bits
        for (size_t i = 0; i < P_REG_COUNT; i++) {
            p_regs.push_back(m.NewBvState("p"+std::to_string(i), P_REG_WIDTH));
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
            ExprRef byte_to_place = GetElementInVectorFromMSB(data, i, BYTE, element_size_bits);
            new_mem = Store(new_mem, addr + BvConst(i, ZA_ADDR_WIDTH), byte_to_place);
        }
        return new_mem;
    }

    ExprRef ArmSme::_ToMemoryAddress(const ExprRef& row, const ExprRef& col) {
        return Extract(ZExt(row, ZA_ADDR_WIDTH) * BvConst(SVL_B, ZA_ADDR_WIDTH) + ZExt(col, ZA_ADDR_WIDTH), ZA_ADDR_WIDTH-1, 0);
    }

    // NOTE Topmost row is index 0
    ExprRef ArmSme::_GetTypedHorizontalSlice(const ExprRef& row_idx, const ExprRef& tile_idx, const NumericType& element_size_bits) {
        // dim x dim elements in tile
        NumericType dim = SVL / element_size_bits;
        NumericType num_tiles = SVL_B / dim;

        // ARM SME: row_idx % dim (required by ARM)
        // NOTE log2(dim=1) produces 0, needs separate handling
        ExprRef wrapped_row_idx = (dim == 1) ? BvConst(0, ZA_ADDR_WIDTH) : ZExt(Extract(row_idx, static_cast<int>(std::log2(dim))-1, 0), ZA_ADDR_WIDTH);
        ExprRef row = ZExt(tile_idx, ZA_ADDR_WIDTH) + wrapped_row_idx * BvConst(num_tiles, ZA_ADDR_WIDTH);

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
        // NOTE log2(dim=1) produces 0, needs separate handling
        ExprRef wrapped_col_idx = (dim == 1) ? BvConst(0, ZA_ADDR_WIDTH) : ZExt(Extract(col_idx, static_cast<int>(std::log2(dim))-1, 0), ZA_ADDR_WIDTH);
        ExprRef col = (BvConst(SVL_B, ZA_ADDR_WIDTH) - element_size_bytes) - (wrapped_col_idx * element_size_bytes); // (SVL_B - element_size_bytes) gives the column index of the rightmost element, (col_idx * element_size_bytes) moves back col_idx times
        
        ExprRef slice = _GetElementAtAddress(_ToMemoryAddress(tile_idx, col), element_size_bits); // first row

        // SVL / element_size_bits gives num_cols which equals num_rows (square)
        for (size_t i = 1; i < dim; i++) { // so loops all rows of this tile
            // Section B2.3.4 concat order: Topmost element is index 0
            slice = Concat(_GetElementAtAddress(_ToMemoryAddress(ZExt(tile_idx, ZA_ADDR_WIDTH) + BvConst(i * num_tiles, ZA_ADDR_WIDTH), col), element_size_bits), slice);
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
        // NOTE log2(dim=1) produces 0, needs separate handling
        ExprRef wrapped_row_idx = (dim == 1) ? BvConst(0, ZA_ADDR_WIDTH) : ZExt(Extract(row_idx, static_cast<int>(std::log2(dim))-1, 0), ZA_ADDR_WIDTH);
        ExprRef row = ZExt(tile_idx, ZA_ADDR_WIDTH) + wrapped_row_idx * BvConst(num_tiles, ZA_ADDR_WIDTH);

        // accumulates new_mem = Updated(new_mem)
        ExprRef new_mem = za;
        for (size_t i = 0; i < SVL_B; i++){
            new_mem = _SetByte(new_mem, _ToMemoryAddress(row, BvConst(i, ZA_ADDR_WIDTH)), GetElementInVectorFromMSB(data, i, BYTE, Z_REG_WIDTH));
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
        // NOTE log2(dim=1) produces 0, needs separate handling
        ExprRef wrapped_col_idx = (dim == 1) ? BvConst(0, ZA_ADDR_WIDTH) : ZExt(Extract(col_idx, static_cast<int>(std::log2(dim))-1, 0), ZA_ADDR_WIDTH);
        ExprRef col = (BvConst(SVL_B, ZA_ADDR_WIDTH) - element_size_bytes) - (wrapped_col_idx * element_size_bytes); // (SVL_B - element_size_bytes) gives the column index of the rightmost element, (col_idx * element_size_bytes) moves back col_idx times
        
        // accumulates new_mem = Updated(new_mem)
        ExprRef new_mem = _SetElementAtAddress(za, _ToMemoryAddress(tile_idx, col), element_size_bits, GetElementInVectorFromLSB(data, 0, element_size_bits)); // first row
        // SVL / element_size_bits gives num_cols which equals num_rows (square)
        for (size_t i = 1; i < dim; i++) { // so loops all rows of this tile
            // Section B2.3.4 concat order: Topmost element is index 0
            new_mem = _SetElementAtAddress(new_mem, _ToMemoryAddress(ZExt(tile_idx, ZA_ADDR_WIDTH) + BvConst(i * num_tiles, ZA_ADDR_WIDTH), col), element_size_bits, GetElementInVectorFromLSB(data, i, element_size_bits));
        }
        return new_mem;
    }
    
    ExprRef ArmSme::SetTypedSlice(const ExprRef& is_vertical, const ExprRef& tile_idx, const ExprRef& slice_idx, const NumericType& element_size_bits, const ExprRef& data) {
        return Ite(is_vertical, _SetTypedVerticalSlice(slice_idx, tile_idx, element_size_bits, data), _SetTypedHorizontalSlice(slice_idx, tile_idx, element_size_bits, data));
    }
    
    ExprRef ArmSme::Concatenate(const std::vector<ExprRef> elements) {
        if (elements.empty()) throw std::runtime_error("Concatenate(): elements is empty");

        ExprRef expr = elements[0];
        for (size_t i = 1; i < elements.size(); i++){
            expr = Concat(expr, elements[i]);
        }
        return expr;
    }

    ExprRef ArmSme::GetElementInVectorFromLSB(const ExprRef& vector, const NumericType& idx, const NumericType& element_size_bits) {
        NumericType rightmost_idx = idx * element_size_bits;
        return Extract(vector, rightmost_idx + element_size_bits - 1, rightmost_idx);
    }

    ExprRef ArmSme::GetElementInVectorFromMSB(const ExprRef& vector, const NumericType& idx, const NumericType& element_size_bits, const NumericType& vector_length_bits) {
        NumericType mirrored_idx = (vector_length_bits / element_size_bits) - 1 - idx;
        return GetElementInVectorFromLSB(vector, mirrored_idx, element_size_bits);
    }
    
    ExprRef ArmSme::GetBitFromLSB(const ExprRef& vector, const NumericType& idx) {
        return GetElementInVectorFromLSB(vector, idx, 1);
    }

    ExprRef ArmSme::SetElementInVectorFromLSB(const ExprRef& vector, const NumericType& idx, const NumericType& element_size_bits, const ExprRef& new_element, const NumericType& vector_length_bits) {
        NumericType num_elements = vector_length_bits / element_size_bits;
        if (num_elements == 0) throw std::runtime_error("SetElementInVectorFromLSB(): received no elements, possibly by integer division of input");

        // NOTE edge case where left or right would be an invalid extraction
        if (num_elements == 1) { // single element fills the entire vector
            return new_element;
        }

        NumericType rightmost_idx = idx * element_size_bits;
        if (idx == num_elements-1){ // leftmost
            ExprRef right = Extract(vector, rightmost_idx - 1, 0);
            return Concatenate({new_element, right});
        }
        else if (idx == 0) { // rightmost
            ExprRef left = Extract(vector, vector_length_bits-1, rightmost_idx + element_size_bits);
            return Concatenate({left, new_element});
        }
        else { // middle case
            ExprRef left = Extract(vector, vector_length_bits-1, rightmost_idx + element_size_bits);
            ExprRef right = Extract(vector, rightmost_idx - 1, 0);
            return Concatenate({left, new_element, right});
        }
    }

    ExprRef ArmSme::SetElementInVectorFromMSB(const ExprRef& vector, const NumericType& idx, const NumericType& element_size_bits, const ExprRef& new_element, const NumericType& vector_length_bits) {
        NumericType num_elements = vector_length_bits / element_size_bits;
        if (num_elements == 0) throw std::runtime_error("SetElementInVectorFromMSB(): received no elements, possibly by integer division of input");

        NumericType mirrored_idx = num_elements - 1 - idx;
        return SetElementInVectorFromLSB(vector, mirrored_idx, element_size_bits, new_element, vector_length_bits);
    }

    ExprRef ArmSme::ReadVectorRegister(const ExprRef& z_idx) {
        ExprRef expr = z_regs[0];
        for (size_t i = 1; i < Z_REG_COUNT; i++){
            expr = Ite(z_idx == BvConst(i, Z_ADDR_WIDTH), z_regs[i], expr);
        }
        return expr;
    }
    
    ExprRef ArmSme::ReadPredicateRegister(const ExprRef& p_idx) {
        ExprRef expr = p_regs[0];
        for (size_t i = 1; i < P_REG_COUNT; i++){
            expr = Ite(p_idx == BvConst(i, P_ADDR_WIDTH), p_regs[i], expr);
        }
        return expr;
    }
    
    void ArmSme::WriteVectorRegister(InstrRef& instr, const ExprRef& z_idx, const ExprRef& val) {
        if (val.bit_width() != Z_REG_WIDTH) throw std::runtime_error("WriteVectorRegister(): val's bit-width must match Z_REG_WIDTH");
        for (size_t i = 0; i < Z_REG_COUNT; i++){
            instr.SetUpdate(z_regs[i], Ite(z_idx == BvConst(i, Z_ADDR_WIDTH), val, z_regs[i]));
        }
    }
    
    void ArmSme::WritePredicateRegister(InstrRef& instr, const ExprRef& p_idx, const ExprRef& val) {
        if (val.bit_width() != P_REG_WIDTH) throw std::runtime_error("WritePredicateRegister(): val's bit-width must match P_REG_WIDTH");
        for (size_t i = 0; i < P_REG_COUNT; i++){
            instr.SetUpdate(p_regs[i], Ite(p_idx == BvConst(i, P_ADDR_WIDTH), val, p_regs[i]));
        }
    }
    
    ExprRef ArmSme::BaseRegPlusImm(const ExprRef& base_reg, const ExprRef& imm) {
        return ZExt(base_reg, TEMP_LARGEST_ADDR_WIDTH) + ZExt(imm, TEMP_LARGEST_ADDR_WIDTH);
    }
    
    ExprRef ArmSme::MaskWithSinglePredicate(const ExprRef& source, const ExprRef& dest, const NumericType& element_size_bits, const NumericType& vector_length_bits, const ExprRef& predicate, const ExprRef& is_zero_mode) {
        if (source.bit_width() != dest.bit_width()) throw std::runtime_error("MaskWithSinglePredicate(): source and dest must have same bit-width");
        if (source.bit_width() != vector_length_bits) throw std::runtime_error("MaskWithSinglePredicate(): bit-width must equal vector_length_bits");

        NumericType num_elements = vector_length_bits / element_size_bits;
        // move from least-significant element to most-significant element
        ExprRef result = dest;
        for (size_t i = 0; i < num_elements; i++){
            ExprRef source_element = GetElementInVectorFromLSB(source, i, element_size_bits);
            ExprRef dest_element = GetElementInVectorFromLSB(dest, i, element_size_bits);
            ExprRef is_activated = (GetBitFromLSB(predicate, i) != 0);
            ExprRef new_element = Ite(is_activated, source_element, Ite(
                is_zero_mode, BvConst(0, element_size_bits), dest_element
            ));
            result = SetElementInVectorFromLSB(result, i, element_size_bits, new_element, vector_length_bits);
        }
        return result;
    }
    
    // NOTE esize is BUILT-INTO the instruction using .B .H .W .D .Q suffixes
    void ArmSme::AddInstructions() {
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
        
        // NOTE instructions below requires Streaming SVE mode
        ExprRef SME_ON = pstate_sm & pstate_za;
        // ASK since MOV is alias to MOVA, maybe no need to implement
        { // MOVA (tile to vector)
            auto f = [&](NumericType opcode, NumericType esize, std::string suffix){
                InstrRef instr = m.NewInstr("MOVA_T2V."+suffix);
                auto decode = SME_ON & (cmd == opcode);
                instr.SetDecode(decode);

                auto slice_idx = BaseRegPlusImm(Ws, Imm);
                auto source = GetTypedSlice(HV, ZAd, slice_idx, esize);
                auto dest = ReadVectorRegister(Zn);
                auto masked = MaskWithSinglePredicate(source, dest, esize, SVL, ReadPredicateRegister(Pn), BoolConst(false));
                WriteVectorRegister(instr, Zn, masked);
            };
            f(TEMP_OPCODE, BYTE, "B");
            f(TEMP_OPCODE, HALF, "H");
            f(TEMP_OPCODE, WORD, "S");
            f(TEMP_OPCODE, DOUBLE, "D");
            f(TEMP_OPCODE, QUAD, "Q");
        }
        { // MOVA (vector to tile)
            {
                InstrRef instr = m.NewInstr("MOVA_V2T.B");
                auto decode = SME_ON & TEMP_DECODE;
                instr.SetDecode(decode);
                
                auto slice_idx = BvConst(1, ZA_ADDR_WIDTH);//BaseRegPlusImm(Ws, Imm);
                auto source = ReadVectorRegister(Zn);
                auto dest = GetTypedSlice(HV, ZAd, slice_idx, BYTE);
                auto masked = MaskWithSinglePredicate(source, dest, BYTE, SVL, ReadPredicateRegister(Pn), BoolConst(false));
                auto new_za = SetTypedSlice(HV, ZAd, slice_idx, BYTE, masked);
                instr.SetUpdate(za, new_za);
            }
        }
    }

}  // namespace arm
