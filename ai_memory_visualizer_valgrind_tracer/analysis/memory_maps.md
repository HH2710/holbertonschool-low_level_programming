# Memory Maps — AI Memory Visualizer

Programs analysed for this task: `stack_example.c` (mandatory), `aliasing_example.c` (mandatory) and `heap_example.c` (optional, short section at the end).

## 0. Conventions and environment

| Item | Value |
|---|---|
| Architecture | x86-64 Linux, System V ABI |
| Compiler | gcc 13.3.0, flags from the Makefile: `-Wall -Wextra -Werror -pedantic -std=gnu89 -g` (no `-O`, so `-O0`) |
| libc | glibc 2.39 |
| Validation tools | normal execution, `gdb` (frame and variable addresses), `valgrind` (Memcheck) |

- **Stack**: one *frame* per active function call. It holds the return address, the saved `rbp`, the parameters (at `-O0` they are copied from registers to the frame) and the local variables. The stack grows **towards lower addresses**: a deeper call has a lower address. A frame is created at function entry and destroyed at `return`. After that, its bytes may be reused by the next call.
- **Heap**: blocks returned by `malloc`. A block lives from `malloc` until `free`, independently of the function that called `malloc`.
- **Lifetime**: the period during which the object exists and may legally be accessed. It is not the same as *scope* (where the name is visible).
- **Owner**: the pointer that is responsible for calling `free` on a heap block. An **alias** is another pointer that holds the same address but is not responsible for freeing it.
- Addresses differ between runs (ASLR). The real values below come from one run. The **relations** between them (offsets, equalities) are stable.

---

## 1. `stack_example.c`

### 1.1 Observed output (native run, excerpt)

```
[enter] depth=0
  &local_int=0x7ffc2cc20dd4  p_local=0x7ffc2cc20dd4  local_int=100
  local_buf=0x7ffc2cc20de0  local_buf[0]=A
  &marker=0x7ffc2cc20e24  marker=0
[enter] depth=1
  &local_int=0x7ffc2cc20da4  ...  &marker=0x7ffc2cc20df4  marker=10
[enter] depth=2
  &local_int=0x7ffc2cc20d74  ...  &marker=0x7ffc2cc20dc4  marker=20
[enter] depth=3
  &local_int=0x7ffc2cc20d44  ...  &marker=0x7ffc2cc20d94  marker=30
[exit] depth=3 ... identical addresses and values to [enter] depth=3
...
[exit] depth=0 ... identical addresses and values to [enter] depth=0
```

Valgrind: 0 errors, no heap block allocated by the program. All the program's data lives **on the stack**.

### 1.2 Frame layouts (measured with gdb, offsets relative to the frame's `rbp`)

**`walk_stack(depth, max_depth)`**: 0x30 = 48 bytes per call

| Offset | Content | Lifetime |
|---|---|---|
| `rbp+8` | return address (into `main` or into the caller `walk_stack`) | whole call |
| `rbp+0` | saved `rbp` of the caller | whole call |
| `rbp-0x08` | stack-protector canary (`-fstack-protector-strong`, Ubuntu default, because `&marker` is taken) | whole call |
| `rbp-0x0c` | `int marker` | whole call, **including while the recursive call runs** |
| `rbp-0x14` | `int depth` (parameter copy) | whole call |
| `rbp-0x18` | `int max_depth` (parameter copy) | whole call |

**`dump_frame(label, depth)`**: 0x50 = 80 bytes per call

| Offset | Content | Lifetime |
|---|---|---|
| `rbp+8` | return address (into `walk_stack`) | one call |
| `rbp+0` | saved `rbp` of `walk_stack` | one call |
| `rbp-0x08` | canary | one call |
| `rbp-0x20 … rbp-0x11` | `char local_buf[16]`: only `[0]` and `[1]` are initialised, `[2..15]` are indeterminate | one call |
| `rbp-0x28` | `int *p_local` = `&local_int` | one call |
| `rbp-0x2c` | `int local_int` = `100 + depth` | one call |
| `rbp-0x38` | `const char *label`: the pointer is on the stack, but it points to the string literal `"enter"`/`"exit"` in `.rodata`, which is **neither stack nor heap** and lives for the whole program | one call (the pointer) |
| `rbp-0x3c` | `int depth` (parameter copy) | one call |

