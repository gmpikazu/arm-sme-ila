#include "arm.h"

namespace arm {

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
        }
        
        // NOTE instructions below requires Streaming SVE mode
        ExprRef SME_ON = pstate_sm & pstate_za;
        #define constrained(tile_idx, esize) ToConstrainedTileIndex(tile_idx, esize)
        
        // TODO move the constrained() logic INTO the lambda itself, all instr below
        // TODO update the lambdas to use const reference instead
        // ASK since MOV is alias to MOVA, maybe no need to implement
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

            // NOTE instruction only operates on 64-bit tiles (8 tiles total)
            NumericType esize = DOUBLE; 
            ExprRef new_za = za;
            for (size_t tile_idx = 0; tile_idx < 8; tile_idx++){
                // ASK is it really from LSB?
                ExprRef activated = (GetBitFromLSB(Imm8, tile_idx) != 0);
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
            
            // NOTE no SP support for this instruction
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
            // TODO bf16 and fp16 treated the same
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
    }
    
}  // namespace arm