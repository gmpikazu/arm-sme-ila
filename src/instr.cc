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
        
        // ASK since MOV is alias to MOVA, maybe no need to implement
        { // MOVA (tile to vector)
            auto f = [&](NumericType opcode, NumericType esize, std::string suffix, ExprRef tile_idx, ExprRef imm){
                InstrRef instr = m.NewInstr("MOVA_T2V"+suffix);
                auto decode = SME_ON & (cmd == opcode);
                instr.SetDecode(decode);

                auto slice_idx = BaseRegPlusImm(Get32BitGPR(Rs), imm);
                // TODO what to do with ZAn, different for each bit width
                auto source = GetTypedSlice(za, HV, tile_idx, slice_idx, esize);
                auto dest = GetVectorRegister(Zd);
                auto masked = MaskWithSinglePredicate(source, dest, esize, SVL, GetPredicateRegister(Pg), BoolConst(false));
                UpdateSingleVectorRegister(instr, Zn, masked);
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
                auto dest = GetTypedSlice(za, HV, tile_idx, slice_idx, BYTE);
                auto masked = MaskWithSinglePredicate(source, dest, BYTE, SVL, GetPredicateRegister(Pn), BoolConst(false));
                UpdateSingleTypedSlice(instr, HV, tile_idx, slice_idx, BYTE, masked);
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
    }
    
}  // namespace arm