Note: inside `dump_frame`, `local_buf` (`…de0`) is at a **higher** address than `local_int` (`…dd4`), even though it is declared after it. The order of locals inside a frame is chosen by the compiler (here, the array is placed next to the canary). It does not follow declaration order.

**Aliasing in this program**: `p_local` is an alias of `&local_int`. The two printed values are always equal. The alias is valid because `p_local` and `local_int` are in the **same frame** and die together. No pointer to a local ever escapes its frame.

### 1.3 Step-by-step map (real addresses, prefix `0x7ffc2cc20` omitted)

Each `walk_stack` frame is 0x30 bytes lower than its caller: `marker` goes e24 → df4 → dc4 → d94.

| Step | Code point | Active frames (top = deepest) | What changes |
|---|---|---|---|
| S1 | `main` calls `walk_stack(0,3)` (l.35) | `walk_stack(0)` [rbp=e30, marker@e24=0] / `main` | new frame created, `marker` initialised to 0 |
| S2 | l.22 `dump_frame("enter",0)` | `dump_frame` [rbp=e00, local_int@dd4=100, local_buf@de0="A", p_local=dd4] / `walk_stack(0)` / `main` | temporary frame created **below** `walk_stack(0)` |
| S3 | `dump_frame` returns | `walk_stack(0)` / `main` | the `dump_frame` frame is **destroyed**. The bytes between dc0 and e10 are now free stack space but still contain the old values |
| S4 | l.26 `walk_stack(1,3)` | `walk_stack(1)` [rbp=e00, marker@df4=10] / `walk_stack(0)` / `main` | the new frame occupies **the same addresses** as the destroyed `dump_frame` frame from S2 (its `rbp` is also e00) |
| S5 | `dump_frame("enter",1)` | `dump_frame` [local_int@da4=101] / `walk_stack(1)` / `walk_stack(0)` / `main` | same pattern, 0x30 lower |
| S6–S9 | depths 2 and 3 | maximum depth: `walk_stack(3)` (marker@d94=30) + `dump_frame` (local_int@d44=103) | 4 `walk_stack` frames + 1 `dump_frame` frame are alive at the same time |
| S10 | `depth == max_depth` → no recursion. l.28 `dump_frame("exit",3)` | same position as the enter call at depth 3 | a **new** `dump_frame` frame, built at the same `rsp`, so it gets the same addresses. `local_int` = 103 because it is **recomputed**, not because the old value survived |
| S11 | `walk_stack(3)` returns | `walk_stack(2)` / … | the depth-3 frame is destroyed. `marker` of depth 2 (dc4 = 20) was never touched |
| S12–S14 | exit for depths 2, 1, 0 | frames unwind in LIFO order | each `exit` line shows the same addresses as its `enter` line |
| S15 | `walk_stack(0)` returns to `main` | `main` | all recursion frames are destroyed. `main` returns 0 |

### 1.4 Lifetimes: key points

- `marker` of depth *d* **remains alive** during all deeper calls. It is only out of *scope* (its name is not visible inside `walk_stack(d+1)`). Lifetime ≠ scope.
- `local_int`, `local_buf` and `p_local` exist only between the entry and the `return` of **one** `dump_frame` call. The `enter` and `exit` calls at the same depth create **two distinct objects** that happen to have the same address.
- The fact that an address reappears does **not** prove that a variable was preserved. It only proves that `rsp` had the same value when the call was made.

---

## 2. `aliasing_example.c`

### 2.1 Build note (real behaviour)

With the Makefile flags, **GCC 13 refuses to build this program**:

```
aliasing_example.c:40:5:  error: pointer 'b' used after 'free' [-Werror=use-after-free]
aliasing_example.c:42:36: error: pointer 'b' used after 'free' [-Werror=use-after-free]
aliasing_example.c:44:6:  error: pointer 'b' used after 'free' [-Werror=use-after-free]
aliasing_example.c:45:34: error: pointer 'b' used after 'free' [-Werror=use-after-free]
```

