#include "arm.h"

namespace arm {
    ArmSme::ArmSme() :
        m(Ila("arm")),

        // TODO these need more thought
        // Tszh(m.NewBvInput("Tszh",)1);
        // Tszl(m.NewBvInput("Tszl",)3);
        // Size(m.NewBvInput("Size",)2);
        
        // NOTE internal states
        za(m.NewMemState("ZA", ZA_ADDR_WIDTH, BYTE)),
        pstate_sm(m.NewBoolState("PSTATE_SM")),
        pstate_za(m.NewBoolState("PSTATE_ZA")),

        XZR(BvConst(0, 64)),
        WZR(BvConst(0, 32)),

        // NOTE input states
        cmd(m.NewBvInput("cmd", TEMP_LARGEST_ADDR_WIDTH)), // TODO TBC

        ZAda(m.NewBvInput("ZAda", LOG2_SVL_B)),
        ZAn(m.NewBvInput("ZAn", LOG2_SVL_B)),
        ZAd(m.NewBvInput("ZAd", LOG2_SVL_B)),
        ZAt(m.NewBvInput("ZAt", LOG2_SVL_B)),
        HV(m.NewBoolInput("HV")),

        // TODO TBC: check each bit width
        Rs(m.NewBvInput("Rs", GPR_ADDR_WIDTH)),
        Rv(m.NewBvInput("Rv", GPR_ADDR_WIDTH)),
        Rn(m.NewBvInput("Rn", GPR_ADDR_WIDTH)),
        Rm(m.NewBvInput("Rm", GPR_ADDR_WIDTH)),
        Rd(m.NewBvInput("Rd", GPR_ADDR_WIDTH)),
        
        Imm(m.NewBvInput("Imm", 8)), // TODO TBC: what is the maximum bits needed?
        Imm1(Imm(1, 0)), Imm2((Imm(2, 0))),
        Imm3(Imm(3, 0)), Imm4(Imm(4, 0)),
        Imm6(Imm(6, 0)), Imm8(Imm(8, 0)),

        Pg(m.NewBvInput("Pg", P_ADDR_WIDTH)),
        Pd(m.NewBvInput("Pd", P_ADDR_WIDTH)),
        Pn(m.NewBvInput("Pn", P_ADDR_WIDTH)),
        Pm(m.NewBvInput("Pm", P_ADDR_WIDTH)),

        Zd(m.NewBvInput("Zd", Z_ADDR_WIDTH)),
        Zn(m.NewBvInput("Zn", Z_ADDR_WIDTH)),
        Zm(m.NewBvInput("Zm", Z_ADDR_WIDTH))
    {
        assert(Z_REG_WIDTH == SVL);

        // initialize vector registers of length SVL bits
        for (size_t i = 0; i < Z_REG_COUNT; i++) {
            z_regs.push_back(m.NewBvState("z"+std::to_string(i), Z_REG_WIDTH));
        }
        // initialize predicate registers of length SVL_B bits
        for (size_t i = 0; i < P_REG_COUNT; i++) {
            p_regs.push_back(m.NewBvState("p"+std::to_string(i), P_REG_WIDTH));
        }
        // initialize GPRs of length 64 bits
        for (size_t i = 0; i < GPR_COUNT; i++) {
            GPRs.push_back(m.NewBvState("x"+std::to_string(i), 64));
        }

        AddInstructions();
    }
    
    ExprRef ArmSme::ToConstrainedTileIndex(const ExprRef& tile_idx, const NumericType& esize) {
        if (esize == BYTE) return BvConst(1, 1); // edge case: log2(1)=0 fails
        // (1): dim = SVL / esize
        // (2): num_tiles = SVL_B / dim
        // SVL_B / (SVL / esize) = (SVL / 8) * (esize / SVL) = esize / 8
        NumericType num_tiles = esize / BYTE;
        return Extract(tile_idx, std::log2(num_tiles)-1, 0);
    }

