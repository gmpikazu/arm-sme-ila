# Project Plan
## Crucial Clarification (ask in meeting)
- Untyped data type instruction how to encode and operate
- How to model DRAM memory, because we need to agree on the memory model to prove between Gemmini and ARM SME
    - To implement `LD`, `ST` for each data type
- How to deal with different float types, widening, and arithmetic (signed & unsigned)
    - To implement sum, outer product, and other operations
- Must `ZA0` single choice tile access be independent instruction? I handled it with the `if(dim==1)` check already though before doing `log2(dim)`
- What is the length of the `Imm` field, it differs based on instruction it seems
- How many predicate registers do we need to have?
- `esize` is baked into opcode, and we use lambdas for .B .H .W .D .Q suffixes, right?
## Next Steps
- Helper that takes two vectors and `esize`, returns a new vector that combines the two vectors using a Lambda `combine()` function: `add`, `sub`, `accumulate`
    - For outer products, sums, accumulate, etc
- Agreement on how to model general purpose DRAM memory for `LD`, `ST` instructions
- Mov (alias), Mova, Ld, St, Ldr, Str, Zero instructions
## Delayed Simple Tasks
- `SMSSTART/STOP` on/off zeroing behavior (B1.1.1 and E2 pseudocode of SM,ZA states)
- `Ws+imm` slice index
- Use asserts instead of `if` and `switch` for program guarantee
- `Z_REG_WIDTH` and `SVL` scattered around code but they are same thing
## Delayed Complex Tasks
- Floating point
- Outer products and sums
## Random Questions
- Does ILAng use 2's complement natively for operator overloads?
- How do I know if a comparison operator overload is signed or unsigned?

# Implementation Overview
This document is aimed to provide viewers with a overview of the lower-level implementation specifics of this project
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