`-Wuse-after-free` (GCC ≥ 12, enabled by `-Wall`) follows the copy `b = a`. So the compiler already knows that `b` is an alias of `a`. For this analysis only, the binary was built with the same flags plus `-Wno-use-after-free`. The source file was not modified.

Line 40 is flagged even though it only *prints* `b`. In ISO C, once a block is freed, the **value** of every pointer to it becomes indeterminate (C11 §6.2.4p2), not only the pointed-to memory.

### 2.2 Observed output

Native run:
```
  a=0x556b8467b2b0 b=0x556b8467b2b0 a[2]=22 b[2]=22
  after free(a): b=0x556b8467b2b0 (dangling)
  reading b[2]=-857494026      <- changes every run
  wrote b[3]=1234
```
Under Valgrind: `reading b[2]=22`, and **3 errors** are reported (invalid read l.42, invalid write l.44, invalid read l.45), all inside "a block of size 20 free'd" at l.38, allocated at l.12 (`make_numbers`) called from l.30.

### 2.3 Step-by-step map (gdb run, ASLR disabled: heap block `H = 0x55555555a2b0`)

| Step | Code point | Stack | Heap | Aliases / owner |
|---|---|---|---|---|
| A1 | `main` entry, l.24–26 | `main` frame: `a`@…72e0 = NULL, `b`@…72e8 = NULL, `n`@…72dc = 5 | — | none |
| A2 | l.30 → `make_numbers(5)` entry | + `make_numbers` frame (rbp …72c0): `n`@…72ac = 5, `i`@…72b4 = 0, `arr`@…72b8 = NULL | — | — |
| A3 | l.12 `malloc(5*4 = 20)` | `arr` = H | **block H**, 20 bytes requested. glibc chunk header at H-16: size field `0x21`, i.e. a 32-byte chunk with 24 usable bytes | owner: `arr` (stack) |
| A4 | l.16–17 loop | `i` goes 0→5 | H = `{0, 11, 22, 33, 44}` (bytes 0–19). Bytes 20–23 are padding | owner: `arr` |
| A5 | l.19 `return arr` | the `make_numbers` frame (`n`, `i`, `arr`) is **destroyed**. The address H is copied into `rax`, then into `a` | H **still alive**. The heap does not depend on frames | ownership transferred to `a` |
| A6 | l.34 `b = a` | `a` = H, `b` = H | unchanged. **No copy of the data**, only the 8-byte address is copied | `a` and `b` are **aliases**. `a` is the owner; `b` must not free |
| A7 | l.36 printf | — | reads H[2] twice through the two names → 22, 22 | same memory, two names |
| A8 | l.38 `free(a)` | `a` and `b` **still contain H** (`free` cannot modify the caller's variables) | lifetime of H **ends**. glibc puts the chunk into the **tcache**: bytes 0–7 = protected `next` pointer `(H >> 12) ^ NULL = 0x55555555a` (*safe-linking*, glibc ≥ 2.32); bytes 8–15 = random tcache *key* (`0x9bd7b870_60218e3e`); bytes 16–19 still hold 44 | `a` and `b` are **both dangling**. Nobody owns H any more |
| A9 | l.42 read `b[2]` | — | `b[2]` = bytes 8–11 of H = low half of the tcache key → `0x60218e3e`. Natively it is a random integer, **not 22** | undefined behaviour (use-after-free read) |
| A10 | l.44 `b[3] = 1234` | — | bytes 12–15 = **high half of the tcache key** overwritten with `0x000004d2`. This corrupts allocator metadata, without a crash here because the program does not call `free`/`malloc` again | undefined behaviour (use-after-free write) |
| A11 | l.45 read `b[3]` | — | reads back 1234 | undefined behaviour (3rd error) |
| A12 | `return 0` | `main` frame destroyed | heap: 0 bytes in use (H was already freed) | no leak, but 3 invalid accesses |

### 2.4 Why the native and Valgrind values differ

Valgrind **replaces** `malloc`/`free` with its own allocator. A freed block is placed in a quarantine and its bytes are not reused for tcache metadata, so `b[2]` still reads 22. Natively, glibc immediately writes its metadata into the freed block. **Same code, different memory state**: printing 22 does not mean the access is valid. Only Memcheck's "Invalid read" diagnosis tells you whether an access is allowed.

---

## 3. AI review: errors found and corrections

**AI tool used:** Claude (Anthropic), asked to read the code only, without compiling or running it. Prompt summary: *"Generate step-by-step memory maps (stack frames, heap, pointer aliases, lifetimes) for stack_example.c and aliasing_example.c, say what each printf prints, whether the program compiles with the Makefile flags, and what Valgrind will report."* Each claim was then checked against the real compilation, execution, gdb and Valgrind.

### Error 1: wrong prediction about compilation (incorrect)

- **AI claim:** *"Compilation with -Wall -Wextra -Werror -pedantic -std=gnu89 -g: I expect it to compile."* It then added a caveat that `-Wuse-after-free` *might* fire on `b[2]` or `b[3]`.
- **Reality:** the build **fails** with 4 errors (lines 40, 42, 44, 45), see §2.1.
- **Why the AI was wrong:** it reasoned only about C90 syntax and `printf` formats. It treated alias tracking by the compiler as unlikely. In fact, at `-O0` GCC's `-Wuse-after-free` sees that `b` was assigned from `a` before `free(a)`.
- **What the AI missed completely:** line 40 is also an error, even though it never dereferences `b`. After `free`, using the pointer **value** itself is already undefined (C11 §6.2.4p2). The AI only considered `b[2]` and `b[3]`.
- **Correction:** documented in §2.1. The binary was built with `-Wno-use-after-free` for analysis only.

### Error 2: incomplete Valgrind prediction

- **AI claim:** *"Valgrind should report an invalid read of size 4 at b[2] and an invalid write of size 4 at b[3]."* That is 2 errors.
- **Reality:** `ERROR SUMMARY: 3 errors from 3 contexts`. The AI forgot the **read** of `b[3]` in the `printf` on line 45.
- **Correction:** every access through a dangling pointer is a separate error. Line 44 writes `b[3]` and line 45 reads `b[3]`, which are two distinct accesses.

### Error 3: an explanation valid in only one context (misleading)

- **AI claim:** *"reading b[2] … The value is the low 32 bits of the tcache key"*, and the tcache *"writes a next pointer into bytes 0-7"*.
- **Verified:** natively this is correct: `b[2] = 0x60218e3e` is the low half of the key read in gdb.
- **Misleading points:**
  1. The claim is presented as *the* answer, but under Valgrind the same line prints **22** (§2.4). A student who checks with Valgrind would wrongly conclude that the memory is still valid.
  2. Bytes 0–7 do **not** contain a raw `next` pointer. Since glibc 2.32 it is mangled by *safe-linking*: `(H >> 12) ^ next` = `0x55555555a` here, because `next = NULL`.
- **Correction:** I stated which allocator the value depends on, and showed the real contents of the freed chunk (§2.3, A8).

### Error 4: correct but incomplete stack explanation

- **AI claim:** the exit lines show the same addresses *"because the recursive call has returned and restored rsp, so dump_frame's frame lands in the same slot"*. It also said the gap between depths *"is not predictable without running the program."*
- **Verified:** the reasoning is correct, and the measured gap is **0x30 = 48 bytes** (return address 8 + saved rbp 8 + canary 8 + three `int` + alignment).
- **Missing:** the AI did not say that, between the `enter` and `exit` calls, those **same addresses are reused by `walk_stack(d+1)`**. Both the destroyed `dump_frame` frame and the next `walk_stack` frame have `rbp = …e00` (§1.3 S4). It also did not explain that identical values come from **re-initialisation**, not preservation. Without this, a reader may believe that a "slot" belongs permanently to `dump_frame`.
- **Correction:** added steps S3, S4 and S10, and the "Lifetimes" section (§1.4).

**Takeaway:** the AI got the *mechanisms* right (stack grows down, aliases, dangling pointer). It failed whenever the answer depended on the **real toolchain** (GCC version, glibc version, Valgrind's allocator). Those claims must be treated as hypotheses and checked by execution.

---

## 4. Optional: `heap_example.c` (summary)

| Step | Stack (`main`) | Heap | Owner |
|---|---|---|---|
| `alice = person_new("Alice",30)` | `alice` → P1 | P1 = `Person` (16 bytes: `char *name` 8 + `int age` 4 + 4 padding); N1 = 6 bytes `"Alice\0"`, with `P1->name = N1` | `alice` owns P1, **P1->name owns N1** |
| `bob = person_new("Bob",41)` | `bob` → P2 | P2, N2 = 4 bytes `"Bob\0"` | `bob` → P2 → N2 |
| `free(bob->name); free(bob);` | `bob` dangling (not used afterwards) | N2 then P2 freed, in the correct order (child before parent) | — |
| `person_free_partial(alice)` | `alice` dangling | **only P1 freed**. N1 is still allocated, but the only pointer to it (`P1->name`) was inside P1, which no longer exists | N1 has no owner → **leak** |

Valgrind confirms: `6 bytes in 1 blocks are definitely lost`, allocated at `heap_example.c:21` (`person_new`) called from l.51. The parameter `name` in `person_new` points to a string literal in `.rodata` (neither stack nor heap). It is copied byte by byte into the heap; it is not aliased.

Si git push affiche une erreur, copie-la-moi ici et je t'explique comment la régler.

Memory maps
Document
·MD

refait moi lr texte a copier de maniere plus fluide

J'ai réécrit le fichier pour qu'il se lise comme une explication : des étapes racontées une par une, au lieu de gros tableaux. Le contenu et toutes les vérifications restent les mêmes. Il est toujours en anglais, comme le reste de ton dépôt Holberton.

Le fichier memory_maps.md est envoyé : le télécharger est le plus simple. Sinon, copie uniquement ce qui se trouve entre les deux lignes de quatre accents graves (````).

markdown
# Memory Maps — AI Memory Visualizer

This document describes, step by step, how memory behaves in `stack_example.c` and `aliasing_example.c` (plus a short optional section on `heap_example.c`). Every map was first produced with the help of an AI tool, then checked against the real program: compilation, normal execution, `gdb` and Valgrind.

**Environment:** x86-64 Linux, gcc 13.3.0, glibc 2.39, Makefile flags `-Wall -Wextra -Werror -pedantic -std=gnu89 -g`. Addresses change between runs because of ASLR. The values below come from one real run. What matters is the relation between the addresses, not the addresses themselves.

---

## 1. Key ideas used in this analysis

**The stack** holds one *frame* per function call that is still running. A frame contains the return address, the caller's saved `rbp`, a copy of the parameters and the local variables. It is created when the function is entered and destroyed when it returns. On x86-64, the stack grows downwards, so a deeper call always has a lower address. Once a frame is destroyed, its bytes are not erased: they are simply reused by the next call.

**The heap** holds the blocks returned by `malloc`. A heap block does not belong to any frame. It lives from `malloc` until `free`, no matter which function allocated it.

**Lifetime and scope are different things.** The lifetime is the period during which a variable exists in memory. The scope is the part of the code where its name can be used. A variable can be alive while its name is not visible.

**Owner and alias.** The owner of a heap block is the pointer responsible for calling `free`. An alias is any other pointer that holds the same address. After `free`, the owner and all aliases become *dangling pointers*: they still contain the address, but the block no longer exists.

---

## 2. `stack_example.c`: recursion and stack frames

### 2.1 What the program does

`main` calls `walk_stack(0, 3)`. Each `walk_stack` stores `marker = depth * 10`, calls `dump_frame("enter", depth)`, recurses down to depth 3, then calls `dump_frame("exit", depth)` on the way back up. Everything in this program lives on the **stack**. Valgrind reports 0 errors and the program never allocates heap memory.

Observed output (excerpt):

```
[enter] depth=0
  &local_int=0x7ffc2cc20dd4  p_local=0x7ffc2cc20dd4  local_int=100
  local_buf=0x7ffc2cc20de0  local_buf[0]=A
  &marker=0x7ffc2cc20e24  marker=0
[enter] depth=1   ... &marker=0x7ffc2cc20df4  marker=10
[enter] depth=2   ... &marker=0x7ffc2cc20dc4  marker=20
[enter] depth=3   ... &marker=0x7ffc2cc20d94  marker=30
[exit]  depth=3   ... same addresses and values as [enter] depth=3
...
[exit]  depth=0   ... same addresses and values as [enter] depth=0
```

### 2.2 What each frame contains

Measured with `gdb`. Offsets are relative to the frame's `rbp`.

A `walk_stack` frame is **48 bytes (0x30)**. It holds the return address (`rbp+8`), the saved `rbp` (`rbp+0`), a stack-protector canary (`rbp-0x08`), `marker` (`rbp-0x0c`), and copies of `depth` (`rbp-0x14`) and `max_depth` (`rbp-0x18`). All of these live for the whole call, **including while the deeper recursive calls are running**.

A `dump_frame` frame is **80 bytes (0x50)**. It holds the return address, the saved `rbp`, a canary, `local_buf[16]` (`rbp-0x20`), `p_local` (`rbp-0x28`), `local_int` (`rbp-0x2c`), and copies of `label` (`rbp-0x38`) and `depth` (`rbp-0x3c`). All of these live only for **one** call.

Three details are worth noting:

- Only `local_buf[0]` and `local_buf[1]` are initialised. Bytes 2 to 15 contain whatever was left on the stack.
- `label` is a pointer stored on the stack, but the string it points to (`"enter"` or `"exit"`) is a literal stored in the read-only data segment (`.rodata`). That string is neither on the stack nor on the heap, and it lives for the whole program.
- `local_buf` (`…de0`) sits at a higher address than `local_int` (`…dd4`) even though it is declared after it. The compiler chooses the order of locals inside a frame. Declaration order says nothing about memory order.

**Aliasing:** `p_local` holds `&local_int`, so it is an alias of `local_int`. This alias is always safe because both variables live in the same frame and die at the same moment. No address of a local variable ever leaves its frame.

### 2.3 Step by step (addresses without the `0x7ffc2cc20` prefix)

**Step 1: `main` calls `walk_stack(0, 3)` (line 35).** A first `walk_stack` frame is created with `rbp = e30`. `marker` is stored at `e24` and set to 0.

**Step 2: `dump_frame("enter", 0)` (line 22).** A `dump_frame` frame is created just below, with `rbp = e00`. `local_int` is at `dd4` and holds 100. `p_local` holds `dd4`, the same address. `local_buf` is at `de0` and holds `"A"`.

**Step 3: `dump_frame` returns.** Its frame is destroyed. The bytes from `dc0` to `e10` become free stack space, but the old values are still physically there.

**Step 4: recursive call `walk_stack(1, 3)` (line 26).** The new frame is built on **exactly the same addresses** as the `dump_frame` frame that was just destroyed: its `rbp` is also `e00`. Its `marker` is at `df4` and holds 10. The `marker` of depth 0, at `e24`, is still alive and unchanged. It is simply not visible from here.

**Step 5: depths 1, 2 and 3 repeat the same pattern.** Each level is 0x30 bytes lower than the previous one, so `marker` moves from `e24` to `df4`, then `dc4`, then `d94`. At the deepest point, four `walk_stack` frames and one `dump_frame` frame are alive at the same time.

**Step 6: at depth 3 there is no more recursion, so `dump_frame("exit", 3)` is called (line 28).** The stack pointer has the same value as during the `enter` call, so the new frame gets the same addresses. But it is a **new frame**: `local_int` shows 103 again because it is recalculated as `100 + depth`, not because the old value survived.

**Step 7: the frames are unwound in reverse order (LIFO).** Each `walk_stack` returns, its frame disappears, and the caller's `marker` is still intact. Finally `walk_stack(0)` returns to `main` and `main` returns 0.

### 2.4 Lifetimes: what to remember

- `marker` at depth *d* stays alive for the whole duration of all the deeper calls. It is out of scope there, but it is not dead.
- `local_int`, `local_buf` and `p_local` live for one `dump_frame` call only. The `enter` and `exit` calls at the same depth create **two different variables that happen to share the same address**.
- Seeing the same address twice does not prove that a variable was preserved. It only proves that the stack pointer had the same value when the function was called.

---

## 3. `aliasing_example.c`: alias and use-after-free

### 3.1 First observation: the program does not compile

With the Makefile flags, GCC 13 stops with four errors:

```
aliasing_example.c:40:5:  error: pointer 'b' used after 'free' [-Werror=use-after-free]
aliasing_example.c:42:36: error: pointer 'b' used after 'free' [-Werror=use-after-free]
aliasing_example.c:44:6:  error: pointer 'b' used after 'free' [-Werror=use-after-free]
aliasing_example.c:45:34: error: pointer 'b' used after 'free' [-Werror=use-after-free]
```

The `-Wuse-after-free` warning (GCC 12 and later, enabled by `-Wall`) follows the copy `b = a`. In other words, the compiler itself detects the alias. Because `-Werror` turns warnings into errors, the build fails. For this analysis only, the program was built with the same flags plus `-Wno-use-after-free`. The source file was not modified.

Line 40 is reported even though it only prints the value of `b` without dereferencing it. In C, once a block is freed, the value of every pointer to that block becomes indeterminate (C11 §6.2.4p2). Even reading the pointer itself is no longer valid.

### 3.2 Observed output

Normal run:

```
  a=0x556b8467b2b0 b=0x556b8467b2b0 a[2]=22 b[2]=22
  after free(a): b=0x556b8467b2b0 (dangling)
  reading b[2]=-857494026      <- different on every run
  wrote b[3]=1234
```

Under Valgrind, the same line prints `reading b[2]=22`, and Valgrind reports **3 errors**: an invalid read at line 42, an invalid write at line 44 and an invalid read at line 45. All three point to "a block of size 20 free'd" at line 38, allocated at line 12 in `make_numbers`, called from line 30.

### 3.3 Step by step (gdb run with ASLR disabled, heap block `H = 0x55555555a2b0`)

**Step 1: entering `main` (lines 24–26).** The `main` frame contains three variables on the stack: `a` = NULL, `b` = NULL and `n` = 5. Nothing exists on the heap yet.

**Step 2: calling `make_numbers(5)` (line 30).** A new frame is created on the stack with `n` = 5, `i` = 0 and `arr` = NULL.

**Step 3: `malloc(20)` (line 12).** Twenty bytes (5 × `sizeof(int)`) are requested. glibc returns block `H` on the heap. Just before it, at `H - 16`, glibc stores a header whose size field is `0x21`: the real chunk is 32 bytes, 24 of them usable. `arr` now holds `H`. At this point, `arr` (on the stack) is the owner of the block (on the heap).

**Step 4: the loop (lines 16–17).** `i` goes from 0 to 5 and the block is filled with `{0, 11, 22, 33, 44}`.

**Step 5: `return arr` (line 19).** The `make_numbers` frame is destroyed: `n`, `i` and `arr` no longer exist. But block `H` is still alive, because the heap does not depend on frames. The address is copied into `a`, which becomes the new owner.

**Step 6: `b = a` (line 34).** Only the 8-byte address is copied. The data itself is not duplicated. `a` and `b` now both hold `H`: they are **aliases**. `a` is the owner, and `b` must never be passed to `free`.

**Step 7: `printf` (line 36).** `a[2]` and `b[2]` read the same integer through two different names and both print 22.

**Step 8: `free(a)` (line 38).** The lifetime of `H` ends here. `free` cannot change the caller's variables, so `a` and `b` still contain `H`. Both are now dangling, and no pointer owns the block any more. glibc immediately reuses the freed block to store its own bookkeeping data (the *tcache*):

- bytes 0–7 now hold the "next" pointer, protected by *safe-linking* (glibc 2.32 and later): `(H >> 12) ^ NULL = 0x55555555a`;
- bytes 8–15 now hold a random security key, here `0x9bd7b870_60218e3e`;
- bytes 16–19 still hold 44.

**Step 9: reading `b[2]` (line 42).** `b[2]` is bytes 8 to 11 of the freed block, which is the low half of the tcache key: `0x60218e3e`. This is why a random number is printed instead of 22. This is a use-after-free **read**.

**Step 10: `b[3] = 1234` (line 44).** This writes into bytes 12 to 15, which overwrites the high half of the tcache key. glibc's internal data is now corrupted. The program does not crash only because it never calls `malloc` or `free` again. This is a use-after-free **write**.

**Step 11: reading `b[3]` (line 45).** The program reads back the 1234 it just wrote. This is the third invalid access.

**Step 12: `return 0`.** The `main` frame is destroyed. There is no leak, because the block was freed, but there were three invalid memory accesses.

### 3.4 Why Valgrind prints 22 and a normal run does not

Valgrind replaces `malloc` and `free` with its own versions. A freed block is set aside without being reused, so its bytes keep their old values and `b[2]` still shows 22. A normal run uses glibc, which writes its bookkeeping data into the freed block straight away.

The lesson: **a value that looks correct does not mean the access is valid.** Only Valgrind's "Invalid read" or "Invalid write" diagnosis tells you whether you were allowed to touch that memory.

---

## 4. Reviewing the AI's explanations

**Method:** I gave both programs to an AI assistant (Claude, by Anthropic) and asked it to produce step-by-step memory maps (stack frames, heap blocks, aliases, lifetimes), to predict what each `printf` prints, whether the code compiles with the Makefile flags, and what Valgrind will report. The AI only read the code; it did not compile or run anything. I then checked each claim against real compilation, execution, `gdb` and Valgrind. I found four problems.

### Error 1 — Incorrect: "it compiles"

The AI wrote: *"Compilation with -Wall -Wextra -Werror -pedantic -std=gnu89 -g: I expect it to compile."* It added that `-Wuse-after-free` might possibly fire on `b[2]` or `b[3]`.

**In reality**, the build fails with four errors (section 3.1). The AI only checked C90 syntax and `printf` formats, and assumed the compiler would not follow the alias `b = a`. GCC does follow it, even at `-O0`. The AI also completely missed line 40, which only prints the pointer without dereferencing it. That line is still an error, because the pointer's value itself becomes invalid after `free`.

**Correction:** section 3.1 documents the real compiler output and explains why line 40 is flagged.

### Error 2 — Incomplete: "Valgrind will report 2 errors"

The AI predicted an invalid read on `b[2]` and an invalid write on `b[3]`, which makes two errors.

**In reality**, Valgrind reports **3 errors**. The AI forgot that the `printf` on line 45 **reads** `b[3]` again. Each access through a dangling pointer is a separate error: line 44 writes `b[3]`, then line 45 reads it.

**Correction:** section 3.3, steps 9 to 11, lists all three accesses.

### Error 3 — Misleading: "b[2] prints the tcache key"

The AI explained that `b[2]` prints the low half of the tcache key, and that glibc writes a "next pointer" into bytes 0 to 7.

**What was verified:** in a normal run this is correct. `gdb` shows `b[2] = 0x60218e3e`, which is indeed part of the key.

**Why it is misleading:**
1. The AI presented this as *the* answer, but under Valgrind the same line prints **22** (section 3.4). A student checking with Valgrind might wrongly conclude that the memory is still valid.
2. Bytes 0 to 7 do not contain a plain pointer. Since glibc 2.32, that pointer is scrambled by safe-linking: `(H >> 12) ^ next`, which gives `0x55555555a` here.

**Correction:** the analysis now states which allocator the value depends on, and shows the real contents of the freed block (section 3.3, step 8).

### Error 4 — Incomplete: the stack explanation

The AI correctly said that the `exit` lines show the same addresses as the `enter` lines *"because the recursive call has returned and restored rsp, so dump_frame's frame lands in the same slot"*. It added that the gap between two depths *"is not predictable without running the program."*

**What was verified:** the reasoning is right. The measured gap is **0x30 = 48 bytes**.

**What was missing:** the AI did not say that between the `enter` and `exit` calls, those same addresses are **reused by the next `walk_stack` frame**: the destroyed `dump_frame` frame and `walk_stack(d+1)` both have `rbp = …e00`. It also did not explain that the values are identical because they are **recalculated**, not preserved. Without this, a reader might think that an area of the stack belongs permanently to `dump_frame`.

**Correction:** steps 3, 4 and 6 of section 2.3 and the whole of section 2.4 were added.

### What I take away from this

The AI understood the general mechanisms well: the stack grows downwards, `b = a` creates an alias, and a pointer is dangling after `free`. It went wrong every time the answer depended on the **real tools**: the GCC versio©◊◊

n, the glibc version, and the allocator Valgrind uses. Its explanations are useful hypotheses, but every one of them has to be checked by actually running the program.


