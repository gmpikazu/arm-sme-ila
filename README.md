# Project Plan
## Crucial Clarification (in meeting)
- how to model exception throw of `SPAlignmentCheck`, do we need to search for how ARM handles exceptions and save state, exception handler? Or just assume the best case scenario that we always have aligned SP using assertions (SP % 16 == 0 precondition), or update a custom made bool flag for `stack_misaligned`
    > how to conditionally run an SP alignment check when `Rn` field is passed in as 31??
> how to constrain UFs, like negating is only flipping first bit of float, or product/sum, without being tedious?
- loading/storing BYTE,..,QUAD has no alignment check? meaning we can read across cache lines?
- typed load/store first before `LDR`, `STR`

> Does ZA behave in little endian (rightmost is LSB) when interpreting elements on tile slices?
- When modelling DRAM load/stores, we need to care about endianness (higher bits are placed near MSB)
> Arithmetic Instruction with Predicate Masking are non-efficient, solver can't finish 
## Remaining Tasks
- Check ARM pseudocode for Floating Point blackbox instructions (similar to checking `ElemP[]` implementation)
    - eg., neg_fn, fmac_fn, fdotadd_fn, etc
- Optimize `K2` FP instructions using `delta` then `sum` pattern <!-- TODO: optimizations require checking the true ARM pseuducode to see whether we can split the logic into `delta` and `sum` -->
- Check each instruction and their inputs for proper `SExt` or `ZExt`
- Check `ExprRef` arithmetic, make sure they are extended before operation **to prevent truncation**
- Model load store DRAM (using UFs), SVE2 instructions using <T> field
- Test edge cases of `XZR`, `WZR` access and write
- Verify instructions by constructing unit tests, then integration tests (eg., {ZERO, MOVA, SMOPA})
## Differences From ARM SME Document
> No information on `REVD`'s `Reverse(element, swsize)` internal behavior (modelled by assumption instead)
- A64 instructions like `MSR` and `SMSTART SM` aren't modelled completely down to each bit
- `Ws`, `Wv` in ARM only selects registers `W12-W15`, but this ILA allows selecting any `W` register
    - **Solution:** limit `Rs`, `Rv` to 2-bits and prepend `011` in `BasePlusOffset` function
