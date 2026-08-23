# WISA Specification, version 0.1

WISA (Warp Instruction Set Architecture) is the instruction set executed by WarpSim. It is inspired by PTX in syntax and in its execution model, and it is deliberately small: every instruction listed here is implemented by the core, assembled by the assembler, printed by the disassembler, and covered by the round-trip property test. This document is versioned with the code. A change to any instruction updates this document in the same pull request.

Sections 1 through 5 define the machine. Section 6 is the instruction table that the test suite parses. Sections 7 through 9 define the assembler syntax, the reconvergence rule, and the design notes behind the layout decisions.

## 1. Execution model

- A **lane** is one thread of execution with its own registers.
- A **warp** is 32 lanes that share one program counter (PC) and issue one instruction per step in lockstep. Lane `i` of a warp has `%laneid = i`.
- An **active mask** is a 32-bit set of lanes that execute the current instruction. A lane that is not in the active mask is unaffected by the instruction.
- A **block** is a set of warps that share one shared memory and can synchronize with `bar.sync`. A block is launched with dimensions `ntid.x` by `ntid.y`; lane indices within the block are linearized as `tid.x + tid.y * ntid.x`, and warp `w` of the block holds linear indices `32w` through `32w + 31`. A block whose linear size is not a multiple of 32 has a final warp whose high lanes are never active.
- A **grid** is a set of blocks with dimensions `nctaid.x` by `nctaid.y`. Blocks do not communicate except through global memory, and the simulator executes them in order `ctaid.x + ctaid.y * nctaid.x`, one block at a time.
- A **kernel** is one program with one entry point. Every lane begins at PC 0 with every general and predicate register set to zero.
- Control flow within a warp is handled by an active-mask divergence stack described in section 8. The observable semantics are: every lane executes exactly the instructions that its own control flow would select, and when two groups of lanes of one warp take different paths, the taken path executes first and the fall-through path second, before both rejoin at the reconvergence point.

## 2. State

### 2.1 General registers

Each lane owns 64 general registers `r0` through `r63`, each 32 bits wide. A register has no type; an instruction interprets its bits as a signed 32-bit two's complement integer (`s32`) or as an IEEE 754 binary32 value (`f32`) according to its mnemonic.

### 2.2 Predicate registers

Each lane owns 8 predicate registers `p0` through `p7`, each one bit. Predicates are written by `setp` and read by guards.

### 2.3 Guards

Any instruction may carry a guard `@p` or `@!p` where `p` is a predicate register. A guarded instruction executes on a lane only if the lane is in the active mask and its predicate has the required value (true for `@p`, false for `@!p`). For all instructions except `bra` and `exit`, a guard that is false on a lane simply skips the lane. For `bra` and `exit`, the guard selects which lanes branch or retire; see section 8.

### 2.4 Special registers

Special registers are read with `mov.sreg` and are never written.

| Name | Index | Value |
|------|-------|-------|
| `%tid.x` | 0 | Lane index within the block, x dimension |
| `%tid.y` | 1 | Lane index within the block, y dimension |
| `%ntid.x` | 2 | Block size, x dimension |
| `%ntid.y` | 3 | Block size, y dimension |
| `%ctaid.x` | 4 | Block index within the grid, x dimension |
| `%ctaid.y` | 5 | Block index within the grid, y dimension |
| `%nctaid.x` | 6 | Grid size, x dimension |
| `%nctaid.y` | 7 | Grid size, y dimension |
| `%laneid` | 8 | Lane index within the warp, 0 through 31 |
| `%warpid` | 9 | Warp index within the block |

### 2.5 Memory spaces

