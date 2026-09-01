# Project Notes
## Codebase Quirks
**Modulo Responsibility:**
- Some functions require the caller to perform modulo before calling (eg., `GetPredBitFromLSB`) and asserts that the parameters are within bounds
- Other functions (eg., Typed Slice Helpers) perform the modulo internally, caller modulo is optional
- Constraining the bit-width of an `ilang::ExprRef` naturally constrains the set of values it can possibly take

**Unit Tests Assume a Certain SVL:**
- Bit vector constraints are written using hexadecimal and only span up to a certain number of `SVL` bits
- Only `REVD.Q` unit test uses 256-bit `SVL`, others expect 128-bit `SVL`

**`Z_REG_WIDTH` and `SVL` are Equivalent:**
- They are used interchangeably in the codebase but are the same value
- Since a `Z` vector register, by definition, must be able to hold an entire `SVL`-bit vector

**Floating Point UFs and Sort Refs:**
- FP operations are modelled in the ILA as Uninterpreted Functions (`ilang::FuncRef`) where the bit vector inputs symbolizing IEEE bit vectors are `ilang::SortRef`s
    - Since ILAng identifies `SortRef`s by their bit-width, BFloat16 and Half Precision (`fp16`) bit vectors will be treated as the same `SortRef` when the UFs are left uninterpreted
- Through UF substitution, there is a way to turn those bit vectors into `Z3 FPA` expressions involving exponent bits and significand bits following IEEE standard, only in this case will BFloat16 be different from FP16

## ARM SME Quirks
**`LD1`, `ST1` Performs Base Alignment Check for SP Only:**
- If the base register is not `SP` then alignment is not checked, may access cache boundaries
- `LDR`, `STR` instructions, on the other hand, **always** check their base address alignment to a multiple of the smallest `SVL_B` (ie., 16) so it always accesses an entire `SVL`-bit vector from an aligned base address

## ILA Differences Against ARM SME Document
1. System-wide States
    - Instructions assume SME and SVE extensions are both present since the decode uses `BoolConst(true)`
    - `MSR` instruction skipped, ILA only includes relevant `SMSTART`, `SMSTOP` aliases that control bits used in other instructions (ie., `PSTATE.SM`, `PSTATE.ZA` for SVE Streaming Mode and ZA Tile Storage Activation)
    - A compile-time constant `SVL` is used throughout and a separate `VL` is not defined
    - Traps and exceptions levels are not modelled in the ILA, instructions **only** check `PSTATE.SM`, `PSTATE.ZA` whether SVE Streaming Mode and ZA status are enabled, they ignore exceptions levels and other quirks
    - The only exception modelled is `SP` alignment and `(base + offset)` address alignment in Load/Store instructions
    - Additional features (eg., watchpoints, transactional memory extension) are not present in the ILA
2. Endianness
    - The implemented ZA and DRAM is big endian. Though the ILA is self-consistent (and `PrintZa` still prints according to ARM SME convention), a byte dump between the model's ZA and ARM SME's will differ in endianness
3. Hardware Decoding Specifics
    - Vector select register names, `Ws`, `Wv`, in ARM can only select registers `W12-W15`, but this ILA allows selecting any `W` register
        - ARM hardware actually limits `Rs`, `Rv` fields to 2-bits and prepends `011` to the input
        - Current unit tests forgot to consider only using `W12-W15` so future development should take care to use the correct register names
    - Size decoding (`.B`, ..., `.Q`) is baked into the ILA instruction name, not something to reasoned internally before determining `element_size_bits`
        - `SCLAMP`, `UCLAMP` has no size `T` input that gets decoded at runtime, sizes are in the name
        - `PSEL` has no `tszh`, `tszl` input, but `imm`'s bit-width extraction still behaves as if it was extracted from `i1:tszh:tszl`
            - Since `i1` is a free bit, `imm` takes the form of `Imm1` (for `.D`), `Imm2` (for `.S`), `Imm3` (for `.H`), and `Imm4` (for `.B`)
4. SVE2 Instructions
    - The PDF contains no information on `REVD`'s `Reverse(element, swsize)` internal behavior (assumed it swaps 64-bit halves since `swsize=64` is fixed)
5. Load/Store Instructions
    - Transactional Memory Extension (TME) not modelled for `STR`, `LDR`
    - `LD1` instructions do not check `ConstrainUnpredictableBool` before checking `SPAlignment`
    - Though ARM says `inactive` elements must not touch DRAM, store instructions always write `SVL` bits to DRAM regardless of each element's `active` predicate (due to `MemState` and `WB_svl_vector` coexisting)
        - By first saving `old_dram_elem` and writing the original DRAM element if the source element is `inactive`, the ILA achieves the same effect as if the source element did not touch DRAM