- Size suffix decoding is the reponsibility of the caller who decides which instruction type (.B .H ..) to execute, the model's ILA behavior does not adjust its logic depending on a `size` `ExprRef` runtime variable:
    - `SCLAMP`, `UCLAMP` have their `size` suffix (.B .H ..) embedded into the instruction type, hence the ILA behavior does not attempt to compute an `esize` `BvExpr` at runtime since the helper functions require concrete C++ integers
    - `PSEL` runtime decoding of `i1:tszh:tszl` is left to the caller, the ILA behavior only limits the bit-width of the `Imm` field according to the `size` suffix (.B .H ..) embedded into the instruction type
        - Replacing `i1:tszh:tszl` immediate extraction with immediates among `Imm1,Imm2,Imm3,Imm4` is valid because the freedom of choosing bits in the immediate is the same (constraining `tszh:tszl` for decode, doesn't constrain the range of immediates that can be used in `imm5<4:>` since `i1` is free)
## Delayed Simple Tasks
- Not all instructions require Streaming SVE Mode, some only need ZA
- Refactor unit tests to use the new `track_slice()` + `cstr_all_tracked_and_zero()` idiom
- `SMSSTART/STOP` on/off zeroing behavior (B1.1.1 and E2 pseudocode of SM,ZA states)
- Use `assert`s instead of `if` and `switch` for program invariants
- Remove `merge` and `zero` mode `ExprRef` selection if Z3 takes too long
- `Z_REG_WIDTH` and `SVL` scattered around code but they are same thing
## Delayed Complex Tasks
- Floating point IEEE behavior, maybe no need since we replaced FP with Uninterpreted Functions
- Bfloat16 and Fp16 are treated as the same thing in ILAng because bit-widths are equal (not a problem at the moment)
## Random Questions
- Does ILAng use 2's complement natively for operator overloads?
- How do I know if a comparison operator overload is signed or unsigned?
## Hardcoded Things To Generalize Later
- `PrintZa` prints 16x16 matrix
    - Need check the alignement issue and make sure the printing is accurate for larger `SVL_B`
- All unit tests constrain a 128-bit vector
    - Need to create generalized `bv_ones(size)`, `bv_zeros(size)` that fills `size` bits with 1 or 0 respectively
    - Also need `bv_sequence(size)` that fills `0x00`, ..., `0xff` up to `size` bits

# Implementation Overview
This document is aimed to provide viewers with an overview of the implementation specifics of this project

## Code Conventions
- Widths and sizes are given in bits (eg., `SVL`, `BYTE`, `HALF`)
- `UpdateSingle`-prefixed functions perform `instr.SetUpdate()` internally so **does not** support updating multiple changes at once (use lower-level helpers instead)

## ZA Storage
**Representation:**
- `SVL_B`x`SVL_B` matrix represented as a linear array of `BYTE`s
- Smallest unit of data in ARM is `BYTE`
- `ZA[row][col]` is expanded into C-style pointer arithmetic indexing, where top-left element is index `[0][0]` and bottom-right element is index `[SVL_B-1][SVL_B-1]`

**Complying with ARM's Convention:**
- ARM SME views the matrix with top-right element being index `[0][0]` and bottom-left element being index `[SVL_B-1][SVL_B-1]` (ie., index 0 starts at topmost row or rightmost colummn)

**Helper Functions:**
- `GetTypedSlice()` and `SetTypedSlice()` uses ARM's convention to access vector slices of tile but internally converts ARM's indexing to C-style pointer arithmetic indexing
- `ToMemoryAddress(row, col)` helper function uses C-style pointer arithmetic indexing to convert `[row][col]` into a linear address

**Getter and Setter for ZA Tiling:**
- `GetElement` helper function `loads` adjacent `BYTE`s and concatenates them to form the output vector
- `SetElement` helper function breaks the input vector into `BYTE`s and `stores` them into ZA memory byte-per-byte

## Predicate Masking
- ARM SME supports `/M` (merge mode), destination element is unmodified if source element is not activated by predicate bit, and `/Z` (zero mode), destination element is zeroed out instead when source element is not activated by predicate bit
- Predicate registers contain `SVL_B` bits and `bit[i * (esize / BYTE)]` controls activation of `vector.elem[i]` where an element can occupy `esize` bits (eg., `BYTE`, `HALF`, etc)
- The implementation extracts bits starting from LSB, where index `i` is multiplied by `(element_size_bits) / BYTE`, following ARM's convention in this [website](https://support.arm.com/documentation/ddi0596/2021-06/Shared-Pseudocode/AArch64-Functions?lang=en), this means:
    1. For a WORD Vector like `[0x11111111, 0x33333333, 0x55555555, 0x77777777]`, both Predicate Masks `[0xFFFF]` or `[0x1111]` effectively activate all four elements of the WORD vector because only the rightmost bit of every 4-bit-group starting from the right is associated with the activation of a WORD element (note: `0x1 == 0b0001`)
    2. For a BYTE vector, `(esize / BYTE) = 1` so each predicate bit corresponds to exactly one byte of the BYTE vector
    3. For general vectors of `esize`-byte elements, `SVL / esize` predicate bits are needed to control all elements

## Instruction Unit Testing
- Specify a vector of instructions to `UnrollPathConn()`
- This unrolls transitions and constraints where:
    1. The conditions to make each particular instruction decode **is automatically generated**
    2. The instructions in the list run one after another forming a connected transition path
- Observation: if we manually constrain `pstate_sm` or `pstate_za` to false before our SME instruction is supposed to execute, Z3 **cannot auto-generate** conditions to make `SME_ON=true` so, returns `unsat` because instruction can't decode

## Preventing Z3 Garbage Initialization
- Explicitly constrain all values to prevent Z3 populating them with garbage
- For ZA, this was done through `cstr_step_slice()` where all untouched addresses are explicitly set to `0x00` to clean up `PrintZa()`'s output for easier empirical verification

## Optimizing Load Store Operations on ZA
- Functions like `CombineTileWith*Vector()` follow the pattern:
    1. First, read all the required bytes from old memory
    2. Then, process all of the data inside register space
    3. Finally, accumulatively-store the updated data into the state
- Otherwise, initial naive `read, modify, and accumulate-store` per iteration is too complex for Z3 to solve in a reasonable amount of time because subsequent reads need to consider whether they are reading a previously stored value, causing deeply nested internal `Ite()` branching

# Z3 Timeout Cases and Solutions ( + Confusions marked with TODO:)
Z3 timed out during `UnrollPathConn` in some cases, below lists the bottlenecks and patterns to address each

## `CombineTileWith*Vector()`: Storing to somewhere we are about to Load WITHIN the same loop
- **problem:** future iterations need to reason whether their read slice was previously written in the past or not
- **solution (see current code):** first read from old `mem`, then update things, finally propagate changes to `new_mem`
```cpp
// PROBLEMATIC
ExprRef ArmSme::CombineTileWithHorizontalVector(...) {
    NumericType dim = Z_REG_WIDTH / element_size_bits;

    auto new_mem = mem; // 1. saves new_mem
    for (size_t row = 0; row < dim; row++){
        auto hor_slice = _GetTypedHorizontalSlice(new_mem, BvConst(row, ZA_ADDR_WIDTH), tile_idx, element_size_bits);
        // 2. perform modification
        for (size_t col = 0; col < dim; col++){
            auto old_elem = GetElementInVectorFromLSB(hor_slice, col, element_size_bits);
            auto extra_elem = GetElementInVectorFromLSB(vec, col, element_size_bits);
            ExprRef row_col_activated = (GetBitFromLSB(row_pred, row) != 0) & (GetBitFromLSB(col_pred, col) != 0);
            auto new_elem = Ite(row_col_activated, combine_fn(old_elem, extra_elem), Ite(is_zero_mode, BvConst(0, element>
            hor_slice = SetElementInVectorFromLSB(hor_slice, col, element_size_bits, new_elem, Z_REG_WIDTH);
        }
        // 3. updates new_mem
        new_mem = _SetTypedHorizontalSlice(new_mem, BvConst(row, ZA_ADDR_WIDTH), tile_idx, element_size_bits, hor_slice);
    }
    return new_mem;
}
```

## `IntegerCombineTileWithMatrices`: Innermost loop built an AST with many Load() nodes
- **insight:** commenting out the `Ite(activated, sum + prod, sum)` fixed the timeout, but why?
- **problems:** <!-- TODO: have not identified all the problems thoroughly, focusing on empirically working solution -->
    1. `sum` is a big AST of `Extract` operations on concatenated `Load` operations where `ZA`, being a `MemState`, is modelled as a functional array (ie., nested `Ite` tree of `Stores`, `addr`, etc)
    2. `sum@1 = Ite(activated, sum@0 + prod, sum@0)` builds an AST where left and right child depends on previous `sum`
    3. `activated` is built from `Extract` operations on `row_pred`, `col_pred` which are also nested `Ite` trees of `p_regs[i]`. Similarly, `op1`, `op2` depend on `vec1`, `vec2` which are also nested `Ite` trees of `z_regs[i]`
- **solutions (see current code):**
    1. use `sum` sparingly, delegate the complex arithmetic to a newly-instantiated `BvConst(0, element_size_bits)`, that does not come with a big memory tree, and only combine them together at the very end
    2. pre-extract both `op`s (extract Vector elements and `SExt` or `ZExt`) and predicate bits into an `std::vector` and inner loop only references those pre-extracting `ExprRef`s (optional) <!-- TODO: why does this matter? -->
```cpp
// PROBLEMATIC
ExprRef ArmSme::IntegerCombineTileWithMatrices(...) {
    NumericType dim = Z_REG_WIDTH / element_size_bits;
    auto new_mem = mem;
    for (size_t row = 0; row < dim; row++){
        auto hor_slice = _GetTypedHorizontalSlice(new_mem, BvConst(row, ZA_ADDR_WIDTH), tile_idx, element_size_bits);
        for (size_t col = 0; col < dim; col++){
            auto sum = GetElementInVectorFromLSB(hor_slice, col, element_size_bits);
            for (size_t k = 0; k < 4; k++){
                auto activated = (GetBitFromLSB(row_pred, 4*row+k) != 0) & (GetBitFromLSB(col_pred, 4*col+k) != 0);

                NumericType sub_element_size_bits = element_size_bits / 4;
                auto op1 = GetElementInVectorFromLSB(vec1, 4*row+k, sub_element_size_bits);
                op1 = op1_unsigned ? ZExt(op1, element_size_bits) : SExt(op1, element_size_bits);
                auto op2 = GetElementInVectorFromLSB(vec2, 4*col+k, sub_element_size_bits);
                op2 = op2_unsigned ? ZExt(op2, element_size_bits) : SExt(op2, element_size_bits);
                auto prod = op1 * op2;
                if (sub_instead_of_add) prod = -prod;
                sum = Ite(activated, sum + prod, sum); // 3. left and right expression contain `sum`
            }
            // update hor_slice with new sum
            hor_slice = SetElementInVectorFromLSB(hor_slice, col, element_size_bits, sum, Z_REG_WIDTH);
        }
        // get new_mem by updating the entire horizontal slice
        new_mem = _SetTypedHorizontalSlice(new_mem, BvConst(row, ZA_ADDR_WIDTH), tile_idx, element_size_bits, hor_slice);
    }
    return new_mem;
}
```