    ExprRef ArmSme::_GetByte(const ExprRef& mem, const ExprRef& addr) { 
        return Load(mem, Extract(addr, ZA_ADDR_WIDTH-1, 0));
    }
    ExprRef ArmSme::_GetHalf(const ExprRef& mem, const ExprRef& addr) { 
        return Concat(_GetByte(mem, addr), _GetByte(mem, addr + BvConst(1, ZA_ADDR_WIDTH)));
    }
    ExprRef ArmSme::_GetWord(const ExprRef& mem, const ExprRef& addr) {
        return Concat(_GetHalf(mem, addr), _GetHalf(mem, addr + BvConst(2, ZA_ADDR_WIDTH)));
    }
    ExprRef ArmSme::_GetDouble(const ExprRef& mem, const ExprRef& addr) { 
        return Concat(_GetWord(mem, addr), _GetWord(mem, addr + BvConst(4, ZA_ADDR_WIDTH)));
    }
    ExprRef ArmSme::_GetQuad(const ExprRef& mem, const ExprRef& addr) { 
        return Concat(_GetDouble(mem, addr), _GetDouble(mem, addr + BvConst(8, ZA_ADDR_WIDTH)));
    }
    ExprRef ArmSme::_GetElementAtAddress(const ExprRef& mem, const ExprRef& addr, const NumericType& element_size_bits) {
        switch (element_size_bits) {
            case BYTE: return _GetByte(mem, addr);
            case HALF: return _GetHalf(mem, addr);
            case WORD: return _GetWord(mem, addr);
            case DOUBLE: return _GetDouble(mem, addr);
            case QUAD: return _GetQuad(mem, addr);
            default: throw std::runtime_error("_GetElement(): invalid element size_bits");
        }
    }
    
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
    ExprRef ArmSme::_GetTypedHorizontalSlice(const ExprRef& mem, const ExprRef& row_idx, const ExprRef& tile_idx, const NumericType& element_size_bits) {
        // dim x dim elements in tile
        NumericType dim = SVL / element_size_bits;
        NumericType num_tiles = SVL_B / dim;

        // ARM SME: row_idx % dim (required by ARM)
        // NOTE log2(dim=1) produces 0, needs separate handling
        ExprRef wrapped_row_idx = (dim == 1) ? BvConst(0, ZA_ADDR_WIDTH) : ZExt(Extract(row_idx, static_cast<int>(std::log2(dim))-1, 0), ZA_ADDR_WIDTH);
        ExprRef row = ZExt(tile_idx, ZA_ADDR_WIDTH) + wrapped_row_idx * BvConst(num_tiles, ZA_ADDR_WIDTH);

        ExprRef slice = _GetByte(mem, _ToMemoryAddress(row, BvConst(0, ZA_ADDR_WIDTH))); // first byte
        for (size_t i = 1; i < SVL_B; i++) { // the remaining bytes
            // Section B2.3.3 concat order: Rigtmost element is index 0
            slice = Concat(slice , _GetByte(mem, _ToMemoryAddress(row, BvConst(i, ZA_ADDR_WIDTH))));
        }
        return slice;
    }
    
    // NOTE Rightmost col is index 0
    ExprRef ArmSme::_GetTypedVerticalSlice(const ExprRef& mem, const ExprRef& col_idx, const ExprRef& tile_idx, const NumericType& element_size_bits) {
        // dim x dim elements in tile
        NumericType dim = SVL / element_size_bits;
        NumericType num_tiles = SVL_B / dim;
        ExprRef element_size_bytes = BvConst(element_size_bits / BYTE, ZA_ADDR_WIDTH);
        
        // ARM SME: col_idx % dim (required by ARM)
        // NOTE log2(dim=1) produces 0, needs separate handling
        ExprRef wrapped_col_idx = (dim == 1) ? BvConst(0, ZA_ADDR_WIDTH) : ZExt(Extract(col_idx, static_cast<int>(std::log2(dim))-1, 0), ZA_ADDR_WIDTH);
        ExprRef col = (BvConst(SVL_B, ZA_ADDR_WIDTH) - element_size_bytes) - (wrapped_col_idx * element_size_bytes); // (SVL_B - element_size_bytes) gives the column index of the rightmost element, (col_idx * element_size_bytes) moves back col_idx times
        
        ExprRef slice = _GetElementAtAddress(mem, _ToMemoryAddress(tile_idx, col), element_size_bits); // first row

        // SVL / element_size_bits gives num_cols which equals num_rows (square)
        for (size_t i = 1; i < dim; i++) { // so loops all rows of this tile
            // Section B2.3.4 concat order: Topmost element is index 0
            slice = Concat(_GetElementAtAddress(mem, _ToMemoryAddress(ZExt(tile_idx, ZA_ADDR_WIDTH) + BvConst(i * num_tiles, ZA_ADDR_WIDTH), col), element_size_bits), slice);
        }
        return slice;
    }
    