- **Global memory** is one flat byte-addressed space shared by every block of a launch. The host allocates buffers within it and passes their byte offsets as parameters. Reads and writes are 32-bit words at 4-byte aligned addresses. An access that is misaligned or outside the allocated size is a simulator fault that stops the launch and reports the lane, the PC, and the address.
- **Shared memory** is one byte-addressed space per block, sized by the `.shared` directive, zero-initialized at block start, and discarded at block end. The same alignment and bounds rules apply.
- **Parameter memory** is a read-only table of 32-bit words per launch, indexed by parameter ordinal. A buffer parameter holds the byte offset of the buffer in global memory. A scalar parameter holds its value.

## 3. Encoding

Every instruction is one 64-bit little-endian word with the following fields. Bit 63 is the most significant bit.

| Bits | Width | Field | Meaning |
|------|-------|-------|---------|
| 63:56 | 8 | `opcode` | Opcode number from section 6; `0x00` is invalid |
| 55 | 1 | `guard` | 1 if the instruction carries a guard |
| 54 | 1 | `negate` | 1 if the guard is `@!p`; must be 0 when `guard` is 0 |
| 53:51 | 3 | `pred` | Guard predicate register index; must be 0 when `guard` is 0 |
| 50:45 | 6 | `dst` | Destination register; a predicate index for `setp` |
| 44:39 | 6 | `src0` | First source register, or special register index for `mov.sreg` |
| 38:33 | 6 | `src1` | Second source register |
| 32 | 1 | `imm_flag` | 1 if the immediate field replaces the immediate-capable operand |
| 31:0 | 32 | `imm` | Immediate value, two's complement integer or IEEE binary32 bits |

The fields sum to exactly 64 bits. Every field that the opcode does not use must be zero, so each instruction has exactly one valid encoding and the decoder rejects any other word. Which fields an opcode uses is given by its **operand shape** in section 6:

| Shape | Syntax | Fields used | Immediate-capable operand |
|-------|--------|-------------|---------------------------|
| `RRR` | `op rd, ra, rb` or `op rd, ra, imm` | `dst`, `src0`, `src1` or `imm` | `rb` |
| `RR` | `op rd, ra` or `op rd, imm` | `dst`, `src0` or `imm` | `ra` |
| `ACC` | `op rd, ra, rb` | `dst`, `src0`, `src1` | none |
| `PRR` | `op p, ra, rb` or `op p, ra, imm` | `dst` (predicate), `src0`, `src1` or `imm` | `rb` |
| `SREG` | `op rd, %name` | `dst`, `src0` (special index) | none |
| `BRA` | `op label` | `imm` | target and reconvergence, always present |
| `NONE` | `op` | none | none |
| `LD` | `op rd, [ra+off]` | `dst`, `src0`, `imm` with `imm_flag` set | offset, always present |
| `ST` | `op [ra+off], rb` | `src0`, `src1`, `imm` with `imm_flag` set | offset, always present |
| `LDP` | `op rd, [name]` | `dst`, `imm` with `imm_flag` set | parameter ordinal, always present |

When `imm_flag` is 1 for `RRR` or `PRR`, `src1` must be 0. When `imm_flag` is 1 for `RR`, `src0` must be 0. For `LD`, `ST`, and `LDP`, `imm_flag` is always 1 even when the offset is zero, because the offset is part of the instruction rather than an optional operand. For `BRA`, `imm_flag` is 0 and `imm` is interpreted as two 16-bit halves: `imm[15:0]` is the target PC and `imm[31:16]` is the reconvergence PC, with `0xFFFF` meaning no reconvergence point (section 8). A program therefore has at most 65535 instructions, and PC 0xFFFF is not addressable.

### 3.1 Worked example

`@p1 add r3, r4, 7` encodes as:

| Field | Value | Contribution |
|-------|-------|--------------|
| `opcode` | `add` = `0x01` | `0x01 << 56` = `0x0100000000000000` |
| `guard` | 1 | `1 << 55` = `0x0080000000000000` |
| `negate` | 0 | 0 |
| `pred` | 1 | `1 << 51` = `0x0008000000000000` |
| `dst` | 3 | `3 << 45` = `0x0000600000000000` |
| `src0` | 4 | `4 << 39` = `0x0000020000000000` |
| `src1` | 0 | 0 |
| `imm_flag` | 1 | `1 << 32` = `0x0000000100000000` |
| `imm` | 7 | `0x0000000000000007` |
| **Word** | | `0x0188620100000007` |

