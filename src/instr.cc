#include "arm.h"
#include <cmath>

namespace arm {

    // NOTE: esize is BUILT-INTO the instruction using .B .H .W .D .Q suffixes
    void ArmSme::AddInstructions() {
        { // SMSTART
            InstrRef instr = m.NewInstr("SMSTART");
            auto decode = TEMP_DECODE;
            instr.SetDecode(decode);
            instr.SetUpdate(pstate_sm, BoolConst(true));
            instr.SetUpdate(pstate_za, BoolConst(true));
            // TODO: zero out vector and predicate registers
        }
        { // SMSTOP
            InstrRef instr = m.NewInstr("SMSTOP");
            auto decode = TEMP_DECODE;
            instr.SetDecode(decode);
            instr.SetUpdate(pstate_sm, BoolConst(false));
            instr.SetUpdate(pstate_za, BoolConst(false));
            // TODO: zero out vector and predicate registers
            // TODO: changing pstate_ZA may zero out the ZA storage (read B1.1.1.2 PSTATE.ZA)
        }
        
        // NOTE: instructions below requires Streaming SVE mode
        ExprRef SME_ON = pstate_sm & pstate_za;
        #define constrained(tile_idx, esize) ToConstrainedTileIndex(tile_idx, esize)
        
        // TODO: move the constrained() logic INTO the lambda itself, all instr below
        // TODO: update the lambdas to use const reference instead
        // ASK: since MOV is alias to MOVA, maybe no need to implement
        { // MOVA (tile to vector)
            auto f = [&](NumericType opcode, NumericType esize, std::string suffix, ExprRef tile_idx, ExprRef imm){
                InstrRef instr = m.NewInstr("MOVA_T2V"+suffix);
                auto decode = SME_ON & (cmd == opcode);
                instr.SetDecode(decode);

                auto slice_idx = BaseRegPlusImm(Get32BitGPR(Rs), imm);
                auto source = GetTypedSlice(za, HV, tile_idx, slice_idx, esize);
                auto dest = GetVectorRegister(Zd);
                auto masked = MaskWithSinglePredicate(source, dest, esize, SVL, GetPredicateRegister(Pg), BoolConst(false));
                UpdateSingleVectorRegister(instr, Zd, masked);
            };
            f(TEMP_OPCODE, BYTE, ".B", constrained(ZAn, BYTE), Imm4);
            f(TEMP_OPCODE, HALF, ".H", constrained(ZAn, HALF), Imm3);
            f(TEMP_OPCODE, WORD, ".S", constrained(ZAn, WORD), Imm2);
            f(TEMP_OPCODE, DOUBLE, ".D", constrained(ZAn, DOUBLE), Imm1);
            f(TEMP_OPCODE, QUAD, ".Q", constrained(ZAn, QUAD), BvConst(0, 1));
        }
        { // MOVA (vector to tile)
            auto f = [&](NumericType opcode, NumericType esize, std::string suffix, ExprRef tile_idx, ExprRef imm){
                InstrRef instr = m.NewInstr("MOVA_V2T"+suffix);
                auto decode = SME_ON & (cmd == opcode);
                instr.SetDecode(decode);
                
                auto slice_idx = BaseRegPlusImm(Get32BitGPR(Rs), imm);
                auto source = GetVectorRegister(Zn);
                auto dest = GetTypedSlice(za, HV, tile_idx, slice_idx, esize);
                auto masked = MaskWithSinglePredicate(source, dest, esize, SVL, GetPredicateRegister(Pg), BoolConst(false));
                UpdateSingleTypedSlice(instr, HV, tile_idx, slice_idx, esize, masked);
            };
            f(TEMP_OPCODE, BYTE, ".B", constrained(ZAd, BYTE), Imm4);
            f(TEMP_OPCODE, HALF, ".H", constrained(ZAd, HALF), Imm3);
            f(TEMP_OPCODE, WORD, ".S", constrained(ZAd, WORD), Imm2);
            f(TEMP_OPCODE, DOUBLE, ".D", constrained(ZAd, DOUBLE), Imm1);
            f(TEMP_OPCODE, QUAD, ".Q", constrained(ZAd, QUAD), BvConst(0, 1));
        }
        { // ZERO
            InstrRef instr = m.NewInstr("ZERO");
            auto decode = SME_ON & TEMP_DECODE;
            instr.SetDecode(decode);

            // NOTE: instruction only operates on 64-bit tiles (8 tiles total)
            NumericType esize = DOUBLE; 
            ExprRef new_za = za;
            for (size_t tile_idx = 0; tile_idx < 8; tile_idx++){
                // ASK: is it really from LSB?
                ExprRef activated = (GetPredBitFromLSB(Imm8, tile_idx, BYTE) != 0);
                ExprRef zeroed_tile_za = new_za;
                // construct new ZA expr where tile[tile_idx] is zeroed out
                for (size_t row_idx = 0; row_idx < 8; row_idx++){
                    zeroed_tile_za = _SetTypedHorizontalSlice(zeroed_tile_za, BvConst(row_idx, ZA_ADDR_WIDTH), BvConst(tile_idx, ZA_ADDR_WIDTH), esize, BvConst(0, SVL)); // convert hardcoded row_idx, tile_idx to ExprRef
                }
                // only apply zeroing if tile bit is activated
                new_za = Ite(activated, zeroed_tile_za, new_za);
            }
            instr.SetUpdate(za, new_za);
        }
        { // ADDHA
            auto add_fn = [](ExprRef a, ExprRef b) { return a + b; };
            auto f = [&](NumericType opcode, NumericType esize, std::string suffix, ExprRef tile_idx){
                InstrRef instr = m.NewInstr("ADDHA"+suffix);
                auto decode = SME_ON & (cmd == opcode);
                instr.SetDecode(decode);
                
                auto row_pred = GetPredicateRegister(Pn);
                auto col_pred = GetPredicateRegister(Pm);
                auto vec = GetVectorRegister(Zn);
                auto new_za = CombineTileWithHorizontalVector(za, tile_idx, vec, row_pred, col_pred, esize, BoolConst(false), add_fn);
                instr.SetUpdate(za, new_za);
            };
            f(TEMP_OPCODE, WORD, ".S", constrained(ZAda, WORD));
            f(TEMP_OPCODE, DOUBLE, ".D", constrained(ZAda, DOUBLE));
        }
        { // ADDVA
            auto add_fn = [](ExprRef a, ExprRef b) { return a + b; };
            auto f = [&](NumericType opcode, NumericType esize, std::string suffix, ExprRef tile_idx){
                InstrRef instr = m.NewInstr("ADDVA"+suffix);
                auto decode = SME_ON & (cmd == opcode);
                instr.SetDecode(decode);
                
                auto row_pred = GetPredicateRegister(Pn);
                auto col_pred = GetPredicateRegister(Pm);
                auto vec = GetVectorRegister(Zn);
                auto new_za = CombineTileWithVerticalVector(za, tile_idx, vec, row_pred, col_pred, esize, BoolConst(false), add_fn);
                instr.SetUpdate(za, new_za);
            };
            f(TEMP_OPCODE, WORD, ".S", constrained(ZAda, WORD));
            f(TEMP_OPCODE, DOUBLE, ".D", constrained(ZAda, DOUBLE));
        }
        { // Integer Outer Product and Accumulate or Subtract
            auto f = [&](std::string name, NumericType opcode, NumericType esize, std::string suffix, ExprRef tile_idx, bool sub_op, bool op1_unsigned, bool op2_unsigned){
                InstrRef instr = m.NewInstr(name+suffix);
                auto decode = SME_ON & (cmd == opcode);
                instr.SetDecode(decode);

                auto new_za = IntegerCombineTileWithMatrices(za, tile_idx, GetVectorRegister(Zn), GetVectorRegister(Zm), GetPredicateRegister(Pn), GetPredicateRegister(Pm), esize, sub_op, op1_unsigned, op2_unsigned);
                instr.SetUpdate(za, new_za);
            };
            struct inst {
                std::string name;
                NumericType opcode;
                bool sub_op;
                bool op1_unsigned;
                bool op2_unsigned;
            };
            std::vector<inst> inst_list = {
                {"SMOPA", TEMP_OPCODE, false, false, false},
                {"SMOPS", TEMP_OPCODE, true, false, false},
                {"SUMOPA", TEMP_OPCODE, false, false, true},
                {"SUMOPS", TEMP_OPCODE, true, false, true},
                {"UMOPA", TEMP_OPCODE, false, true, true},
                {"UMOPS", TEMP_OPCODE, true, true, true},
                {"USMOPA", TEMP_OPCODE, false, true, false},
                {"USMOPS", TEMP_OPCODE, true, true, false}
            };
            for (auto inst : inst_list){
                f(inst.name, inst.opcode, WORD, " (8b->32b)", constrained(ZAda, WORD), inst.sub_op, inst.op1_unsigned, inst.op2_unsigned);
                f(inst.name, inst.opcode, DOUBLE, " (16b->64b)", constrained(ZAda, DOUBLE), inst.sub_op, inst.op1_unsigned, inst.op2_unsigned);
            }
        }
        { // ADDSPL
            InstrRef instr = m.NewInstr("ADDSPL");
            auto decode = SME_ON & (cmd == TEMP_OPCODE);
            instr.SetDecode(decode);
            
            auto val = BvConst(P_REG_WIDTH / BYTE, 64) * SExt(Imm6, 64) + Get64BitGPR(Rn, true);
            UpdateSingle64BitGPR(instr, Rd, val, true);
        }
        { // ADDSVL
            InstrRef instr = m.NewInstr("ADDSVL");
            auto decode = SME_ON & (cmd == TEMP_OPCODE);
            instr.SetDecode(decode);
            
            auto val = BvConst(Z_REG_WIDTH / BYTE, 64) * SExt(Imm6, 64) + Get64BitGPR(Rn, true);
            UpdateSingle64BitGPR(instr, Rd, val, true);
        }
        { // RDSVL
            InstrRef instr = m.NewInstr("RDSVL");
            auto decode = SME_ON & (cmd == TEMP_OPCODE);
            instr.SetDecode(decode);
            
            // NOTE: no SP support for this instruction
            auto val = BvConst(Z_REG_WIDTH / BYTE, 64) * SExt(Imm6, 64);
            UpdateSingle64BitGPR(instr, Rd, val);
        }
        { // Widening Floating Point Outer Product and Accumulate or Subtract (K=2)
            auto f = [&](std::string name, NumericType opcode, NumericType dest_esize, NumericType src_esize, const ExprRef& tile_idx, bool sub_op, const ExprRef& fpzero, const FuncRef& neg_fn, const FuncRef& dotadd_fn){
                InstrRef instr = m.NewInstr(name);
                auto decode = SME_ON & (cmd == opcode);
                instr.SetDecode(decode);
                
                auto new_za = FloatCombineTileWithMatricesK2(za, tile_idx, GetVectorRegister(Zn), GetVectorRegister(Zm), GetPredicateRegister(Pn), GetPredicateRegister(Pm), dest_esize, src_esize, sub_op, fpzero, neg_fn, dotadd_fn);
                instr.SetUpdate(za, new_za);
            };
            // TODO: bf16 and fp16 treated the same
            f("BFMOPA (bf16->fp32)", TEMP_OPCODE, WORD, HALF, constrained(ZAda, WORD), false, bf16_zero, bfneg16, bfdotadd16to32);
            f("BFMOPS (bf16->fp32)", TEMP_OPCODE, WORD, HALF, constrained(ZAda, WORD), true, bf16_zero, bfneg16, bfdotadd16to32);
            f("FMOPA (fp16->fp32)", TEMP_OPCODE, WORD, HALF, constrained(ZAda, WORD), false, fp16_zero, fpneg16, fpdotadd16to32);
            f("FMOPS (fp16->fp32)", TEMP_OPCODE, WORD, HALF, constrained(ZAda, WORD), true, fp16_zero, fpneg16, fpdotadd16to32);
        }
        { // Non-Widening Floating Point Outer Product and Accumulate or Subtract (K=1)
            // has .S and .D
            auto f = [&](std::string name, std::string suffix, NumericType opcode, NumericType esize, const ExprRef& tile_idx, bool sub_op, const FuncRef& neg_fn, const FuncRef& fmac_fn){
                InstrRef instr = m.NewInstr(name);
                auto decode = SME_ON & (cmd == opcode);
                instr.SetDecode(decode);
                
                auto new_za = FloatCombineTileWithMatricesK1(za, tile_idx, GetVectorRegister(Zn), GetVectorRegister(Zm), GetPredicateRegister(Pn), GetPredicateRegister(Pm), esize, sub_op, neg_fn, fmac_fn);
                instr.SetUpdate(za, new_za);
            };
            f("FMOPA (non-widening)", ".S", TEMP_OPCODE, WORD, constrained(ZAda, WORD), false, fpneg32, fpmac32);
            f("FMOPS (non-widening)", ".S", TEMP_OPCODE, WORD, constrained(ZAda, WORD), true, fpneg32, fpmac32);
            f("FMOPA (non-widening)", ".D", TEMP_OPCODE, DOUBLE, constrained(ZAda, DOUBLE), false, fpneg64, fpmac64);
            f("FMOPS (non-widening)", ".D", TEMP_OPCODE, DOUBLE, constrained(ZAda, DOUBLE), true, fpneg64, fpmac64);
        }
        { // Typed Loads (not LDR)
            auto f = [&](std::string name, NumericType opcode, NumericType esize, const ExprRef& tile_idx, const ExprRef imm){
                InstrRef instr = m.NewInstr(name);
                auto decode = SME_ON & (cmd == opcode);
                instr.SetDecode(decode);

                // TODO: base ignores check SP alignment
                // how would we conditionally run a chunk of ILAng code to check SP alignment?
                // for now always guarantee SP is aligned
                auto base = Get64BitGPR(Rn, true); 
                auto offset = Get64BitGPR(Rm); // NOTE: assembler defaults Rm=31 (XZR) if programmer left it blank
                auto slice_idx = BaseRegPlusImm(Get32BitGPR(Rs), imm);
                auto mask = GetPredicateRegister(Pg);

                ExprRef result = BvConst(0, SVL); // init zero vector
                NumericType dim = SVL / esize;
                NumericType byte_esize = esize / BYTE;
                for (size_t i = 0; i < dim; i++) {
                    auto addr = ZExt(base, DRAM_ADDR_WIDTH) + ZExt(offset, DRAM_ADDR_WIDTH) * BvConst(byte_esize, DRAM_ADDR_WIDTH);
                    ExprRef loaded = DRAM_GetElementBytes(addr, byte_esize); // using UF must use byte_esize
                    ExprRef new_elem = Ite(GetPredBitFromLSB(mask, i, esize) != 0, loaded, BvConst(0, esize));
                    result = SetElementInVectorFromLSB(result, i, esize, new_elem, SVL);
                    offset = offset + 1;
                }
                UpdateSingleTypedSlice(instr, HV, tile_idx, slice_idx, esize, result);
            };
            f("LD1.B", TEMP_OPCODE, BYTE, constrained(ZAt, BYTE), Imm4);
            f("LD1.H", TEMP_OPCODE, HALF, constrained(ZAt, HALF), Imm3);
            f("LD1.W", TEMP_OPCODE, WORD, constrained(ZAt, WORD), Imm2);
            f("LD1.D", TEMP_OPCODE, DOUBLE, constrained(ZAt, DOUBLE), Imm1);
            f("LD1.Q", TEMP_OPCODE, QUAD, constrained(ZAt, QUAD), BvConst(0, 1));
        }
        // TODO: LDR, store, STR
        // then base A64, SVE2 instructions
        // some decode only need ZA

        { // PSEL
            auto f = [&](std::string name, NumericType opcode, NumericType esize, const ExprRef imm){
                InstrRef instr = m.NewInstr(name);
                auto decode = SME_ON & (cmd == opcode);
                instr.SetDecode(decode);

                auto operand1 = GetPredicateRegister(Pn); // source
                auto operand2 = GetPredicateRegister(Pm); // mask

                NumericType elements = SVL / esize; 
                assert(esize != SVL); // NOTE: no QUAD support in ARM SME, so esize != SVL
                assert(elements % 2 == 0); // must be even for log2 to be safe
                auto before_modulo = BaseRegPlusImm(Get32BitGPR(Rv), imm);
                auto wrapped_index = Extract(before_modulo, std::log2(elements)-1, 0); // lower bits

                auto is_active = (GetPredBitFromLSB(operand2, wrapped_index, esize) != 0); // nested Ite
                assert(operand1.bit_width() == P_REG_WIDTH && operand1.bit_width() == operand2.bit_width());
                auto result = Ite(is_active, operand1, BvConst(0, operand1.bit_width()));
                UpdateSinglePredicateRegister(instr, Pd, result);
            };
            f("PSEL.B", TEMP_OPCODE, BYTE, Imm4);
            f("PSEL.H", TEMP_OPCODE, HALF, Imm3);
            f("PSEL.W", TEMP_OPCODE, WORD, Imm2);
            f("PSEL.D", TEMP_OPCODE, DOUBLE, Imm1);
            // no QUAD support
        }

        { // REVD.Q
            InstrRef instr = m.NewInstr("REVD.Q");
            auto decode = SME_ON & (cmd == TEMP_OPCODE);
            instr.SetDecode(decode);

            NumericType esize = 128, swsize = 64; // NOTE: fixed for QUAD only
            NumericType elements = SVL / esize;
            auto mask = GetPredicateRegister(Pg);
            auto operand = GetVectorRegister(Zn);
            auto result = GetVectorRegister(Zd); // will be modified below

            assert(elements > 0); // SVL greater than esize
            for (size_t i = 0; i < elements; i++) {
                auto src_elem = GetElementInVectorFromLSB(operand, i, esize);
                auto high_half = GetElementInVectorFromLSB(src_elem, 1, swsize);
                auto low_half = GetElementInVectorFromLSB(src_elem, 0, swsize);
                auto reversed = Concat(low_half, high_half); // swap halves
                assert(reversed.bit_width() == src_elem.bit_width() && reversed.bit_width() == esize);

                // inactive remains unmodified
                auto old_dest_elem = GetElementInVectorFromLSB(result, i, esize);
                auto active = (GetPredBitFromLSB(mask, i, esize) != 0);
                auto new_elem = Ite(active, reversed, old_dest_elem);

                result = SetElementInVectorFromLSB(result, i, esize, new_elem, SVL);
            }
            UpdateSingleVectorRegister(instr, Zd, result);
        }

        { // CLAMP (signed and unsigned)
            #define Umax(a, b) Ite(Ugt(a, b), a, b)
            #define Umin(a, b) Ite(Ult(a, b), a, b)
            #define Smax(a, b) Ite(Sgt(a, b), a, b)
            #define Smin(a, b) Ite(Slt(a, b), a, b)

            auto f = [&](std::string name, std::string suffix, NumericType opcode, NumericType esize, bool is_signed) {
                InstrRef instr = m.NewInstr(name+suffix);
                auto decode = SME_ON & (cmd == opcode);
                instr.SetDecode(decode);

                auto min_op = GetVectorRegister(Zn);
                auto max_op = GetVectorRegister(Zm);
                auto dest_old = GetVectorRegister(Zd);
                auto result = BvConst(0, dest_old.bit_width());
                assert(result.bit_width() == Z_REG_WIDTH);

                NumericType elements = SVL / esize;
                for (size_t i = 0; i < elements; i++) {
                    auto min_elem = GetElementInVectorFromLSB(min_op, i, esize);
                    auto max_elem = GetElementInVectorFromLSB(max_op, i, esize);
                    auto elem = GetElementInVectorFromLSB(dest_old, i, esize);
                    
                    auto res_elem = is_signed ? Smin(Smax(min_elem, elem), max_elem) : Umin(Umax(elem, min_elem), max_elem);
                    result = SetElementInVectorFromLSB(result, i, esize, res_elem, SVL);
                }
                UpdateSingleVectorRegister(instr, Zd, result);
            };
            f("SCLAMP", ".B", TEMP_OPCODE, BYTE, true);
            f("SCLAMP", ".H", TEMP_OPCODE, HALF, true);
            f("SCLAMP", ".S", TEMP_OPCODE, WORD, true);
            f("SCLAMP", ".D", TEMP_OPCODE, DOUBLE, true);
            // no QUAD support for signed
            f("UCLAMP", ".B", TEMP_OPCODE, BYTE, false);
            f("UCLAMP", ".H", TEMP_OPCODE, HALF, false);
            f("UCLAMP", ".S", TEMP_OPCODE, WORD, false);
            f("UCLAMP", ".D", TEMP_OPCODE, DOUBLE, false);
            // no QUAD support for unsigned too
        }
    }
    
}  // namespace arm