    ExprRef ArmSme::GetTypedSlice(const ExprRef& mem, const ExprRef& is_vertical, const ExprRef& tile_idx, const ExprRef& slice_idx, const NumericType& element_size_bits) {
        return Ite(is_vertical, _GetTypedVerticalSlice(mem, slice_idx, tile_idx, element_size_bits), _GetTypedHorizontalSlice(mem, slice_idx, tile_idx, element_size_bits));
    }
    
    // NOTE Topmost row is index 0
    ExprRef ArmSme::_SetTypedHorizontalSlice(const ExprRef& mem, const ExprRef& row_idx, const ExprRef& tile_idx, const NumericType& element_size_bits, const ExprRef& data) {
        // dim x dim elements in tile
        NumericType dim = SVL / element_size_bits;
        NumericType num_tiles = SVL_B / dim;

        // ARM SME: row_idx % dim (required by ARM)
        // NOTE log2(dim=1) produces 0, needs separate handling
        ExprRef wrapped_row_idx = (dim == 1) ? BvConst(0, ZA_ADDR_WIDTH) : ZExt(Extract(row_idx, static_cast<int>(std::log2(dim))-1, 0), ZA_ADDR_WIDTH);
        ExprRef row = ZExt(tile_idx, ZA_ADDR_WIDTH) + wrapped_row_idx * BvConst(num_tiles, ZA_ADDR_WIDTH);

        // accumulates new_mem = Updated(new_mem)
        ExprRef new_mem = mem;
        for (size_t i = 0; i < SVL_B; i++){
            new_mem = _SetByte(new_mem, _ToMemoryAddress(row, BvConst(i, ZA_ADDR_WIDTH)), GetElementInVectorFromMSB(data, i, BYTE, Z_REG_WIDTH));
        }
        return new_mem;
    }

    // NOTE Rightmost col is index 0
    ExprRef ArmSme::_SetTypedVerticalSlice(const ExprRef& mem, const ExprRef& col_idx, const ExprRef& tile_idx, const NumericType& element_size_bits, const ExprRef& data) {
        // dim x dim elements in tile
        NumericType dim = SVL / element_size_bits;
        NumericType num_tiles = SVL_B / dim;
        ExprRef element_size_bytes = BvConst(element_size_bits / BYTE, ZA_ADDR_WIDTH);
        
        // ARM SME: col_idx % dim (required by ARM)
        // NOTE log2(dim=1) produces 0, needs separate handling
        ExprRef wrapped_col_idx = (dim == 1) ? BvConst(0, ZA_ADDR_WIDTH) : ZExt(Extract(col_idx, static_cast<int>(std::log2(dim))-1, 0), ZA_ADDR_WIDTH);
        ExprRef col = (BvConst(SVL_B, ZA_ADDR_WIDTH) - element_size_bytes) - (wrapped_col_idx * element_size_bytes); // (SVL_B - element_size_bytes) gives the column index of the rightmost element, (col_idx * element_size_bytes) moves back col_idx times
        
        // accumulates new_mem = Updated(new_mem)
        ExprRef new_mem = _SetElementAtAddress(mem, _ToMemoryAddress(tile_idx, col), element_size_bits, GetElementInVectorFromLSB(data, 0, element_size_bits)); // first row
        // SVL / element_size_bits gives num_cols which equals num_rows (square)
        for (size_t i = 1; i < dim; i++) { // so loops all rows of this tile
            // Section B2.3.4 concat order: Topmost element is index 0
            new_mem = _SetElementAtAddress(new_mem, _ToMemoryAddress(ZExt(tile_idx, ZA_ADDR_WIDTH) + BvConst(i * num_tiles, ZA_ADDR_WIDTH), col), element_size_bits, GetElementInVectorFromLSB(data, i, element_size_bits));
        }
        return new_mem;
    }
    
