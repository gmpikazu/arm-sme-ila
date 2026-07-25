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

        SP(m.NewBvState("SP", 64)),
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
        // BUG Imm(0, 0) is not supported, but we need 1-bit
        // maybe can model each Imm as different fields
        // ASK does SelectBit exist? I should've used for step predicate bits then...
        Imm1(SelectBit(Imm, 0)), Imm2((Imm(1, 0))),
        Imm3(Imm(2, 0)), Imm4(Imm(3, 0)),
        Imm6(Imm(5, 0)), Imm8(Imm(7, 0)),

        Pg(m.NewBvInput("Pg", P_ADDR_WIDTH)),
        Pd(m.NewBvInput("Pd", P_ADDR_WIDTH)),
        Pn(m.NewBvInput("Pn", P_ADDR_WIDTH)),
        Pm(m.NewBvInput("Pm", P_ADDR_WIDTH)),

        Zd(m.NewBvInput("Zd", Z_ADDR_WIDTH)),
        Zn(m.NewBvInput("Zn", Z_ADDR_WIDTH)),
        Zm(m.NewBvInput("Zm", Z_ADDR_WIDTH)),
        
        // NOTE Sort Refs
        // ASK bfloat and fp16 should have different format
        bf16(SortRef::BV(16)),
        fp64(SortRef::BV(64)),
        fp32(SortRef::BV(32)),
        fp16(SortRef::BV(16)),

        // ASK bit representation of zero for bfloat
        fp32_zero(BvConst(0, 32)),
        fp16_zero(BvConst(0, 16)), // ASK same value as bf16_zero though
        bf16_zero(BvConst(0, 16)),
        
        // NOTE Uninterpreted Functions
        fpneg64("fpneg64", fp64, fp64),
        fpneg32("fpneg32", fp32, fp32),
        fpneg16("fpneg16", fp16, fp16),
        bfneg16("bfneg16", bf16, bf16),
        // TODO TBC: check the argument sizes below
        fpmac64("fpmac64", fp64, {fp64, fp64, fp64}),
        fpmac32("fpmac32", fp32, {fp32, fp32, fp32}),
        fpdotadd32to32("fpdotadd32to32", fp32, {fp32, fp32, fp32, fp32, fp32}),
        fpdotadd16to32("fpdotadd16to32", fp32, {fp32, fp16, fp16, fp16, fp16}),
        bfdotadd16to32("bfdotadd16to32", fp32, {fp32, bf16, bf16, bf16, bf16})
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
        if (esize == BYTE) return BvConst(0, 1); // edge case: log2(1)=0 fails
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
    ExprRef ArmSme::GetElementAtAddress(const ExprRef& mem, const ExprRef& addr, const NumericType& element_size_bits) {
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
    ExprRef ArmSme::SetElementAtAddress(const ExprRef& mem, const ExprRef& addr, const NumericType& element_size_bits, const ExprRef& data) {
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

    ExprRef ArmSme::_ToByteMemoryAddress(const ExprRef& row, const ExprRef& col) {
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

        ExprRef slice = _GetByte(mem, _ToByteMemoryAddress(row, BvConst(0, ZA_ADDR_WIDTH))); // first byte
        for (size_t i = 1; i < SVL_B; i++) { // the remaining bytes
            // Section B2.3.3 concat order: Rigtmost element is index 0
            slice = Concat(slice , _GetByte(mem, _ToByteMemoryAddress(row, BvConst(i, ZA_ADDR_WIDTH))));
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
        
        ExprRef slice = GetElementAtAddress(mem, _ToByteMemoryAddress(tile_idx, col), element_size_bits); // first row

        // SVL / element_size_bits gives num_cols which equals num_rows (square)
        for (size_t i = 1; i < dim; i++) { // so loops all rows of this tile
            // Section B2.3.4 concat order: Topmost element is index 0
            slice = Concat(GetElementAtAddress(mem, _ToByteMemoryAddress(ZExt(tile_idx, ZA_ADDR_WIDTH) + BvConst(i * num_tiles, ZA_ADDR_WIDTH), col), element_size_bits), slice);
        }
        return slice;
    }
    
    ExprRef ArmSme::GetTypedSlice(const ExprRef& mem, const ExprRef& is_vertical, const ExprRef& tile_idx, const ExprRef& slice_idx, const NumericType& element_size_bits) {
        return Ite(is_vertical, _GetTypedVerticalSlice(mem, slice_idx, tile_idx, element_size_bits), _GetTypedHorizontalSlice(mem, slice_idx, tile_idx, element_size_bits));
    }
    
    // PUBLIC convenience helpers for testing
    ExprRef ArmSme::GetHorizontalSlice(const ExprRef& mem, int tile_idx, int row_idx, const NumericType& element_size_bits) {
        return GetTypedSlice(mem, BoolConst(false), BvConst(tile_idx, ZA_ADDR_WIDTH), BvConst(row_idx, ZA_ADDR_WIDTH), element_size_bits);
    }
    
    ExprRef ArmSme::GetVerticalSlice(const ExprRef& mem, int tile_idx, int col_idx, const NumericType& element_size_bits) {
        return GetTypedSlice(mem, BoolConst(true), BvConst(tile_idx, ZA_ADDR_WIDTH), BvConst(col_idx, ZA_ADDR_WIDTH), element_size_bits);
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
            new_mem = _SetByte(new_mem, _ToByteMemoryAddress(row, BvConst(i, ZA_ADDR_WIDTH)), GetElementInVectorFromMSB(data, i, BYTE, Z_REG_WIDTH));
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
        ExprRef new_mem = SetElementAtAddress(mem, _ToByteMemoryAddress(tile_idx, col), element_size_bits, GetElementInVectorFromLSB(data, 0, element_size_bits)); // first row
        // SVL / element_size_bits gives num_cols which equals num_rows (square)
        for (size_t i = 1; i < dim; i++) { // so loops all rows of this tile
            // Section B2.3.4 concat order: Topmost element is index 0
            new_mem = SetElementAtAddress(new_mem, _ToByteMemoryAddress(ZExt(tile_idx, ZA_ADDR_WIDTH) + BvConst(i * num_tiles, ZA_ADDR_WIDTH), col), element_size_bits, GetElementInVectorFromLSB(data, i, element_size_bits));
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
        // TODO could've used Ilang's GetBit function
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
        expr = Ite(w_idx == 31, WZR, expr);
        return expr;
    }

    ExprRef ArmSme::Get64BitGPR(const ExprRef& x_idx, bool use_sp){
        assert(x_idx.bit_width() <= GPR_ADDR_WIDTH);
        ExprRef expr = GPRs[0];
        for (size_t i = 1; i < GPR_COUNT; i++){
            expr = Ite(x_idx == i, GPRs[i], expr);
        }
        expr = use_sp ? Ite(x_idx == 31, SP, expr) : Ite(x_idx == 31, XZR, expr);
        return expr;
    }

    void ArmSme::UpdateSingle32BitGPR(InstrRef& instr, const ExprRef& w_idx, const ExprRef& val){
        if (val.bit_width() != 32) throw std::runtime_error("UpdateSingle32BitGPR(): val's bit-width must be 32 bits");
        assert(w_idx.bit_width() <= GPR_ADDR_WIDTH);
        for (size_t i = 0; i < GPR_COUNT; i++){
            instr.SetUpdate(GPRs[i], Ite(w_idx == i, ZExt(val, 64), GPRs[i]));
        }
        // if w_idx == 31: WZR naturally disregarded
    }

    void ArmSme::UpdateSingle64BitGPR(InstrRef& instr, const ExprRef& x_idx, const ExprRef& val, bool use_sp){
        if (val.bit_width() != 64) throw std::runtime_error("UpdateSingle64BitGPR(): val's bit-width must be 64 bits");
        assert(x_idx.bit_width() <= GPR_ADDR_WIDTH);
        for (size_t i = 0; i < GPR_COUNT; i++){
            instr.SetUpdate(GPRs[i], Ite(x_idx == i, val, GPRs[i]));
        }
        if (use_sp) instr.SetUpdate(SP, Ite(x_idx == 31, val, SP));
        // if !use_sp && x_idx == 31: XZR naturally disregarded
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
    
    ExprRef ArmSme::CombineTileWithHorizontalVector(const ExprRef& mem, const ExprRef& tile_idx, const ExprRef& vec, const ExprRef& row_pred, const ExprRef& col_pred, const NumericType& element_size_bits, const ExprRef& is_zero_mode, std::function<ExprRef(ExprRef a, ExprRef b)> combine_fn) {
        assert(row_pred.bit_width() == col_pred.bit_width());
        assert(row_pred.bit_width() == P_REG_WIDTH);
    
        NumericType dim = Z_REG_WIDTH / element_size_bits;
        
        auto new_mem = mem;
        for (size_t row = 0; row < dim; row++){
            auto hor_slice = _GetTypedHorizontalSlice(new_mem, BvConst(row, ZA_ADDR_WIDTH), tile_idx, element_size_bits);
            for (size_t col = 0; col < dim; col++){
                auto old_elem = GetElementInVectorFromLSB(hor_slice, col, element_size_bits);
                auto extra_elem = GetElementInVectorFromLSB(vec, col, element_size_bits);
                ExprRef row_col_activated = (GetBitFromLSB(row_pred, row) != 0) & (GetBitFromLSB(col_pred, col) != 0);
                auto new_elem = Ite(row_col_activated, combine_fn(old_elem, extra_elem), Ite(is_zero_mode, BvConst(0, element_size_bits), old_elem));
                hor_slice = SetElementInVectorFromLSB(hor_slice, col, element_size_bits, new_elem, Z_REG_WIDTH);
            }
            // update the entire horizontal slice
            new_mem = _SetTypedHorizontalSlice(new_mem, BvConst(row, ZA_ADDR_WIDTH), tile_idx, element_size_bits, hor_slice);
        }
        return new_mem;
    }
    ExprRef ArmSme::CombineTileWithVerticalVector(const ExprRef& mem, const ExprRef& tile_idx, const ExprRef& vec, const ExprRef& row_pred, const ExprRef& col_pred, const NumericType& element_size_bits, const ExprRef& is_zero_mode, std::function<ExprRef(ExprRef a, ExprRef b)> combine_fn) {
        assert(row_pred.bit_width() == col_pred.bit_width());
        assert(row_pred.bit_width() == P_REG_WIDTH);
    
        NumericType dim = Z_REG_WIDTH / element_size_bits;
        
        auto new_mem = mem;
        for (size_t col = 0; col < dim; col++){
            auto ver_slice = _GetTypedVerticalSlice(new_mem, BvConst(col, ZA_ADDR_WIDTH), tile_idx, element_size_bits);
            for (size_t row = 0; row < dim; row++){
                auto old_elem = GetElementInVectorFromLSB(ver_slice, row, element_size_bits);
                auto extra_elem = GetElementInVectorFromLSB(vec, row, element_size_bits);
                ExprRef row_col_activated = (GetBitFromLSB(row_pred, row) != 0) & (GetBitFromLSB(col_pred, col) != 0);
                auto new_elem = Ite(row_col_activated, combine_fn(old_elem, extra_elem), Ite(is_zero_mode, BvConst(0, element_size_bits), old_elem));
                ver_slice = SetElementInVectorFromLSB(ver_slice, row, element_size_bits, new_elem, Z_REG_WIDTH);
            }
            // update the entire vertical slice
            new_mem = _SetTypedVerticalSlice(new_mem, BvConst(col, ZA_ADDR_WIDTH), tile_idx, element_size_bits, ver_slice);
        }
        return new_mem;
    }
    
    ExprRef ArmSme::IntegerCombineTileWithMatrices(const ExprRef& mem, const ExprRef& tile_idx, const ExprRef& vec1, const ExprRef& vec2, const ExprRef& row_pred, const ExprRef& col_pred, const NumericType& element_size_bits, bool sub_instead_of_add, bool op1_unsigned, bool op2_unsigned) {
        NumericType dim = Z_REG_WIDTH / element_size_bits;
        auto new_mem = mem;
        for (size_t row = 0; row < dim; row++){
            auto hor_slice = _GetTypedHorizontalSlice(new_mem, BvConst(row, ZA_ADDR_WIDTH), tile_idx, element_size_bits);
            for (size_t col = 0; col < dim; col++){
                auto sum = GetElementInVectorFromLSB(hor_slice, col, element_size_bits);
                // widening dot product (smaller bits into larger bits)
                for (size_t k = 0; k < 4; k++){
                    // ASK note sure about predicates, ARM uses `ElemP[esize DIV 4]`
                    auto activated = (GetBitFromLSB(row_pred, 4*row+k) != 0) & (GetBitFromLSB(col_pred, 4*col+k) != 0);

                    NumericType sub_element_size_bits = element_size_bits / 4;
                    auto op1 = GetElementInVectorFromLSB(vec1, 4*row+k, sub_element_size_bits);
                    op1 = op1_unsigned ? ZExt(op1, element_size_bits) : SExt(op1, element_size_bits);
                    auto op2 = GetElementInVectorFromLSB(vec2, 4*col+k, sub_element_size_bits);
                    op2 = op2_unsigned ? ZExt(op2, element_size_bits) : SExt(op2, element_size_bits);
                    auto prod = op1 * op2;
                    if (sub_instead_of_add) prod = -prod;
                    sum = Ite(activated, sum + prod, sum); // only updated if active
                }
                // update hor_slice with new sum
                hor_slice = SetElementInVectorFromLSB(hor_slice, col, element_size_bits, sum, Z_REG_WIDTH);
            }
            // get new_mem by updating the entire horizontal slice
            new_mem = _SetTypedHorizontalSlice(new_mem, BvConst(row, ZA_ADDR_WIDTH), tile_idx, element_size_bits, hor_slice);
        }
        return new_mem;
    }
    
    ExprRef ArmSme::FloatCombineTileWithMatricesK2(const ExprRef& mem, const ExprRef& tile_idx, const ExprRef& vec1, const ExprRef& vec2, const ExprRef& pred1, const ExprRef& pred2, const NumericType& dest_element_size_bits, const NumericType& src_element_size_bits, bool sub_instead_of_add, const ExprRef& fpzero, const FuncRef& neg_fn, const FuncRef& dotadd_fn) {
        NumericType dim = Z_REG_WIDTH / dest_element_size_bits;
        auto new_mem = mem;
        for (size_t row = 0; row < dim; row++){
            auto hor_slice = _GetTypedHorizontalSlice(new_mem, BvConst(row, ZA_ADDR_WIDTH), tile_idx, dest_element_size_bits);
            for (size_t col = 0; col < dim; col++){
                auto prow_0 = (GetBitFromLSB(pred1, 2*row+0) != 0);
                auto prow_1 = (GetBitFromLSB(pred1, 2*row+1) != 0);
                auto pcol_0 = (GetBitFromLSB(pred2, 2*col+0) != 0);
                auto pcol_1 = (GetBitFromLSB(pred2, 2*col+1) != 0);

                auto sum = GetElementInVectorFromLSB(hor_slice, col, dest_element_size_bits);
                auto erow_0 = Ite(prow_0, GetElementInVectorFromLSB(vec1, 2*row+0, src_element_size_bits), fpzero);
                auto erow_1 = Ite(prow_1, GetElementInVectorFromLSB(vec1, 2*row+1, src_element_size_bits), fpzero);
                auto ecol_0 = Ite(pcol_0, GetElementInVectorFromLSB(vec2, 2*col+0, src_element_size_bits), fpzero);
                auto ecol_1 = Ite(pcol_1, GetElementInVectorFromLSB(vec2, 2*col+1, src_element_size_bits), fpzero);

                if (sub_instead_of_add){ // ASK can i remove the Ite() check?
                    // only need erow_0 and erow_1 to be negated in this case
                    erow_0 = Ite(prow_0, neg_fn(erow_0), erow_0);
                    erow_1 = Ite(prow_1, neg_fn(erow_1), erow_1);
                }
                // TODO not sure if predicate logic is right, 'unmodified' is guared by Ite()
                auto any_active = (prow_0 & pcol_0) | (prow_1 & pcol_1);
                sum = Ite(any_active, dotadd_fn({sum, erow_0, erow_1, ecol_0, ecol_1}), sum);
                
                // update hor_slice with new sum
                hor_slice = SetElementInVectorFromLSB(hor_slice, col, dest_element_size_bits, sum, Z_REG_WIDTH);
            }
            // get new_mem by updating the entire horizontal slice
            new_mem = _SetTypedHorizontalSlice(new_mem, BvConst(row, ZA_ADDR_WIDTH), tile_idx, dest_element_size_bits, hor_slice);
        }
        return new_mem;
    }

    ExprRef ArmSme::FloatCombineTileWithMatricesK1(const ExprRef& mem, const ExprRef& tile_idx, const ExprRef& vec1, const ExprRef& vec2, const ExprRef& pred1, const ExprRef& pred2, const NumericType& element_size_bits, bool sub_instead_of_add, const FuncRef& neg_fn, const FuncRef& fmac_fn) {
        NumericType dim = Z_REG_WIDTH / element_size_bits;
        auto new_mem = mem;
        for (size_t row = 0; row < dim; row++){
            auto hor_slice = _GetTypedHorizontalSlice(new_mem, BvConst(row, ZA_ADDR_WIDTH), tile_idx, element_size_bits);
            for (size_t col = 0; col < dim; col++){
                auto sum = GetElementInVectorFromLSB(hor_slice, col, element_size_bits);
                auto op1 = GetElementInVectorFromLSB(vec1, row, element_size_bits);
                auto op2 = GetElementInVectorFromLSB(vec2, col, element_size_bits);

                if (sub_instead_of_add) op1 = neg_fn(op1);
                auto activated = (GetBitFromLSB(pred1, row) != 0) & (GetBitFromLSB(pred2, col) != 0);
                sum = Ite(activated, fmac_fn({sum, op1, op2}), sum);
                
                // update hor_slice with new sum
                hor_slice = SetElementInVectorFromLSB(hor_slice, col, element_size_bits, sum, Z_REG_WIDTH);
            }
            // get new_mem by updating the entire horizontal slice
            new_mem = _SetTypedHorizontalSlice(new_mem, BvConst(row, ZA_ADDR_WIDTH), tile_idx, element_size_bits, hor_slice);
        }
        return new_mem;
    }
    
    std::vector<size_t> ArmSme::GetSliceAddresses(int tile_idx, int slice_idx, bool is_vertical, const NumericType& element_size_bits) {
        std::vector<size_t> addresses;
        NumericType dim = SVL / element_size_bits;
        NumericType num_tiles = SVL_B / dim;
        int element_size_bytes = element_size_bits / BYTE;
        
        if (is_vertical) {
            // ARM SME: col_idx % dim (required by ARM)
            int wrapped_col_idx = (dim == 1) ? 0 : (slice_idx & (dim - 1)); // & (dim-1) fast modulo
            int col = (SVL_B - element_size_bytes) - (wrapped_col_idx * element_size_bytes);
            
            for (size_t i = 0; i < dim; i++) {
                size_t row = tile_idx + i * num_tiles;
                size_t base_addr = row * SVL_B + col;
                for (int b = 0; b < element_size_bytes; b++) {
                    addresses.push_back(base_addr + b);
                }
            }
        } else {
            // ARM SME: row_idx % dim (required by ARM)
            int wrapped_row_idx = (dim == 1) ? 0 : (slice_idx & (dim - 1)); // & (dim-1) fast modulo
            size_t row = tile_idx + wrapped_row_idx * num_tiles;
            
            for (size_t col = 0; col < SVL_B; col++) {
                addresses.push_back(row * SVL_B + col);
            }
        }
        return addresses;
    }
    
    ExprRef ArmSme::GetVectorRegister(size_t z_idx) {
        assert(z_idx < Z_REG_COUNT);
        return z_regs[z_idx];
    }

    ExprRef ArmSme::GetPredicateRegister(size_t p_idx) {
        assert(p_idx < P_REG_COUNT);
        return p_regs[p_idx];
    }
    
    ExprRef ArmSme::Get32BitGPR(size_t w_idx) {
        assert(w_idx < GPR_COUNT);
        if (w_idx == 31) { return WZR; }
        else { return Extract(GPRs[w_idx], 31, 0); }
    }

    ExprRef ArmSme::Get64BitGPR(size_t x_idx, bool use_sp) {
        assert(x_idx < GPR_COUNT);
        if (x_idx == 31) { return use_sp ? SP : XZR; }
        else { return GPRs[x_idx]; }
    }
 
}  // namespace arm