## 4. Data types and arithmetic

- `s32` operations treat registers as signed 32-bit two's complement integers. Addition, subtraction, and multiplication wrap modulo 2^32. `mul` produces the low 32 bits of the product.
- `div` truncates toward zero. Division by zero produces 0. `INT_MIN / -1` produces `INT_MIN`. Neither case is a fault, so that no kernel can take the simulator down through data.
- `rem` produces the remainder with the sign of the dividend. Remainder by zero produces 0. `INT_MIN rem -1` produces 0.
- `shl`, `shr`, and `sra` use only the low 5 bits of the shift count. `shr` is logical, `sra` is arithmetic.
- `f32` operations follow IEEE 754 binary32 with round to nearest even, no exceptions, and no flush to zero. `ffma` is a fused multiply-add with a single rounding, computing `rd = ra * rb + rd` (the destination is the accumulator; see section 9).
- `fmin` and `fmax` return the non-NaN operand when exactly one operand is NaN and NaN when both are.
- `cvt.f32.s32` converts with round to nearest even. `cvt.s32.f32` truncates toward zero and saturates to the `s32` range; NaN converts to 0.
- `setp.*.f32` comparisons are ordered: any comparison involving NaN is false, except `setp.ne.f32`, which is true when either operand is NaN.

## 5. Memory and synchronization

- `ld.global rd, [ra+off]` reads the 32-bit word at global byte address `ra + off`, where `off` is a signed 32-bit immediate. `st.global [ra+off], rb` writes `rb` there.
- `ld.shared` and `st.shared` are the same on the block's shared memory.
- `ld.param rd, [name]` reads parameter `name`, which the assembler resolves to its ordinal.
- Within one warp, a memory instruction performs its lane accesses in lane order 0 through 31; a lane that stores to an address another lane of the same instruction loads produces a write-after-read in lane order, and two lanes storing to the same address leave the value of the higher lane. Across warps of a block, ordering is guaranteed only by `bar.sync`.
- `bar.sync` parks the warp until every warp of the block that has not retired all of its lanes has arrived at a `bar.sync`. It must be executed with an active mask equal to the warp's set of non-retired lanes (that is, outside divergent code); executing it from within divergent code is a fault. If a warp retires all lanes while other warps of the block are parked at a barrier, the barrier completes without it. If every warp is parked at a barrier or retired, the barrier completes.
- `exit` retires the lanes on which it executes. A retired lane never executes again and is removed from every pending mask on the divergence stack. A warp whose lanes have all retired is finished. Falling off the end of the program is equivalent to `exit`.

## 6. Instruction table

This table is parsed by `tests/isa/spec_table_test.cpp`: the set of backticked mnemonics in the first column must equal the assembler's opcode table. Types: `s32` signed integer, `f32` binary32, `b32` raw bits, `p` predicate.