    void ArmSme::UpdateSingleTypedSlice(InstrRef& instr, const ExprRef& is_vertical, const ExprRef& tile_idx, const ExprRef& slice_idx, const NumericType& element_size_bits, const ExprRef& data) {
        ExprRef new_za = Ite(is_vertical, _SetTypedVerticalSlice(za, slice_idx, tile_idx, element_size_bits, data), _SetTypedHorizontalSlice(za, slice_idx, tile_idx, element_size_bits, data));
        instr.SetUpdate(za, new_za);
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

    ExprRef ArmSme::GetVectorRegister(const ExprRef& z_idx) {
        assert(z_idx.bit_width() <= Z_ADDR_WIDTH);
        ExprRef expr = z_regs[0];
        for (size_t i = 1; i < Z_REG_COUNT; i++){
            expr = Ite(z_idx == i, z_regs[i], expr);
        }
        return expr;
    }
    
    ExprRef ArmSme::GetPredicateRegister(const ExprRef& p_idx) {
        assert(p_idx.bit_width() <= P_ADDR_WIDTH);
        ExprRef expr = p_regs[0];
        for (size_t i = 1; i < P_REG_COUNT; i++){
            expr = Ite(p_idx == i, p_regs[i], expr);
        }
        return expr;
    }
    
    void ArmSme::UpdateSingleVectorRegister(InstrRef& instr, const ExprRef& z_idx, const ExprRef& val) {
        if (val.bit_width() != Z_REG_WIDTH) throw std::runtime_error("UpdateSingleVectorRegister(): val's bit-width must match Z_REG_WIDTH");
        assert(z_idx.bit_width() <= Z_ADDR_WIDTH);
        for (size_t i = 0; i < Z_REG_COUNT; i++){
            instr.SetUpdate(z_regs[i], Ite(z_idx == i, val, z_regs[i]));
        }
    }
    
    void ArmSme::UpdateSinglePredicateRegister(InstrRef& instr, const ExprRef& p_idx, const ExprRef& val) {
        if (val.bit_width() != P_REG_WIDTH) throw std::runtime_error("UpdateSinglePredicateRegister(): val's bit-width must match P_REG_WIDTH");
        assert(p_idx.bit_width() <= P_ADDR_WIDTH);
        for (size_t i = 0; i < P_REG_COUNT; i++){
            instr.SetUpdate(p_regs[i], Ite(p_idx == i, val, p_regs[i]));
        }
    }

    ExprRef ArmSme::Get32BitGPR(const ExprRef& w_idx){
        assert(w_idx.bit_width() <= GPR_ADDR_WIDTH);
        ExprRef expr = Extract(GPRs[0], 31, 0);
        for (size_t i = 1; i < GPR_COUNT; i++){
            expr = Ite(w_idx == i, Extract(GPRs[i], 31, 0), expr);
        }
        return expr;
    }

    ExprRef ArmSme::Get64BitGPR(const ExprRef& x_idx){
        assert(x_idx.bit_width() <= GPR_ADDR_WIDTH);
        ExprRef expr = GPRs[0];
        for (size_t i = 1; i < GPR_COUNT; i++){
            expr = Ite(x_idx == i, GPRs[i], expr);
        }
        return expr;
    }

    void ArmSme::UpdateSingle32BitGPR(InstrRef& instr, const ExprRef& w_idx, const ExprRef& val){
        if (val.bit_width() != 32) throw std::runtime_error("UpdateSingle32BitGPR(): val's bit-width must be 32 bits");
        assert(w_idx.bit_width() <= GPR_ADDR_WIDTH);
        for (size_t i = 0; i < GPR_COUNT; i++){
            instr.SetUpdate(GPRs[i], Ite(w_idx == i, ZExt(val, 64), GPRs[i]));
        }
    }

    void ArmSme::UpdateSingle64BitGPR(InstrRef& instr, const ExprRef& x_idx, const ExprRef& val){
        if (val.bit_width() != 64) throw std::runtime_error("UpdateSingle64BitGPR(): val's bit-width must be 64 bits");
        assert(x_idx.bit_width() <= GPR_ADDR_WIDTH);
        for (size_t i = 0; i < GPR_COUNT; i++){
            instr.SetUpdate(GPRs[i], Ite(x_idx == i, val, GPRs[i]));
        }
    }
    
    ExprRef ArmSme::BaseRegPlusImm(const ExprRef& base_reg_value, const ExprRef& imm) {
        return ZExt(base_reg_value, TEMP_LARGEST_ADDR_WIDTH) + ZExt(imm, TEMP_LARGEST_ADDR_WIDTH);
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

}  // namespace arm
