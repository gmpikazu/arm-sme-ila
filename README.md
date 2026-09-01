# Project Notes
## Note on Unit Tests
**Unit Test Expected Output:**
- The expected outputs (seen in `EXPECT_TRUE`) of the unit tests **were not** taken from a simulated ARM SME program
- They were designed to ensure the implemented ILA semantics matched the descriptions in the ARM SME PDF document

**Chaining Instruction Sequence:**
- Using `ilang::UnrollPathConn`, a sequence of instructions can be unrolled consecutively starting from `step 0`
- A notable example is `test/test_sve.cc` where `REVD.Q` is followed by a `MOVA_V2T` instruction, where `REVD.Q` uses the input states constrained at `step 0` to update the ILA in `step 1`, then `MOVA_V2T` uses the input states constrained at `step 1` to update the ILA in `step 2`, propagating the changes from `REVD.Q` in the earlier `step 1`
- This can be scaled to a whole program as long as the required instructions are supported in the ILA

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

**Amazing Tool: [IEEE-Hex-Binary Converter (fp16, ..., fp128)](https://numeral-systems.com/ieee-754-converter/)**:
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

# Running the Project
**Build and Run:**
```bash
mkdir -p build & cd build
cmake ..  & cmake --build . -j$(nproc)
./main # run the executable named 'main'
```

**Sample Output of `main`:**
(the state variables and instruction list shown here are collapsed for readability)
```
» ./main

State variables:
  faults (8 bits) 
  ZA (-1 bits) PSTATE_SM (-1 bits) PSTATE_ZA (-1 bits) 
  SP (64 bits)
  DRAM (-1 bits) WB_svl_vector (128 bits) WB_base_addr (128 bits)
  z0 (128 bits) z1 (128 bits) z2 (128 bits) z3 (128 bits) z4 (128 bits) z5 (128 bits) z6 (128 bits)
  z7 (128 bits) z8 (128 bits) z9 (128 bits) z10 (128 bits) z11 (128 bits) z12 (128 bits) z13 (128 bits)
  z14 (128 bits) z15 (128 bits) z16 (128 bits) z17 (128 bits) z18 (128 bits) z19 (128 bits) z20 (128 bits)
  z21 (128 bits) z22 (128 bits) z23 (128 bits) z24 (128 bits) z25 (128 bits) z26 (128 bits) z27 (128 bits)
  z28 (128 bits) z29 (128 bits) z30 (128 bits) z31 (128 bits)
  p0 (16 bits) p1 (16 bits) p2 (16 bits) p3 (16 bits) p4 (16 bits) p5 (16 bits) p6 (16 bits) p7 (16 bits)
  p8 (16 bits) p9 (16 bits) p10 (16 bits) p11 (16 bits) p12 (16 bits) p13 (16 bits) p14 (16 bits) p15 (16 bits)
  x0 (64 bits) x1 (64 bits) x2 (64 bits) x3 (64 bits) x4 (64 bits) x5 (64 bits) x6 (64 bits) x7 (64 bits)
  x8 (64 bits) x9 (64 bits) x10 (64 bits) x11 (64 bits) x12 (64 bits) x13 (64 bits) x14 (64 bits) x15 (64 bits)
  x16 (64 bits) x17 (64 bits) x18 (64 bits) x19 (64 bits) x20 (64 bits) x21 (64 bits) x22 (64 bits) x23 (64 bits)
  x24 (64 bits) x25 (64 bits) x26 (64 bits) x27 (64 bits) x28 (64 bits) x29 (64 bits) x30 (64 bits)

Instructions created:
  SMSTART SMSTOP 
  MOVA_T2V.B MOVA_T2V.H MOVA_T2V.S MOVA_T2V.D MOVA_T2V.Q
  MOVA_V2T.B MOVA_V2T.H MOVA_V2T.S MOVA_V2T.D MOVA_V2T.Q
  ZERO
  ADDHA.S ADDHA.D 
  ADDVA.S ADDVA.D
  SMOPA (8b->32b) SMOPA (16b->64b)
  SMOPS (8b->32b) SMOPS (16b->64b)
  SUMOPA (8b->32b) SUMOPA (16b->64b)
  SUMOPS (8b->32b) SUMOPS (16b->64b)
  UMOPA (8b->32b) UMOPA (16b->64b)
  UMOPS (8b->32b) UMOPS (16b->64b)
  USMOPA (8b->32b) USMOPA (16b->64b)
  USMOPS (8b->32b) USMOPS (16b->64b)
  ADDSPL
  ADDSVL
  RDSVL
  BFMOPA (bf16->fp32)
  BFMOPS (bf16->fp32)
  FMOPA (fp16->fp32)
  FMOPS (fp16->fp32)
  FMOPA (fp32)
  FMOPS (fp32)
  FMOPA (fp64)
  FMOPS (fp64)
  LD1.B LD1.H LD1.S LD1.D LD1.Q
  ST1.B ST1.H ST1.S ST1.D ST1.Q 
  LDR STR 
  PSEL.B PSEL.H PSEL.S PSEL.D 
  REVD.Q 
  SCLAMP.B SCLAMP.H SCLAMP.S SCLAMP.D 
  UCLAMP.B UCLAMP.H UCLAMP.S UCLAMP.D


=== Test: SMSTART sets pstate and resets SME/SVE states ===
  [INSTRS] start --> SMSTART --> done
  [UNROLL] unrolling... DONE (took 30 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 15 ms)
 initial ZA
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R0
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R1
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R2
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R3
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R4
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R5
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R6
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R7
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R8
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R9
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R10
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R11
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R12
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R13
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R14
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R15
└------------------------------------------------─┘
 should be zeroed out
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 garbage vectors
LOG["P[1] @ 0"] : #xffff
LOG["Z[1] @ 0"] : #xffffffffffffffffffffffffffffffff
 now zeroed out
LOG["P[1] @ 1"] : #x0000
LOG["Z[1] @ 1"] : #x00000000000000000000000000000000
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: SMSTART sets pstate and resets SME/SVE states ===



=== Test: SMSTOP clears pstate and resets ONLY SVE state ===
  [INSTRS] start --> SMSTOP --> done
  [UNROLL] unrolling... DONE (took 3 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 11 ms)
 initial ZA
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R0
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R1
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R2
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R3
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R4
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R5
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R6
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R7
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R8
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R9
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R10
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R11
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R12
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R13
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R14
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R15
└------------------------------------------------─┘
 should still be same as original
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R0
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R1
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R2
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R3
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R4
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R5
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R6
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R7
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R8
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R9
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R10
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R11
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R12
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R13
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R14
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R15
└------------------------------------------------─┘
 garbage vectors
LOG["P[1] @ 0"] : #xffff
LOG["Z[1] @ 0"] : #xffffffffffffffffffffffffffffffff
 now zeroed out
LOG["P[1] @ 1"] : #x0000
LOG["Z[1] @ 1"] : #x00000000000000000000000000000000
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: SMSTOP clears pstate and resets ONLY SVE state ===



=== Test: SHOWCASE: track_slice() + cstr_all_tracked_and_zero() idiom ===
  [INSTRS] start --> ZERO --> done
  [UNROLL] unrolling... DONE (took 573 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 25 ms)
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ 01 02 03 04 05 06 07 08 09 0a 0b 0c 55 0e 0f │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ 55 __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ 44 __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ 44 __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ 22 __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ 22 __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ 11 __ __ │ R6
│ __ 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ dd __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ dd __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ cc __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ cc __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ bb __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ bb __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ aa __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ aa __ __ │ R15
└------------------------------------------------─┘
LOG["vertical slice"] : #xaaaabbbbccccdddd0d11222244445555
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: SHOWCASE: track_slice() + cstr_all_tracked_and_zero() idiom ===



=== Test: SHOWCASE: Multi-byte Vertical Track Slice ===
  [INSTRS] start --> ZERO --> done
  [UNROLL] unrolling... DONE (took 582 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 31 ms)
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ 44 44 55 55 __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ 11 11 22 22 __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ cc cc dd dd __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ aa aa bb bb __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
LOG["vertical slice"] : #xaaaabbbbccccdddd1111222244445555
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: SHOWCASE: Multi-byte Vertical Track Slice ===



=== Test: GetHorizontalSlice constrains underlying ZA memory bytes ===
  [INSTRS] start --> ZERO --> done
  [UNROLL] unrolling... DONE (took 565 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
TIP: we use GetHorizontalSlice() to constrain underlying ZA memory bytes
  [SOLVER] solving... DONE (took 40 ms)
 row 1 will be populated at step 0
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R0
│ 0f 0e 0d 0c 0b 0a 09 08 07 06 05 04 03 02 01 __ │ R1
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R2
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R3
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R4
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R5
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R6
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R7
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R8
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R9
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R10
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R11
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R12
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R13
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R14
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R15
└------------------------------------------------─┘
 row 1 will be zeroed out at step 1
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: GetHorizontalSlice constrains underlying ZA memory bytes ===



=== Test: GetVerticalSlice constrains underlying ZA memory bytes ===
  [INSTRS] start --> ZERO --> done
  [UNROLL] unrolling... DONE (took 585 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 23 ms)
 rightmost vertical slice populated
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f __ │ R0
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 01 │ R1
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 02 │ R2
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 03 │ R3
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 04 │ R4
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 05 │ R5
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 06 │ R6
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 07 │ R7
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 08 │ R8
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 09 │ R9
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0a │ R10
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0b │ R11
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0c │ R12
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0d │ R13
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0e │ R14
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R15
└------------------------------------------------─┘
 no zeroing happens because we set Imm8 = 0x00
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f __ │ R0
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 01 │ R1
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 02 │ R2
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 03 │ R3
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 04 │ R4
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 05 │ R5
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 06 │ R6
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 07 │ R7
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 08 │ R8
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 09 │ R9
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0a │ R10
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0b │ R11
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0c │ R12
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0d │ R13
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0e │ R14
│ 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f 0f │ R15
└------------------------------------------------─┘
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: GetVerticalSlice constrains underlying ZA memory bytes ===



=== Test: Horizontal slices ZA0H.B[1], ZA1H.H[0] have equal bytes ===
  [INSTRS] start --> ZERO --> done
  [UNROLL] unrolling... DONE (took 555 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 20 ms)
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: Horizontal slices ZA0H.B[1], ZA1H.H[0] have equal bytes ===



=== Test: ZERO Imm8=0xFF zeroes entire ZA array that was initialized to non-zero ===
  [INSTRS] start --> ZERO --> done
  [UNROLL] unrolling... DONE (took 561 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 47 ms)
 before zeroing
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R0
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R1
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R2
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R3
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R4
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R5
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R6
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R7
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R8
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R9
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R10
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R11
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R12
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R13
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R14
│ aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa aa │ R15
└------------------------------------------------─┘
 after zeroing
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: ZERO Imm8=0xFF zeroes entire ZA array that was initialized to non-zero ===



=== Test: ZERO Imm8=0x55 zeroes 16-bit element tile ZA0.H which was previously non-zero ===
  [INSTRS] start --> ZERO --> done
  [UNROLL] unrolling... DONE (took 552 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 28 ms)
 everything set to 0xff initially
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R0
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R1
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R2
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R3
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R4
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R5
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R6
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R7
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R8
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R9
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R10
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R11
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R12
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R13
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R14
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R15
└------------------------------------------------─┘
 now all ZA0.H bytes are zeroed
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R15
└------------------------------------------------─┘
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: ZERO Imm8=0x55 zeroes 16-bit element tile ZA0.H which was previously non-zero ===



=== Test: ZERO Imm8=0x84 zeroes 32-bit element tile ZA3.S which was previously non-zero ===
  [INSTRS] start --> ZERO --> done
  [UNROLL] unrolling... DONE (took 563 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 26 ms)
 everything set to 0xff initially
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R0
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R1
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R2
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R3
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R4
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R5
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R6
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R7
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R8
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R9
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R10
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R11
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R12
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R13
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R14
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R15
└------------------------------------------------─┘
 now all ZA3.S bytes are zeroed
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R0
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R3
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R4
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R5
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R8
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R11
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R12
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R13
│ ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: ZERO Imm8=0x84 zeroes 32-bit element tile ZA3.S which was previously non-zero ===



=== Test: MOVA_T2V.S (tile to vector) move ZA3V.S[1] to Z[10] using P[5] (only first and third element) ===
  [INSTRS] start --> MOVA_T2V.S --> done
  [UNROLL] unrolling... DONE (took 936 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 16 ms)
 initialized a WORD at every other row, forming vertical slice
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ 44 44 44 44 __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ 33 33 33 33 __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ 22 22 22 22 __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ 11 11 11 11 __ __ __ __ │ R15
└------------------------------------------------─┘
LOG["Z reg before MOVA_T2V.S"] : #x00000000000000000000000000000000
LOG["Z reg after MOVA_T2V.S"] : #x00000000222222220000000044444444
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: MOVA_T2V.S (tile to vector) move ZA3V.S[1] to Z[10] using P[5] (only first and third element) ===



=== Test: MOVA_V2T.D (vector to tile) move Z[10] to ZA7V.D[1] using P[2] ===
  [INSTRS] start --> MOVA_V2T.D --> done
  [UNROLL] unrolling... DONE (took 204 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 12 ms)
LOG["Z[10] at step 0"] : #x11111111222222223333333344444444
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 ZA updated to contain Z[10] vertically
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ 33 33 33 33 44 44 44 44 __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ 11 11 11 11 22 22 22 22 __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
LOG["ZA7V.D[1] vertical slice at step 1"] : #x11111111222222223333333344444444
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: MOVA_V2T.D (vector to tile) move Z[10] to ZA7V.D[1] using P[2] ===



=== Test: ADDHA.S accumulates horizontally except first row of ZA2.S ===
  [INSTRS] start --> ADDHA.S --> done
  [UNROLL] unrolling... DONE (took 1554 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 12 ms)
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ 01 __ __ __ 01 __ __ __ 01 __ __ __ 01 │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ 02 __ __ __ 02 __ __ __ 02 __ __ __ 02 │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ 03 __ __ __ 03 __ __ __ 03 __ __ __ 03 │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 added 5 to the left column, added 6 to the right column
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ 01 __ __ __ 01 __ __ __ 01 __ __ __ 01 │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ 07 __ __ __ 07 __ __ __ 08 __ __ __ 08 │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ 08 __ __ __ 08 __ __ __ 09 __ __ __ 09 │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ 05 __ __ __ 05 __ __ __ 06 __ __ __ 06 │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: ADDHA.S accumulates horizontally except first row of ZA2.S ===



=== Test: ADDVA.D accumulates vertically on ZA7.D ===
  [INSTRS] start --> ADDVA.D --> done
  [UNROLL] unrolling... DONE (took 83 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 10 ms)
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ 02 __ __ __ __ __ __ __ 01 │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ 02 __ __ __ __ __ __ __ 01 │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 added 5 to bottom row, 6 to top row
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ 08 __ __ __ __ __ __ __ 07 │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ 07 __ __ __ __ __ __ __ 06 │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
LOG["vertical slice on right"] : #x00000000000000060000000000000007
LOG["vertical slice on left"] : #x00000000000000070000000000000008
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: ADDVA.D accumulates vertically on ZA7.D ===



=== Test: UMOPA (8b->32b) correctly computes new diagonal matrix sum using predicates ===
  [INSTRS] start --> UMOPA (8b->32b) --> done
  [UNROLL] unrolling... DONE (took 5024 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 18 ms)
 input vector registers Z[1] and Z[2]
LOG["Zn @ 0"] : #x000102030405060708090a0b0c0d0e0f
LOG["Zm @ 0"] : #x000102030405060708090a0b0c0d0e0f
 ZA initially zeroed out
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 then contains the resulting matrix
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ 90 │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ 51 __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ 24 __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ 09 __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 row slices of ZA1H.S from top to bottom
LOG["ZA1H.S[0] @ 1"] : #x00000000000000000000000000000090
LOG["ZA1H.S[1] @ 1"] : #x00000000000000000000005100000000
LOG["ZA1H.S[2] @ 1"] : #x00000000000000240000000000000000
LOG["ZA1H.S[3] @ 1"] : #x00000009000000000000000000000000
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: UMOPA (8b->32b) correctly computes new diagonal matrix sum using predicates ===



=== Test: SMOPS (16b->64b) subtracts from original zero matrix (input is unsigned) with alternating predicates ===
  [INSTRS] start --> SMOPS (16b->64b) --> done
  [UNROLL] unrolling... DONE (took 361 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 17 ms)
 input vector registers Z[1] and Z[2]
LOG["Zn @ 0"] : #x00010002000300040005000600070008
LOG["Zm @ 0"] : #x00010002000300040005000600070008
 ZA initially zeroed out
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 then contains the resulting matrix
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ ff ff ff ff ff ff ff ba ff ff ff ff ff ff ff 52 │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ ff ff ff ff ff ff ff e2 ff ff ff ff ff ff ff ba │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 row slices of ZA3H.D from top to bottom
LOG["ZA3H.D[0] @ 1"] : #xffffffffffffffbaffffffffffffff52
LOG["ZA3H.D[1] @ 1"] : #xffffffffffffffe2ffffffffffffffba
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: SMOPS (16b->64b) subtracts from original zero matrix (input is unsigned) with alternating predicates ===



=== Test: SMOPS (16b->64b) adds and subtracts original zero matrix (input signed and unsigned) with alternating predicates ===
  [INSTRS] start --> SMOPS (16b->64b) --> done
  [UNROLL] unrolling... DONE (took 366 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 18 ms)
 input vector registers Z[1] and Z[2]
LOG["Zn @ 0"] : #xfffffffefffdfffc0001000200030004
LOG["Zm @ 0"] : #xfffffffefffdfffc0001000200030004
 ZA initially zeroed out
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 then contains the resulting matrix
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ 14 ff ff ff ff ff ff ff ec │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ ff ff ff ff ff ff ff ec __ __ __ __ __ __ __ 14 │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 row slices of ZA3H.D from top to bottom
LOG["ZA3H.D[0] @ 1"] : #x0000000000000014ffffffffffffffec
LOG["ZA3H.D[1] @ 1"] : #xffffffffffffffec0000000000000014
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: SMOPS (16b->64b) adds and subtracts original zero matrix (input signed and unsigned) with alternating predicates ===



=== Test: FMOPA (fp64) computes correct matrix in ZA6.D ===
  [INSTRS] start --> FMOPA (fp64) --> done
  [UNROLL] unrolling... DONE (took 69 ms)
  [SUB UF] IEEE substitution... ENABLED (took 4 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 39 ms)
 input vector registers Z[1] and Z[2]
LOG["Zn @ 0"] : #x40240000000000004034000000000000
LOG["Zm @ 0"] : #x40240000000000004034000000000000
 ZA initially zeroed out
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 then contains the resulting matrix
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ 40 69 __ __ __ __ __ __ 40 79 __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ 40 59 __ __ __ __ __ __ 40 69 __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 row slices of ZA2H.S from top to bottom
LOG["ZA6H.D[0] @ 1"] : #x40690000000000004079000000000000
LOG["ZA6H.D[1] @ 1"] : #x40590000000000004069000000000000
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: FMOPA (fp64) computes correct matrix in ZA6.D ===



=== Test: FMOPS (fp32) subtracts diagonal matrix from zeroed ZA2.S ===
  [INSTRS] start --> FMOPS (fp32) --> done
  [UNROLL] unrolling... DONE (took 196 ms)
  [SUB UF] IEEE substitution... ENABLED (took 6 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 26 ms)
 input vector registers Z[1] and Z[2]
LOG["Zn @ 0"] : #x3f8000004040000040a0000040e00000
LOG["Zm @ 0"] : #x3f8000004040000040a0000040e00000
 ZA initially zeroed out
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 then contains the resulting matrix
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ c0 e0 __ __ c1 a8 __ __ c2 0c __ __ c2 44 __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ c0 a0 __ __ c1 70 __ __ c1 c8 __ __ c2 0c __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ c0 40 __ __ c1 10 __ __ c1 70 __ __ c1 a8 __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ bf 80 __ __ c0 40 __ __ c0 a0 __ __ c0 e0 __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 row slices of ZA2H.S from top to bottom
LOG["ZA2H.S[0] @ 1"] : #xc0e00000c1a80000c20c0000c2440000
LOG["ZA2H.S[1] @ 1"] : #xc0a00000c1700000c1c80000c20c0000
LOG["ZA2H.S[2] @ 1"] : #xc0400000c1100000c1700000c1a80000
LOG["ZA2H.S[3] @ 1"] : #xbf800000c0400000c0a00000c0e00000
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: FMOPS (fp32) subtracts diagonal matrix from zeroed ZA2.S ===



=== Test: FMOPS (fp16->fp32) subtracts and leaves plus (+) pattern untouched ===
  [INSTRS] start --> FMOPS (fp16->fp32) --> done
  [UNROLL] unrolling... DONE (took 405 ms)
  [SUB UF] IEEE substitution... ENABLED (took 9 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 30 ms)
 input vector registers Z[1] and Z[2]
LOG["Zn @ 0"] : #x3c0040003c0040004200450042004500
LOG["Zm @ 0"] : #x3c0040003c0040004200450042004500
 ZA initially contains a pattern
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ 41 20 __ __ 41 20 __ __ 41 20 __ __ 41 20 __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ 41 20 __ __ 41 20 __ __ 41 20 __ __ 41 20 __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ 41 20 __ __ 41 20 __ __ 41 20 __ __ 41 20 __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ 41 20 __ __ 41 20 __ __ 41 20 __ __ 41 20 __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 then contains the resulting matrix
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ c0 40 __ __ c0 40 __ __ 41 20 __ __ c1 c0 __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ c0 40 __ __ c0 40 __ __ 41 20 __ __ c1 c0 __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ 41 20 __ __ 41 20 __ __ 41 20 __ __ 41 20 __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ 40 a0 __ __ 40 a0 __ __ 41 20 __ __ c0 40 __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 row slices of ZA1H.S from top to bottom
LOG["ZA1H.S[0] @ 1"] : #xc0400000c040000041200000c1c00000
LOG["ZA1H.S[1] @ 1"] : #xc0400000c040000041200000c1c00000
LOG["ZA1H.S[2] @ 1"] : #x41200000412000004120000041200000
LOG["ZA1H.S[3] @ 1"] : #x40a0000040a0000041200000c0400000
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: FMOPS (fp16->fp32) subtracts and leaves plus (+) pattern untouched ===



=== Test: BFMOPA produces computed matrix in zeroed ZA1.S ===
  [INSTRS] start --> BFMOPA (bf16->fp32) --> done
  [UNROLL] unrolling... DONE (took 352 ms)
  [SUB UF] IEEE substitution... ENABLED (took 9 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 30 ms)
 input vector registers Z[1] and Z[2]
LOG["Zn @ 0"] : #x40004080400040803f8040403f804040
LOG["Zm @ 0"] : #x40004080400040803f8040403f804040
 ZA initially zeroed out
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 then contains the resulting matrix
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ 41 60 __ __ 41 60 __ __ 41 20 __ __ 41 20 __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ 41 60 __ __ 41 60 __ __ 41 20 __ __ 41 20 __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ 41 a0 __ __ 41 a0 __ __ 41 60 __ __ 41 60 __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ 41 a0 __ __ 41 a0 __ __ 41 60 __ __ 41 60 __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 row slices of ZA1H.S from top to bottom
LOG["ZA1H.S[0] @ 1"] : #x41600000416000004120000041200000
LOG["ZA1H.S[1] @ 1"] : #x41600000416000004120000041200000
LOG["ZA1H.S[2] @ 1"] : #x41a0000041a000004160000041600000
LOG["ZA1H.S[3] @ 1"] : #x41a0000041a000004160000041600000
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: BFMOPA produces computed matrix in zeroed ZA1.S ===



=== Test: ADDSPL chain read X[3] write to SP, then read SP write to X[10] ===
  [INSTRS] start --> ADDSPL --> ADDSPL --> done
  [UNROLL] unrolling... DONE (took 140 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 9 ms)
LOG["Imm6 @ 0"] : #b111101
LOG["SP @ 0 (zeroed)"] : #x0000000000000000
LOG["SP @ 1"] : #xfffffffffffffffe
LOG["Imm6 @ 1"] : #b111010
LOG["X[10] @ 1 (zeroed)"] : #x0000000000000000
LOG["X[10] @ 2"] : #xfffffffffffffff2
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
  step: 2 faults: #x00
=== PASS: ADDSPL chain read X[3] write to SP, then read SP write to X[10] ===



=== Test: ADDSVL chain read SP write to X[3], then read X[3] write to SP ===
  [INSTRS] start --> ADDSVL --> ADDSVL --> done
  [UNROLL] unrolling... DONE (took 138 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 8 ms)
LOG["Imm6 @ 0"] : #b111101
LOG["X[3] @ 0 (zeroed)"] : #x0000000000000000
LOG["X[3] @ 1"] : #xffffffffffffffd4
LOG["Imm6 @ 1"] : #b111010
LOG["SP @ 1 (zeroed)"] : #x0000000000000004
LOG["SP @ 2"] : #xffffffffffffff74
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
  step: 2 faults: #x00
=== PASS: ADDSVL chain read SP write to X[3], then read X[3] write to SP ===



=== Test: RDSVL reads value into X[3] ===
  [INSTRS] start --> RDSVL --> done
  [UNROLL] unrolling... DONE (took 7 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 8 ms)
LOG["Imm6 @ 0"] : #b111101
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: RDSVL reads value into X[3] ===



=== Test: LDR to ZA[3] (equivalent to ZA0H[3]) starting at an offset ===
  [INSTRS] start --> LDR --> done
  [UNROLL] unrolling... DONE (took 40 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 10 ms)
 initialize a monotonic sequence in DRAM
--------------------------------------------------------------------------------
 DRAM - Step 0
--------------------------------------------------------------------------------
 0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15
 #x00 #x01 #x02 #x03 #x04 #x05 #x06 #x07 #x08 #x09 #x0a #x0b #x0c #x0d #x0e #x0f
--------------------------------------------------------------------------------
 16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
 #x10 #x11 #x12 #x13 #x14 #x15 #x16 #x17 #x18 #x19 #x1a #x1b #x1c #x1d #x1e #x1f
--------------------------------------------------------------------------------
 32   33   34   35   36   37   38   39   40   41   42   43   44   45   46   47
 #x20 #x21 #x22 #x23 #x24 #x25 #x26 #x27 #x28 #x29 #x2a #x2b #x2c #x2d #x2e #x2f
--------------------------------------------------------------------------------
 row 3 has been filled
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ 1f 1e 1d 1c 1b 1a 19 18 17 16 15 14 13 12 11 10 │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: LDR to ZA[3] (equivalent to ZA0H[3]) starting at an offset ===



=== Test: LD1.H loads into ZA1V.H[3] from DRAM (LE) starting at an offset ===
  [INSTRS] start --> LD1.H --> done
  [UNROLL] unrolling... DONE (took 1000 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 13 ms)
 check endianness of DRAM
--------------------------------------------------------------------------------
 DRAM - Step 0
--------------------------------------------------------------------------------
 0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15
 #xff #xff #xff #xff #xfe #xff #xfd #xff #xfc #xff #xfb #xff #xfa #xff #xf9 #xff
--------------------------------------------------------------------------------
 16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
 #xf8 #xff #xf7 #xff #xf6 #xff #xf5 #xff #xf4 #xff #xf3 #xff #xf2 #xff #xf1 #xff
--------------------------------------------------------------------------------
 32   33   34   35   36   37   38   39   40   41   42   43   44   45   46   47
 #xf0 #xff #xff #xff #xff #xff #xff #xff #xff #xff #xff #xff #xff #xff #xff #xff
--------------------------------------------------------------------------------
 vertical slice filled
LOG["DRAM VECTOR"] : #xfff6fff7fff8fff9fffafffbfffcfffd
LOG["SLICE"] : #xfff6fff7fff8fff9fffafffbfffcfffd
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ ff fd __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ ff fc __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ ff fb __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ ff fa __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ ff f9 __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ ff f8 __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ ff f7 __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ ff f6 __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: LD1.H loads into ZA1V.H[3] from DRAM (LE) starting at an offset ===



=== Test: LD1.D loads to ZA5V.D[1] from DRAM (LE) address 14 with all predicates ===
  [INSTRS] start --> LD1.D --> done
  [UNROLL] unrolling... DONE (took 978 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 9 ms)
 vertical slice filled
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 1          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ 07 06 05 04 03 02 01 __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ 0f 0e 0d 0c 0b 0a 09 08 __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
LOG["top @ 1"] : #x07060504030201000000000000000000
LOG["bottom @ 1"] : #x0f0e0d0c0b0a09080000000000000000
 INVARIANT: checking DRAM_GetElement and DRAM_GetVector helpers (must equal)
LOG["DRAM"] : #x0f0e0d0c0b0a09080706050403020100
LOG["slice"] : #x0f0e0d0c0b0a09080706050403020100
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: LD1.D loads to ZA5V.D[1] from DRAM (LE) address 14 with all predicates ===



=== Test: STR from ZA[5] (equivalent to ZA0H[5]) to DRAM (BE) with offset ===
  [INSTRS] start --> STR --> done
  [UNROLL] unrolling... DONE (took 80 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 13 ms)
 row 5 (ZA0H.B[5]) initialized
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ 11 22 33 44 55 66 77 88 99 aa bb cc dd ee ff │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
 horizontal row 2 starting at address 16 now filled
--------------------------------------------------------------------------------
 DRAM - Step 1
--------------------------------------------------------------------------------
 16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00
--------------------------------------------------------------------------------
 32   33   34   35   36   37   38   39   40   41   42   43   44   45   46   47
 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00
--------------------------------------------------------------------------------
 48   49   50   51   52   53   54   55   56   57   58   59   60   61   62   63
 #xff #xee #xdd #xcc #xbb #xaa #x99 #x88 #x77 #x66 #x55 #x44 #x33 #x22 #x11 #x00
--------------------------------------------------------------------------------
 64   65   66   67   68   69   70   71   72   73   74   75   76   77   78   79
 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00
--------------------------------------------------------------------------------
 these two are BYTE-swapped
LOG["DRAM slice as ZA endian"] : #x00112233445566778899aabbccddeeff
LOG["WB vector"] : #xffeeddccbbaa99887766554433221100
 WB vector was written to address 0x30 (48 in decimal)
LOG["WB vector"] : #xffeeddccbbaa99887766554433221100
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: STR from ZA[5] (equivalent to ZA0H[5]) to DRAM (BE) with offset ===



=== Test: ST1.H stores ZA1V.H[3] to DRAM (BE) with offset 1 ===
  [INSTRS] start --> ST1.H --> done
  [UNROLL] unrolling... DONE (took 935 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 14 ms)
 vertical slice initialized
┌------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 0          │
├------------------------------------------------─┤
│ 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ ee ff __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ cc dd __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ aa bb __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ 88 99 __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ __ __ __ __ __ __ __ __ 66 77 __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ 44 55 __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ 22 33 __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ 11 __ __ __ __ __ __ │ R15
└------------------------------------------------─┘
LOG["Vertical Slice"] : #x00112233445566778899aabbccddeeff
LOG["WB addr"] : #x00000000000000000000000000000002
--------------------------------------------------------------------------------
 DRAM - Step 1
--------------------------------------------------------------------------------
 0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15
 #x00 #x00 #xee #xff #xcc #xdd #xaa #xbb #x88 #x99 #x66 #x77 #x44 #x55 #x22 #x33
--------------------------------------------------------------------------------
 16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
 #x00 #x11 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00
--------------------------------------------------------------------------------
 32   33   34   35   36   37   38   39   40   41   42   43   44   45   46   47
 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00 #x00
--------------------------------------------------------------------------------
 these two are HALF-swapped
LOG["DRAM vector"] : #x00112233445566778899aabbccddeeff
LOG["WB vector"] : #xeeffccddaabb88996677445522330011
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: ST1.H stores ZA1V.H[3] to DRAM (BE) with offset 1 ===



=== Test: PSEL.D fills predicate, then PSEL.S zeroes out ===
  [INSTRS] start --> PSEL.D --> PSEL.S --> done
  [UNROLL] unrolling... DONE (took 175 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 9 ms)
 Z3 INVARIANT: P[1] and P[2] should remain the same throughout
LOG["Pd @ 0 (ugly)"] : #x6767
LOG["Pd @ 1 (pattern)"] : #xffff
LOG["Pd @ 2 (zeroed)"] : #x0000
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
  step: 2 faults: #x00
=== PASS: PSEL.D fills predicate, then PSEL.S zeroes out ===



=== Test: REVD.Q swaps 64-bit halves QUAD element vector then MOVA_V2T.Q updates ZA9V.Q[1] ===
  [INSTRS] start --> REVD.Q --> MOVA_V2T.Q --> done
  [UNROLL] unrolling... DONE (took 909 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 31 ms)
 dest is updated by source
LOG["source @ 0"] : #x00112233445566778899aabbccddeeff000102030405060708090a0b0c0d0e0f
LOG["dest @ 0"] : #xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
LOG["dest @ 1"] : #x8899aabbccddeeff001122334455667708090a0b0c0d0e0f0001020304050607
 ZA is updated at step 2
┌------------------------------------------------------------------------------------------------─┐
│ ZA TILE MEMORY LAYOUT (16x16) - Step 2                                                          │
├------------------------------------------------------------------------------------------------─┤
│ 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 │
├------------------------------------------------------------------------------------------------─┤
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R0
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R1
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R2
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R3
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R4
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R5
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R6
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R7
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R8
│ 08 09 0a 0b 0c 0d 0e 0f __ 01 02 03 04 05 06 07 __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R9
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R10
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R11
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R12
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R13
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R14
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R15
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R16
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R17
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R18
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R19
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R20
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R21
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R22
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R23
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R24
│ 88 99 aa bb cc dd ee ff __ 11 22 33 44 55 66 77 __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R25
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R26
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R27
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R28
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R29
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R30
│ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ __ │ R31
└------------------------------------------------------------------------------------------------─┘
LOG["ZA9V.Q[1] @ 2"] : #x8899aabbccddeeff001122334455667708090a0b0c0d0e0f0001020304050607
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
  step: 2 faults: #x00
=== PASS: REVD.Q swaps 64-bit halves QUAD element vector then MOVA_V2T.Q updates ZA9V.Q[1] ===



=== Test: UCLAMP.S makes each element atleast 0x1000 ===
  [INSTRS] start --> UCLAMP.S --> done
  [UNROLL] unrolling... DONE (took 357 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 8 ms)
LOG["before clamping"] : #xf8f9fafbfcfdfeff0102030405060708
LOG["after clamping"] : #xf8f9fafbfcfdfeff1000100010001000
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: UCLAMP.S makes each element atleast 0x1000 ===



=== Test: SCLAMP.B only allows nonnegative elements to remain ===
  [INSTRS] start --> SCLAMP.B --> done
  [UNROLL] unrolling... DONE (took 1092 ms)
  [SUB UF] IEEE substitution disabled (uninterpreted)
  [SOLVER] solving... DONE (took 9 ms)
LOG["before clamping"] : #xf8f9fafbfcfdfeff0102030405060708
LOG["after clamping"] : #x00000000000000000102030405060700
--- CHECKING FOR FAULTS ---
  step: 0 faults: #x00
  step: 1 faults: #x00
=== PASS: SCLAMP.B only allows nonnegative elements to remain ===


===========================================
            TEST SUMMARY
===========================================
  [PASS] SMSTART sets pstate and resets SME/SVE states
  [PASS] SMSTOP clears pstate and resets ONLY SVE state
  [PASS] SHOWCASE: track_slice() + cstr_all_tracked_and_zero() idiom
  [PASS] SHOWCASE: Multi-byte Vertical Track Slice
  [PASS] GetHorizontalSlice constrains underlying ZA memory bytes
  [PASS] GetVerticalSlice constrains underlying ZA memory bytes
  [PASS] Horizontal slices ZA0H.B[1], ZA1H.H[0] have equal bytes
  [PASS] ZERO Imm8=0xFF zeroes entire ZA array that was initialized to non-zero
  [PASS] ZERO Imm8=0x55 zeroes 16-bit element tile ZA0.H which was previously non-zero
  [PASS] ZERO Imm8=0x84 zeroes 32-bit element tile ZA3.S which was previously non-zero
  [PASS] MOVA_T2V.S (tile to vector) move ZA3V.S[1] to Z[10] using P[5] (only first and third element)
  [PASS] MOVA_V2T.D (vector to tile) move Z[10] to ZA7V.D[1] using P[2]
  [PASS] ADDHA.S accumulates horizontally except first row of ZA2.S
  [PASS] ADDVA.D accumulates vertically on ZA7.D
  [PASS] UMOPA (8b->32b) correctly computes new diagonal matrix sum using predicates
  [PASS] SMOPS (16b->64b) subtracts from original zero matrix (input is unsigned) with alternating predicates
  [PASS] SMOPS (16b->64b) adds and subtracts original zero matrix (input signed and unsigned) with alternating predicates
  [PASS] FMOPA (fp64) computes correct matrix in ZA6.D
  [PASS] FMOPS (fp32) subtracts diagonal matrix from zeroed ZA2.S
  [PASS] FMOPS (fp16->fp32) subtracts and leaves plus (+) pattern untouched
  [PASS] BFMOPA produces computed matrix in zeroed ZA1.S
  [PASS] ADDSPL chain read X[3] write to SP, then read SP write to X[10]
  [PASS] ADDSVL chain read SP write to X[3], then read X[3] write to SP
  [PASS] RDSVL reads value into X[3]
  [PASS] LDR to ZA[3] (equivalent to ZA0H[3]) starting at an offset
  [PASS] LD1.H loads into ZA1V.H[3] from DRAM (LE) starting at an offset
  [PASS] LD1.D loads to ZA5V.D[1] from DRAM (LE) address 14 with all predicates
  [PASS] STR from ZA[5] (equivalent to ZA0H[5]) to DRAM (BE) with offset
  [PASS] ST1.H stores ZA1V.H[3] to DRAM (BE) with offset 1
  [PASS] PSEL.D fills predicate, then PSEL.S zeroes out
  [PASS] REVD.Q swaps 64-bit halves QUAD element vector then MOVA_V2T.Q updates ZA9V.Q[1]
  [PASS] UCLAMP.S makes each element atleast 0x1000
  [PASS] SCLAMP.B only allows nonnegative elements to remain

  Total: 33 tests
  Passed: 33
  Failed: 0

  All tests passed! 🎉
```