| Mnemonic | Opcode | Shape | Semantics |
|----------|--------|-------|-----------|
| `add` | 0x01 | RRR | `rd = ra + rb` (s32, wraps) |
| `sub` | 0x02 | RRR | `rd = ra - rb` (s32, wraps) |
| `mul` | 0x03 | RRR | `rd = low32(ra * rb)` (s32) |
| `div` | 0x04 | RRR | `rd = ra / rb` (s32, truncating, zero divisor gives 0) |
| `rem` | 0x05 | RRR | `rd = ra rem rb` (s32, sign of dividend, zero divisor gives 0) |
| `min` | 0x06 | RRR | `rd = min(ra, rb)` (s32) |
| `max` | 0x07 | RRR | `rd = max(ra, rb)` (s32) |
| `neg` | 0x08 | RR | `rd = -ra` (s32, wraps) |
| `and` | 0x09 | RRR | `rd = ra & rb` (b32) |
| `or` | 0x0A | RRR | `rd = ra \| rb` (b32) |
| `xor` | 0x0B | RRR | `rd = ra ^ rb` (b32) |
| `not` | 0x0C | RR | `rd = ~ra` (b32) |
| `shl` | 0x0D | RRR | `rd = ra << (rb & 31)` (b32) |
| `shr` | 0x0E | RRR | `rd = ra >> (rb & 31)` logical (b32) |
| `sra` | 0x0F | RRR | `rd = ra >> (rb & 31)` arithmetic (s32) |
| `mov` | 0x10 | RR | `rd = ra` (b32) |
| `fadd` | 0x11 | RRR | `rd = ra + rb` (f32) |
| `fsub` | 0x12 | RRR | `rd = ra - rb` (f32) |
| `fmul` | 0x13 | RRR | `rd = ra * rb` (f32) |
| `ffma` | 0x14 | ACC | `rd = fma(ra, rb, rd)` (f32, single rounding) |
| `fmin` | 0x15 | RRR | `rd = min(ra, rb)` (f32, NaN-aware) |
| `fmax` | 0x16 | RRR | `rd = max(ra, rb)` (f32, NaN-aware) |
| `fneg` | 0x17 | RR | `rd = -ra` (f32, flips the sign bit) |
| `cvt.f32.s32` | 0x18 | RR | `rd = float(ra)` (round to nearest even) |
| `cvt.s32.f32` | 0x19 | RR | `rd = int(ra)` (truncate, saturate, NaN gives 0) |
| `setp.eq.s32` | 0x1A | PRR | `p = (ra == rb)` (s32) |
| `setp.ne.s32` | 0x1B | PRR | `p = (ra != rb)` (s32) |
| `setp.lt.s32` | 0x1C | PRR | `p = (ra < rb)` (s32) |
| `setp.le.s32` | 0x1D | PRR | `p = (ra <= rb)` (s32) |
| `setp.gt.s32` | 0x1E | PRR | `p = (ra > rb)` (s32) |
| `setp.ge.s32` | 0x1F | PRR | `p = (ra >= rb)` (s32) |
| `setp.eq.f32` | 0x20 | PRR | `p = (ra == rb)` (f32, ordered) |
| `setp.ne.f32` | 0x21 | PRR | `p = (ra != rb)` (f32, unordered) |
| `setp.lt.f32` | 0x22 | PRR | `p = (ra < rb)` (f32, ordered) |
| `setp.le.f32` | 0x23 | PRR | `p = (ra <= rb)` (f32, ordered) |
| `setp.gt.f32` | 0x24 | PRR | `p = (ra > rb)` (f32, ordered) |
| `setp.ge.f32` | 0x25 | PRR | `p = (ra >= rb)` (f32, ordered) |
| `mov.sreg` | 0x26 | SREG | `rd = special[index]` |
| `bra` | 0x27 | BRA | Branch to target; guarded form may diverge (section 8) |
| `exit` | 0x28 | NONE | Retire the executing lanes |
| `bar.sync` | 0x29 | NONE | Block barrier |
| `ld.global` | 0x2A | LD | `rd = global[ra + off]` |
| `st.global` | 0x2B | ST | `global[ra + off] = rb` |
| `ld.shared` | 0x2C | LD | `rd = shared[ra + off]` |
| `st.shared` | 0x2D | ST | `shared[ra + off] = rb` |
| `ld.param` | 0x2E | LDP | `rd = param[ordinal]` |

Forty-six instructions. Opcodes 0x2F through 0xFF are invalid and decode to an error.

## 7. Assembly syntax

