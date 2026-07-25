# Project Plan
## Remaining Tasks
- Model load store DRAM (using UFs), SVE2 instructions using <T> field
- Verify instructions by constructing unit tests, then integration tests (eg., {ZERO, MOVA, SMOPA})
## Delayed Simple Tasks
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

## Single Predicate Masking
- ARM SME supports `/M` (merge mode), destination element is unmodified if source element is not activated by predicate bit, and `/Z` (zero mode), destination element is zeroed out instead when source element is not activated by predicate bit
- Predicate registers contain `SVL_B` bits and `bit[i]` controls activation of `vector.elem[i]` where an element can occupy `esize` bits (eg., `BYTE`, `HALF`, etc)
- The implementation extracts bits starting from LSB, higher order bits are ignored when we have iterated over `num_elements` bits

## Preventing Z3 Garbage Initialization
- Explicitly constrain all values to prevent Z3 populating them with garbage
- For ZA, this was done through `cstr_step_slice()` where all untouched addresses are explicitly set to `0x00` to clean up `PrintZa()`'s output for easier empirical verification