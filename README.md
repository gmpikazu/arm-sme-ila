# Project Plan
## TODO
> STR test, SVE2 test, FP structural test (please don't timeout)
- what is difference between `s.add(u.GetZ3Expr() == cstr)` and `u.AddStepPred() + u.Unroll`? and `s.Add(u.Equal(..))` constrain future `Load` using previous `Store` concretely first, before moving on to associative list in the unroller

## IMPORTANT
- floating point UFs is terrible to test (tedious to constrain each input/output)
- the tests now are a bit hardcoded assuming SVL=128, else it breaks

#### My UFs DRAM Idea:
1. `Write` updates DRAM `MemState` with `DRAM_ADDR_WIDTH` **and** `wb_svl_vector`, `wb_base_addr` for UFs
2. `Read` either reads from DRAM `MemState` or UFs controlled by a boolean
- maintain (1) SVL-bit write vector, (2) esize_bits, (3) base+offset that starts the write
- then in testing, use Z3 to constrain step[i+1] UFs to produce results based on prev write **and** carry over the last couple writes that didnt get overwritten
> This is NOT POSSIBLE, since we need to constrain everything BEFORE running `s.check()` but `u.GetZ3Expr` needs solver to already be done (JUST USE MEMSTATE??)

## Crucial Clarification (in meeting)
- loading/storing BYTE,..,QUAD has no alignment check? meaning we can read across cache lines?

## Remaining Tasks
- Unit tests for SVE2 instructions
- Check ARM pseudocode for Floating Point blackbox instructions (similar to checking `ElemP[]` implementation)
    - eg., neg_fn, fmac_fn, fdotadd_fn, etc
- Optimize `K2` FP instructions using `delta` then `sum` pattern <!-- TODO: optimizations require checking the true ARM pseuducode to see whether we can split the logic into `delta` and `sum` -->
- Check each instruction and their inputs for proper `SExt` or `ZExt`
- Check `ExprRef` arithmetic, make sure they are extended before operation **to prevent truncation**
- Test edge cases of `XZR`, `WZR` access and write
- Verify instructions by constructing unit tests, then integration tests (eg., {ZERO, MOVA, SMOPA})
## Differences From ARM SME Document
- Store instructions always write to memory regardless of `active` predicate, it's just that `inactive` elements are written exactly as they were initially in DRAM. ARM says `inactive` elements shouldn't write memory (but in this case the memory was updated to its initial value so does it matter?)
- Instructions always execute the happy path while `faults` state is incremented when the fault condition is true so state changes still proceed even during fault
    > `LD1` instructions don't check `ConstrainUnpredictableBool` before checking `SPAlignment`
> No information on `REVD`'s `Reverse(element, swsize)` internal behavior (modelled by assumption instead)
- A64 instructions like `MSR` and `SMSTART SM` aren't modelled completely down to each bit
- `Ws`, `Wv` in ARM only selects registers `W12-W15`, but this ILA allows selecting any `W` register
    - **Solution:** limit `Rs`, `Rv` to 2-bits and prepend `011` in `BasePlusOffset` function
    - and make `Rs`, `Rv` two bits **NOT ALWAYS 2 bits** need to really check...
- Size suffix decoding is the reponsibility of the caller who decides which instruction type (.B .H ..) to execute, the model's ILA behavior does not adjust its logic depending on a `size` `ExprRef` runtime variable:
    - `SCLAMP`, `UCLAMP` have their `size` suffix (.B .H ..) embedded into the instruction type, hence the ILA behavior does not attempt to compute an `esize` `BvExpr` at runtime since the helper functions require concrete C++ integers
    - `PSEL` runtime decoding of `i1:tszh:tszl` is left to the caller, the ILA behavior only limits the bit-width of the `Imm` field according to the `size` suffix (.B .H ..) embedded into the instruction type
        - Replacing `i1:tszh:tszl` immediate extraction with immediates among `Imm1,Imm2,Imm3,Imm4` is valid because the freedom of choosing bits in the immediate is the same (constraining `tszh:tszl` for decode, doesn't constrain the range of immediates that can be used in `imm5<4:>` since `i1` is free)
- The implemented ZA and DRAM is big endian. Though the model is self-consistent (and `PrintZa` still prints according to ARM SME convention), a byte dump between the model's ZA and ARM SME's will differ in endianness
- TME not modelled for `STR`, `LDR`
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
- Widths and sizes are given in bits (eg., `SVL`, `BYTE`, `HALF`, `esize`)
- `UpdateSingle`-prefixed functions perform `instr.SetUpdate()` internally so **does not** support updating multiple changes at once (use lower-level helpers instead)

## Temporary Quirks
- `BaseRegPlusImm` extends to `TEMP_LARGEST_ADDR_WIDTH` then the Tile Helpers perform modulo (other helpers perform modulo before calling other functions)
- Modulo (constraining the input) is sometimes the responsibility of the caller and other times the callee

## DRAM Implementation
- The current DRAM implementation involves both a `MemState` and Uninterpreted Function called `DRAM_UF`
    - `USE_DRAM_MEMSTATE` global flag controls whether bytes are read from `DRAM_UF` or `MemState`
    - Whether `DRAM_UF` or `MemState` is used does not break `cstr_step` helpers for read constraints during testing
    - However, setting `USE_DRAM_MEMSTATE=false` means all DRAM reads are unaffected by DRAM writes (`MemState` store)
- `DRAM_is_LE` flag is set in `arm::ArmSme`'s constructor and changes DRAM's endianness for the model at compile time
- `DRAM_Read`-related functions exists as the model's internal helper and also as a public interface for testing ease
    - The public version takes in `element_size_bits` instead of `esize_bytes` to accept `BYTE`, `HALF`, .. macros
    - `Read` optionally converts the data from DRAM endianness to ZA endianness
    - `GetByte` **does not** care about endianness
- `DRAM_Write`-related functions are currently implemented as an atomic store of `SVL` bits since it also updates an entire SVL-bit-wide `wb_svl_vector` to capture what was written to DRAM on that step
    - `Write` optionally converts the data from ZA endianness to DRAM endianness
    - `wb_svl_vector` is read from MSB to LSB, starting from base address and going up to higher addresses
    - It also updates `wb_base_addr` which stores the DRAM `base_addr` of the SVL-bit write

## GPRs (X registers & W registers)
- There are 31 GPRs in Base A64, X registers are 64-bit, W registers are 32-bit lower half of X registers
- `GPRs` array can be indexed up to `idx=30`, but ARM defines `idx=31` to be among `XZR`, `WZR`, `SP` (stack pointer)
- `Get(64|32)BitGPR` helpers return the corresponding zero register (`XZR`, `WZR`) or stack `SP` depending on a `bool`

## ZA Storage
**Representation:**
- `SVL_B`x`SVL_B` matrix represented as a linear array of `BYTE`s
- Smallest unit of data in ARM is `BYTE`
- `ZA[row][col]` is expanded into C-style pointer arithmetic indexing, where top-left element is index `[0][0]` and bottom-right element is index `[SVL_B-1][SVL_B-1]`

**Complying with ARM's Convention:**
- ARM SME views the matrix with top-right element being index `[0][0]` and bottom-left element being index `[SVL_B-1][SVL_B-1]` (ie., index 0 starts at topmost row or rightmost colummn)
- This convention is enforced by the helper functions accessing ZA while the internal ZA storage actually has top-left element at index `[0][0]` and bottom-right element at index `[SVL_B-1][SVL_B-1]`

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
- Created a Ctest-inspired `CHECK()` function that performs the necessary setup using a `std::function` argument, then unrolls, and verifies using another `std::function` argument

## Fault Checking
- Faults are modeled with an additional `faults` state attached to the model that is incremented on each fault
- For `LDR`, `STR` instructions, misalignment is **always** treated as fault (though ARM says it's optional)
- For `LD1`, `ST1` instructions, ARM says SP (stack pointer) misalignment is definitely a fault
- The `CHECK()` function inspects the `faults` state at every step and fails if `faults > 0`

## Preventing Z3 Garbage Initialization
- Explicitly constrain all values (including those we do not care about) to prevent Z3 populating them with garbage
- For ZA, this was **initially** done through `cstr_step_slice()` where all untouched addresses are explicitly set to `0x00` to clean up `PrintZa()`'s output for easier empirical verification (this helper **only supports** constraining **a single slice** due to immediately zeroing out everything else, use **new idiom below** for multiple constraints)
- Later, the `track_slice()` and `cstr_all_tracked_and_zero()` idiom was introduced to track multiple slices with newer ones overwriting previous ones, then finally zeroing out remaining addresses that was not constrained
    - `track_slice()` updates an `std::unordered_map<size_t, z3::expr>` to associate an address with the corresponding `ilang::ExprRef` that represents the constrained byte
    - after accumulating many `track_slice()` (with future constraints overwriting the past ones), `cstr_all_tracked_and_zero()` enforces the constraints defined in the `std::unordered_map` and zeroes out other untouched addresses
    - **Example Usage For Generating Multiple Slice Constraints:**
    ```cpp
    Tracker t; // std::unordered_map<size_t, z3::expr>
    // each new layer is applied on top of previously applied layer
    track_slice(t, bv_val_128(ctx, 0x0001020304050607ULL, 0x08090A0B0C0D0E0F), 0, 0, false, BYTE);
    track_slice(t, bv_val_128(ctx, 0xAAAABBBBCCCCDDDDULL, 0x1111222244445555), 0, 2, true, BYTE); 
    track_slice(t, bv_val_128(ctx, 0x0001020304050607ULL, 0x08090A0B0C0D0E0F), 7, 0, false, BYTE);
    cstr_all_tracked_and_zero(s, u, ctx, t, sme); // enforces the constraint and zeroes the rest
    ```

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

## `MaskWithSinglePredicate`: Each call to `SetElementInVector` inside a loop performs `Extract` and `Concat`, future `SetElementInVector` has to traverse nested `Extract`-`Concat` trees
- **problem:** future iterations that call `SetElementInVector` performs `Extract` and `Concat` on a `BvExpr` that is already a compounded `Extract`-`Concat` tree. Even if this `BvExpr` was originally set to `BvConst(0, vector_length_bits)`, the operations compounded into a big AST expression
- **solution (see current code):** store an `std::vector<ExprRef>` containing the elements of the new vector, then build it using `Concatenate(std::vector)` to get a `BvExpr` without deep trees
- **note:** this bottleneck exists in multiple helper functions but Z3 timeout first appeared during `MaskWithSinglePredicate` on DRAM-related vectors (`CombineTileWith*Vector` and other helpers also have repeated `SetElementInVector` pattern but is not currently a major issue)
```cpp
// PROBLEMATIC
ExprRef ArmSme::MaskWithSinglePredicate(...) {
    NumericType num_elements = vector_length_bits / element_size_bits;
    ExprRef result = BvConst(0, vector_length_bits);
    for (size_t i = 0; i < num_elements; i++){
        ExprRef source_element = GetElementInVectorFromLSB(source, i, element_size_bits);
        ExprRef dest_element = GetElementInVectorFromLSB(dest, i, element_size_bits);
        ExprRef is_activated = (GetPredBitFromLSB(predicate, i, element_size_bits) != 0);
        auto new_elem = is_zero_mode ? Ite(is_activated, source_element, BvConst(0, element_size_bits)) : Ite(is_activated, source_element, dest_element);
        result = SetElementInVectorFromLSB(result, i, element_size_bits, new_elem); // 1. performs extract concat
    }
    return result; // 2. compounded extract-concat tree
}
```