```
// Comments run from // to end of line.
.entry vecadd            // Kernel name. Exactly one per program, first directive.
.param a                 // Parameters are declared in order; ordinal 0, 1, 2, ...
.param b
.param c
.param n
.shared 0                // Shared memory bytes for the block. Optional, default 0.

    mov.sreg r0, %tid.x
    mov.sreg r1, %ctaid.x
    mov.sreg r2, %ntid.x
    mul     r1, r1, r2
    add     r0, r0, r1   // r0 = global index
    ld.param r3, [n]
    setp.ge.s32 p0, r0, r3
@p0 bra done             // Guarded branch: assembler records the reconvergence PC.
    shl     r4, r0, 2    // Byte offset of element r0.
    ld.param r5, [a]
    add     r5, r5, r4
    ld.global r6, [r5+0]
    ld.param r7, [b]
    add     r7, r7, r4
    ld.global r8, [r7+0]
    fadd    r6, r6, r8
    ld.param r9, [c]
    add     r9, r9, r4
    st.global [r9+0], r6
done:
    exit
```

Rules:

- Tokens are case sensitive. Mnemonics and directives are lower case.
- A label is an identifier followed by `:` on its own line or preceding an instruction. Labels are unique within a program.
- Registers are `r0` through `r63`; predicates are `p0` through `p7`; special registers are the names in section 2.4.
- Integer immediates are decimal (optionally negative) or hexadecimal with `0x`. They must fit in 32 bits as a signed or unsigned value. Float immediates contain a `.` or an exponent, for example `1.0`, `-2.5e3`, and are encoded as binary32 bits. An instruction whose immediate-capable operand is an `f32` operand accepts either form; the assembler does not convert between them.
- Address operands are `[ra+off]` or `[ra-off]` or `[ra]`, with `off` an integer immediate. Whitespace inside the brackets is permitted.
- Parameter operands of `ld.param` are `[name]` where `name` is a declared parameter.
- The `bra` target is a label. Branch targets are resolved in a second pass, so forward references are permitted.

## 8. Control flow and reconvergence

### 8.1 Definitions

The assembler partitions the program into **basic blocks**. A block begins at PC 0, at every label that is a branch target, and at every instruction that follows a `bra` or an `exit`. A block ends at a `bra`, at an `exit`, or at the instruction preceding the next block start. The control-flow graph has a **virtual exit node**. Edges are:

- An unguarded `bra` has one edge, to its target.
- A guarded `bra` has two edges: to its target and to the next instruction.
- An `exit`, guarded or not, has an edge to the virtual exit node; a guarded `exit` also has an edge to the next instruction.
- Every other block end has one edge to the next instruction, and the final instruction of the program, if it is not a `bra` or `exit`, has an edge to the virtual exit node.

Block `B` **post-dominates** block `A` if every path from `A` to the virtual exit node passes through `B`. The **immediate post-dominator** of `A` is the post-dominator of `A` that is post-dominated by every other post-dominator of `A`. The assembler computes immediate post-dominators by iterative dataflow on the reversed graph.

### 8.2 The reconvergence rule

For every guarded `bra`, the assembler writes into `imm[31:16]` the PC of the first instruction of the immediate post-dominator of the branch's block. If the immediate post-dominator is the virtual exit node, it writes `0xFFFF`. An unguarded `bra` always carries `0xFFFF` because it never diverges.

### 8.3 Execution

The warp keeps a **divergence stack** of entries `(reconvergence PC, deferred PC, deferred mask)`. Executing a guarded `bra` on the active mask `M` partitions it into `T` (guard true, branch) and `F` (guard false, fall through):

1. If `F` is empty, the warp sets PC to the target with mask `T`. If `T` is empty, the warp proceeds to the next instruction with mask `F`. No entry is pushed.
2. Otherwise the branch **diverges**: the warp pushes `(reconvergence PC, PC + 1, F)` and continues at the target with mask `T`.

Before issuing any instruction, the warp checks the top entry of the stack:

3. If the warp's PC equals the top entry's reconvergence PC, the entry is popped and the deferred path resumes: the warp records its current mask as the **arrived mask**, sets PC to the deferred PC, and sets its active mask to the deferred mask. When that path in turn reaches the same reconvergence PC, the arrived mask is unioned back in. Concretely the implementation pushes a second entry when switching paths, `(reconvergence PC, reconvergence PC, arrived mask)`, so that rule 3 performs the union uniformly.
4. If the active mask becomes empty because every active lane retired through `exit`, the top entry is popped and its path resumes, regardless of PC. If the stack is empty, the warp is finished.

Invariants that the core asserts in debug builds:

- Entries are ordered by nesting: the reconvergence PC of a newer entry is reached before or at the same instruction as the reconvergence PC of an older entry along every path.
- A lane is present in at most one deferred mask on the stack at a time, and never in both the active mask and a deferred mask.
- A retired lane is present in no mask.

### 8.4 Consequences that kernels can rely on

- **If and if-else** reconverge at the instruction after the joined paths.
- **Nested branches** to any depth reconverge innermost first.
- **Loops.** For a loop whose back edge is a guarded `bra` to the loop head, the immediate post-dominator of the branch is the loop exit, so lanes that leave the loop early are deferred until every lane of the warp has left the loop. This is the standard lockstep loop behavior.
- **Early exit from a loop** through a guarded `bra` to a label after the loop reconverges at that label's block, after every other lane has finished the loop.
- **`exit` inside divergent code** retires its lanes immediately; the remaining lanes continue and the deferred path resumes by rule 4 if the active mask becomes empty.
- **Paths that never rejoin.** If both paths of a branch end in `exit` without a common successor, the reconvergence PC is `0xFFFF`, and the deferred path resumes by rule 4 once the taken path has retired.

### 8.5 Worked example

```
        setp.lt.s32 p0, r0, r1
    @p0 bra then            // PC 1: diverges if lanes disagree
        mov r2, 10          // PC 2: fall-through path
        bra join            // PC 3: unguarded, reconvergence 0xFFFF
then:   mov r2, 20          // PC 4: taken path
join:   add r2, r2, 1       // PC 5: immediate post-dominator of the block ending at PC 1
        exit
```

The branch at PC 1 carries target 4 and reconvergence PC 5. If lanes 0 through 15 have `p0` true and lanes 16 through 31 have it false, the warp pushes `(5, 2, 0xFFFF0000)` and runs PC 4 with mask `0x0000FFFF`. Arriving at PC 5 it pops, pushes `(5, 5, 0x0000FFFF)`, and runs PC 2 and PC 3 with mask `0xFFFF0000`. The unguarded `bra` at PC 3 sets PC to 5, where the warp pops again, unions the masks to `0xFFFFFFFF`, and continues.

## 9. Design notes

- **Why a 32-bit immediate and no auxiliary field.** A candidate layout with a 16-bit auxiliary field for the reconvergence PC and the comparison kind was evaluated and rejected: the fields of the chosen layout already sum to 64 bits, and shrinking the immediate below 32 bits would make float immediates and large addresses impossible. The costs of the chosen layout are the 65535-instruction limit and twelve `setp` opcodes, both acceptable for a simulator.
- **Why `ffma` accumulates into `rd`.** Three source registers do not fit the layout. Making the destination the accumulator matches the dominant use (a running dot product) and costs nothing in the shipped kernels.
- **Why division by zero produces zero.** A kernel must never be able to stop the simulator through data. Faults are reserved for memory bounds, alignment, and barrier misuse, which are programming errors rather than data conditions.
- **Why the assembler computes reconvergence points.** The core should not perform control-flow analysis at run time. Recording the post-dominator in the instruction keeps the core a straightforward interpreter and makes the reconvergence decision reviewable in the disassembly.
- **Why unused fields must be zero.** One valid encoding per instruction makes the round-trip test bit exact and makes every stray bit a detectable error rather than a silent one.