6. Floating Point Behavior
    - Floating point exceptions and Floating Point Status Register (FPSR) are not implemented
    - Floating Point Control Register (FPCR), used to alter FP/BF behavior, is not modelled in the ILA
    - Extended BFloat16 Behaviors (EBF) is skipped, only standard behavior is modelled (see Section B3.1.2.3):
        - BF operations **do not** flush denormalized inputs to zero according to `FPCR.FZ` control
        - BF instructions **do not** perform fused two-way dot and add without intermediate rounding
    - Additional FP behaviors in modifying ZA (eg., exceptions, default NaN values, rounding modes, flush) are skipped
    - IEEE UF substitutions defined in `test_helpers.cc` attempts to stay true to ARM SME but has flaws:
        - `*mac` functions use Z3's native Fused Multiply and Accumulate with no intermediate roundings
        - `*dotadd` functions do not have a native Z3 way to skip intermediate roundings, both FP/BF `*dotadd` functions widen the inputs to 64 bits first and explicitly round at the very end (but this does not guarantee infinite precision whilst computing in 64 bits)
        - ARM says that for `*dotadd`, BF standard behavior (without EBF) performs intermediate rounding while FP standard behavior computes without intermediate rounding, however, the ILA currently does not support avoiding intermediate rounding for FP `*dotadd` functions due to the lack of infinite precision floating point in Z3
    - `BFAdd`, `BFMul` is not found in the ARM PDF but there are some clues in this [webpage](https://support.arm.com/documentation/111108/2026-06/Shared-Pseudocode/shared-functions-float?lang=en)
    - Future work could make `BFDotAdd`, `FPDotAdd_ZA`, `FPMulAdd_ZA` into higher-level calls that orchestrate FPCR, FPSR, and additional FP/BF behavior logic before calling primitive UF substitutions that will peform IEEE computation

---

## Delayed Simple Tasks
- Not all instructions require Streaming SVE Mode, some only need ZA
- `SMSSTART/STOP` on/off zeroing behavior (B1.1.1 and E2 pseudocode of SM,ZA states)
- `TEMP_LARGEST_ADDR_WIDTH` for `BaseRegPlusImm` should be what?

---

# Implementation Overview
## Code Conventions
- Widths and sizes are given in bits (eg., `SVL`, `BYTE`, `HALF`, `esize`) unless indicated otherwise
- Helpers that take `InstrRef&` as an argument perform `instr.SetUpdate` internally and must be used cautiously (eg., calling multiple of them sequentially **may not** produce expected results)
    - For example, `UpdateSingle`-prefixed functions **do not** support updating multiple state changes at once, stacking calls together will just make the solver `unsat` since future constraints clash with earlier ones

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

**Initial Motivation (unsuccessful but forward looking):**
- `MemState` and `WB_svl_vec` coexist to potentially completely replace `MemState` with `DRAM_UF` and persist the stores to DRAM by capturing `WB_svl_vec` and using it to constrain `DRAM_UF` reads in the next step, based on the base address reflected in the current `WB_base_addr`
- Tried maintaining a hashmap of previous DRAM `(addr, byte_val)` mapping and only updating the ones that were written to based on `WB_base_addr` and carrying over the unmodified ones but that would require knowing the concrete value of `WB_base_addr` before even calling `s.check()`
- Also, UFs are not differentiated by steps, so constraining different values at different steps leads to `unsat`

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
- Specify a vector of instruction names to `UnrollPathConn(std::vector<std::string>)`
- This unrolls transitions and constraints where:
    1. The conditions to make each particular instruction decode **is automatically generated**
    2. The instructions in the list run one after another forming a connected transition path
- Observation: if we manually constrain `pstate_sm` or `pstate_za` to false before our SME instruction is supposed to execute, Z3 **cannot auto-generate** conditions to make `SME_ON=true` so, returns `unsat` because instruction can't decode
- Created a Ctest-inspired `CHECK()` function that performs the necessary setup using a `std::function` argument, then unrolls, and verifies using another `std::function` argument

**[IEEE-Hex-Binary Converter (fp16, ..., fp128)](https://numeral-systems.com/ieee-754-converter/)**:
- For BFloat16 just use upper bits of FP32 (single precision)

## Z3 Insights
- Internal States can only change between steps if explicitly set in `instr.Update`
- Input States **always** changes between steps
- Solving (`s.check()`) is Z3's processs of filling the symbolic values with concrete ones to satisfy all `instr.Update` transition constraints and external constraints added by `cstr_step` helpers

## Fault Checking
- Faults are modeled with an additional `faults` state attached to the model that is incremented on each fault
- Instructions always execute the happy path while `faults` is incremented whenever the error condition is triggered. Hence, state changes still proceed as if there were no errors, but `faults` clearly indicate errors
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

## Z3 Floating Point Arithmetic
[Z3 API Documentation](https://z3prover.github.io/api/html/classz3_1_1expr.html#aa460b1ef4dde33c6ff10fbae306dc6b8)
[Z3 Source Definitions](https://z3prover.github.io/api/html/z3_09_09_8h_source.html#l04685)

**FPA Sorts (FP16, FP32, BFloat16):**
- `fpa_sort(ebits, sbits)` takes number of exponent bits and significand bits to construct floating point sort
    - The number of bits is counted mathematically, meaning that `sbits=24` or FP32 (includes implied leading  bit)
     ```
     1 bit      8 bits         23 bits
    +-------+-----------+------------------+
    | sign  | exponent  | fraction (stored)| # 23 bits are stored in significand, but mathematically is 24 bits
    +-------+-----------+------------------+
    ```
    - Z3's header defines `fpa_sort<16/32/64>` for FP16/32/64 sorts but it does not include BFloat16
- BFloat16 negation just `XOR`s the most significant bit (following IEEE standard of sign bit)
- BFloat16 widening to FP32 pads additional 16 zeroes to BF16's LSB side, since FP32's upper 16 bits is BFloat16

**Z3 FPA Functions:**
- All FP operations will perform normalization and rounding when needed, therefore require `rounding_mode`
- `z3::fpa_to_fpa(input_fpa, new_fpa_sort)` converts any `input_fpa` to `new_fpa_sort`
- `input_fpa.mk_to_ieee_bv()` dumps the bit vector of this `input_fpa`
- `input_bv.mk_from_ieee_bv(fpa_sort)` interprets the bit vector as an `fpa_sort`
- `ctx.fpa_rounding_mode()` gets the rounding mode of target `z3::context`
- `ctx.set_rounding_mode(z3::RNE)` sets rounding mode to Round Nearest Even (there are other options too)
- `z3::fma(fa, fb, fc, rounding_mode)` computes `fa*fb + fc` using the `rounding_mode` (pass in `ctx.fpa_rounding_mode`)

**Replacing Uninterpreted Functions in `tr` (constraints) Generated by `ilang::UnrollPathConn`:**
- ARM SME model contains placeholder UFs (uninterpreted functions) for Floating Point operations
- The constraints, `tr`, produced by `ilang::UnrollPathConn()` is a tree formed by `And()`-ing constraints together, where each application node (eg., UFs) contain children which are their own function arguments
- By recursively traversing starting at the root, `_recursive_substitute` replaces all Floating Point UF nodes with a concrete body, producing a modified version of `tr` free from placeholder UFs that goes into the solving stage
- The implementation uses Memoization to avoid exponential time complexity, taking into account that Z3 refers to structurally-identical sub-trees as one thing; this unique identifier is used a key to an `std::unordered_map` cache

*Procedure Breakdown (to substitute a single application node):*
    1. Build a template body `B` that contains placeholder `hole[i]` leaves, which will be filled later on
        - Holes are created with `Z3_mk_bound(ctx, idx, SORT)` where a designated `idx` *label* is specified
    2. Gather the actual non-placeholder arguments of the UF into a `z3::expr_vector` called `args_vec`
    3. Call `B.substitute(args_vec)` to fill each `hole[i]` with corresponding `f.args(i)`, producing new `z3::expr`
        - `B.substitute(...)` maps each **positional-indexed** `f.args(i)` to the **label-indexed** `hole[i]`
    4. This new `z3::expr` is the new replaced node, doing this recurisvely replaces an entire sub-tree in `tr`

---

# Z3 Timeout Cases and Solutions
- The unit tests (ie., the `CHECK` helper) **do not** utilize `ilang::UnrollMonoConn` since doing so unrolls the constraints of the entire model at once, potentially over several steps, which timeouts during unrolling or solving stage
- Instead, `ilang::UnrollPathConn` was used, which only unrolls the constraints for a specific sequence of instructions and automatically sets the decodes before executing each instruction
- Additionally, the tests **do not** set up decode conditions for the target instruction sequence since those constraints are auto-generated by `UnrollPathConn` (eg., setting `pstate_sm = true`, `pstate_za = true` before executing)
- Even with a smaller AST from `ilang::UnrollPathConn`, Z3 still timed out in some cases, below lists the bottlenecks encountered throughout development and patterns implemented to address each problem

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
- **problems:**
    1. `sum` is a big AST of `Extract` operations on concatenated `Load` operations where `ZA`, being a `MemState`, is modelled as a functional array (ie., nested `Ite` tree of `Stores`, `addr`, etc)
    2. `sum@1 = Ite(activated, sum@0 + prod, sum@0)` builds an AST where left and right child depends on previous `sum`
- **solutions (see current code):**
    1. use `sum` sparingly, delegate the complex arithmetic to a newly-instantiated `BvConst(0, element_size_bits)`, that does not come with a big memory tree, and only combine them together only at the very end
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
