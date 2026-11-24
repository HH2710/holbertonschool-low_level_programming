# Valgrind Analysis — AI Memory Tracer

This document explains every issue Valgrind reports for the programs of this project. For each one, I identify the type of error, the exact memory object involved, and the lifetime violation that causes it. All explanations are cross-checked against the source code and against the memory maps in `memory_maps.md` (Task 0).

**Environment:** x86-64 Linux, gcc 13.3.0, glibc 2.39, Valgrind 3.22.0 (Memcheck). Programs built with the Makefile flags `-Wall -Wextra -Werror -pedantic -std=gnu89 -g`. `aliasing_example` was built with `-Wno-use-after-free` added, because GCC 13 refuses to compile it otherwise (see `memory_maps.md`, section 3.1).

---

## 1. How Valgrind was run

```bash
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./heap_example
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./aliasing_example
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./stack_example
```

Why these flags:

- `--leak-check=full` shows the allocation call stack of every leaked block, not just a total.
- `--show-leak-kinds=all` reports every leak category (definitely, indirectly, possibly lost, still reachable), so nothing is hidden.
- `--track-origins=yes` makes Valgrind say where an uninitialised value came from, if one is ever used. It is needed to check the "uninitialised memory" part of the task properly.

`crash_example` is analysed in the crash report task and is not covered here.

### Summary of findings

| Program | Valgrind finding | Classification | Memory object |
|---|---|---|---|
| `heap_example` | 6 bytes definitely lost | **Leak due to lost ownership** | `"Alice\0"` name buffer (N1), allocated at line 21 |
| `aliasing_example` | Invalid read of size 4, line 42 | **Use-after-free (read)** | 20-byte array `H`, bytes 8–11 (`b[2]`) |
| `aliasing_example` | Invalid write of size 4, line 44 | **Use-after-free (write)** | 20-byte array `H`, bytes 12–15 (`b[3]`) |
| `aliasing_example` | Invalid read of size 4, line 45 | **Use-after-free (read)** | 20-byte array `H`, bytes 12–15 (`b[3]`) |
| `stack_example` | 0 errors | none | — |
| all programs | no "uninitialised value" report | none | — |

---

## 2. `heap_example`: a leak due to lost ownership

### 2.1 What Valgrind reports

```
HEAP SUMMARY:
    in use at exit: 6 bytes in 1 blocks
  total heap usage: 5 allocs, 4 frees, 4,138 bytes allocated

6 bytes in 1 blocks are definitely lost in loss record 1 of 1
   at 0x4846828: malloc (vgpreload_memcheck-amd64-linux.so)
   by 0x109211: person_new (heap_example.c:21)
   by 0x1092FA: main (heap_example.c:51)

LEAK SUMMARY:
   definitely lost: 6 bytes in 1 blocks
   indirectly lost: 0 bytes in 0 blocks
```

### 2.2 Which object is involved

The stack trace says the leaked block was allocated at **line 21** (`p->name = malloc(len + 1)`) during the call made at **line 51** (`person_new("Alice", 30)`). That is the buffer holding Alice's name. "Alice" has 5 characters, plus the `'\0'` terminator: **6 bytes**, exactly the size Valgrind reports.

### 2.3 What went wrong, step by step

Here is the ownership chain built by `person_new("Alice", 30)` (see `memory_maps.md`, section 5):

```
stack: alice ──► heap: P1 (Person, 16 bytes) ──► heap: N1 ("Alice\0", 6 bytes)
                       P1->name = N1
```

- `alice`, a local variable of `main` on the stack, owns P1.
- `P1->name`, a field stored **inside P1 on the heap**, is the **only** pointer to N1. P1 therefore owns N1.

At line 69, `person_free_partial(alice)` calls `free(p)` and nothing else. The order of events is:

1. P1's lifetime ends. The field `P1->name` stops existing with it.
2. N1 is still allocated, so its lifetime has not ended.
3. But the only pointer that held N1's address was inside P1, and it is gone. No pointer anywhere in the program (stack, globals or live heap blocks) contains N1's address any more.

N1 is therefore alive but unreachable. It can never be freed. This is a **leak due to lost ownership**: the owner (P1) was destroyed before it released what it owned. The correct order would have been `free(p->name)` and then `free(p)`, which is exactly what `main` does for Bob at lines 66–67. Bob's blocks are not reported.

### 2.4 Why "definitely lost" and not "indirectly lost"

At exit, Valgrind scans memory for pointers to each block that is still allocated:

- **Definitely lost**: no pointer to the block exists anywhere.
- **Indirectly lost**: the only pointers to the block are inside **another block that is itself leaked**.

If the program had forgotten to free P1 as well, P1 would be "definitely lost" and N1 would be "indirectly lost", because it would still be pointed to by a leaked block. Here P1 **was** freed, so it is not a leaked block that could point to N1. N1 is pointed to by nothing at all, which makes it **definitely lost**.

### 2.5 Reading the totals: 5 allocs, 4 frees

The program calls `malloc` 4 times (P1, N1, P2, N2) and `free` 3 times (N2, P2, P1). The fifth allocation and the fourth free belong to **glibc**: the first `printf` allocates the buffer behind `stdout`, and glibc frees it when the program exits.

The byte count confirms it: 16 + 6 + 16 + 4 = 42 bytes for the program, and 4,138 − 42 = 4,096 bytes for the `stdout` buffer.

The size of this buffer depends on where `stdout` goes. When the output is piped or redirected, glibc uses a 4,096-byte buffer, which gives the 4,138 total above. When the program writes directly to a terminal, glibc uses a 1,024-byte buffer and Valgrind reports **1,066 bytes allocated** (1,024 + 42). I checked both cases. Either way, the buffer is freed, so it is not part of the leak.

---

## 3. `aliasing_example`: three use-after-free accesses

### 3.1 What Valgrind reports

```
Invalid read of size 4
   at 0x1092F5: main (aliasing_example.c:42)
 Address 0x4a7f088 is 8 bytes inside a block of size 20 free'd
   at 0x484988F: free (...)
   by 0x1092D1: main (aliasing_example.c:38)
 Block was alloc'd at
   at 0x4846828: malloc (...)
   by 0x1091E4: make_numbers (aliasing_example.c:12)
   by 0x109272: main (aliasing_example.c:30)

Invalid write of size 4
   at 0x109315: main (aliasing_example.c:44)
 Address 0x4a7f08c is 12 bytes inside a block of size 20 free'd
   (same free and malloc stacks)

Invalid read of size 4
   at 0x109323: main (aliasing_example.c:45)
 Address 0x4a7f08c is 12 bytes inside a block of size 20 free'd
   (same free and malloc stacks)

All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 3 errors from 3 contexts
```

### 3.2 How to read one of these reports

Each report has three parts, and each part answers a question:

1. **"Invalid read/write of size 4 at … line N"**: *what* was done, and *where*. Size 4 means one `int` was read or written.
2. **"Address … is X bytes inside a block of size 20 free'd at … line 38"**: *which object* was touched, and *when its lifetime ended*.
3. **"Block was alloc'd at … line 12 … line 30"**: *when that object was created*.

Together they give the full life of the object: created at line 12, destroyed at line 38, accessed afterwards at line 42, 44 or 45.

### 3.3 The memory object involved

All three errors concern the same object: the 20-byte heap block `H` (5 `int`s) allocated by `malloc` in `make_numbers` (line 12), returned to `main` and stored in `a` (line 30).

At line 34, `b = a` makes `b` an **alias** of `a`: two pointers, one block. `a` is the owner. At line 38, `free(a)` ends the lifetime of `H`. From that point on, `a` and `b` are both **dangling pointers**: they still hold the address of `H`, but `H` no longer exists (see `memory_maps.md`, section 3.3, step 8).

The program frees through `a` and then keeps using `b`. That is the trap of aliasing: freeing through one name invalidates every other name for the same block.

### 3.4 Each error, one by one

**Error 1: line 42, `printf("... %d", b[2])` — use-after-free read.**
`b[2]` is at offset `2 × sizeof(int) = 8`, which is why Valgrind says "8 bytes inside a block of size 20". The program reads an `int` from memory whose lifetime ended at line 38.

**Error 2: line 44, `b[3] = 1234` — use-after-free write.**
`b[3]` is at offset `3 × 4 = 12`, hence "12 bytes inside". This is the most dangerous of the three. Outside Valgrind, glibc stores its own bookkeeping data (a tcache key) in those bytes of the freed block, so this write corrupts the allocator's internal state (see `memory_maps.md`, section 3.3, step 10). A later `malloc` or `free` could then fail or behave unpredictably.

**Error 3: line 45, `printf("... %d", b[3])` — use-after-free read.**
Same offset as error 2, but this time a read. It is a separate access, so Valgrind reports it separately.

### 3.5 What Valgrind does not report, and why

- **Line 40**, `printf("%p", (void *)b)`, is not reported. Valgrind only checks **accesses to memory**. Line 40 reads the variable `b`, which lives on the stack and is perfectly valid. It never touches the freed block. In ISO C, using the value of a dangling pointer is already undefined behaviour, and GCC's `-Wuse-after-free` does flag line 40. Valgrind cannot see this problem, because no invalid memory is accessed.
- **No leak** is reported, because the block was freed (by `free(a)`). A dangling pointer is the opposite of a leak. A leak is a live block that nobody points to. A dangling pointer is a pointer to a block that no longer lives. The totals confirm it: 2 allocs (`H` and the `stdout` buffer), 2 frees.

### 3.6 Why the program prints `reading b[2]=22` under Valgrind

Under Valgrind, the freed block still shows its old value, 22. This does **not** mean the access is valid. Valgrind replaces glibc's `malloc` and `free` with its own versions. A freed block is put aside in a queue and is not reused or overwritten straight away, precisely so that later accesses to it can be detected. Without Valgrind, the same line prints a random number, because glibc immediately writes its bookkeeping data into the freed block (see `memory_maps.md`, section 3.4). The value printed proves nothing. Only Valgrind's "Invalid read" diagnosis tells you whether the access was legal.

---

## 4. Uninitialised memory

None of the programs triggered a `Conditional jump or move depends on uninitialised value(s)` or `Use of uninitialised value` error, even with `--track-origins=yes`. I checked the places where it could have happened:

- **`stack_example`**: `local_buf[16]` has only `[0]` and `[1]` set, so bytes 2 to 15 are uninitialised. But the program only prints `local_buf[0]` with `%c`, and the address of `local_buf` with `%p`. Memcheck only complains when an uninitialised value **affects the program's behaviour** (a branch, a system call, an address computation, or output). Simply having uninitialised bytes in memory is not an error. Valgrind therefore correctly reports 0 errors.
- **`aliasing_example`**: the loop writes all 5 elements of `H` before any read, so every byte is defined. After `free`, `b[2]` reads bytes that are still **defined** (they hold 22) but **no longer allocated**. Memcheck classifies this as an invalid read, not as uninitialised memory. These are two different properties: *addressability* (may I access this byte?) and *definedness* (does this byte hold a known value?).
- **`heap_example`**: every field of each `Person` and every byte of each name, including `'\0'`, is written before it is read. The 4 padding bytes in each `Person` are never read.

---

## 5. Reviewing the AI's interpretation

**Method:** I gave the source code and the raw Valgrind output to an AI assistant (Claude, by Anthropic). I asked it to classify each report, identify the memory object, and explain the lifetime violation. I also asked three specific questions: why "definitely" rather than "indirectly" lost, where the 5 allocs and 4,138 bytes come from, and whether any uninitialised memory was used. The AI did not run anything. I then verified each claim against the code, my memory maps and new Valgrind runs.

Most of the AI's answer was **correct**:
- the leak of the 6-byte `"Alice"` buffer, with the right cause;
- the explanation of "definitely lost";
- the three use-after-free accesses and their offsets;
- the fact that a dangling pointer is not a leak;
- the absence of uninitialised reads.

These points agree with sections 2 to 4. But I found three problems.

### Error 1 — Incorrect reasoning about the 4,096 bytes

The AI computed 4,138 − 42 = 4,096, then wrote that this *"matches the single 4096-byte allocation in stack_example"*.

**Why this is wrong:** each program runs in its own process, with its own heap. `heap_example` cannot be affected by an allocation made in `stack_example`. The arithmetic is right, but the explanation is not. The 4,096 bytes are `heap_example`'s **own** `stdout` buffer, allocated by its first `printf`. The AI did mention the `stdout` buffer a sentence later, but it backed it with an argument based on a different program, which proves nothing.

**Correction:** section 2.5 explains that the fifth allocation belongs to glibc's `stdio` inside the same process.

### Error 2 — Incomplete: the buffer size is not a fixed value

The AI treated "4,096 bytes" as a fixed property of the program.

**Why this is incomplete:** I ran the same program twice. When its output was piped, Valgrind reported **4,138 bytes allocated**. When it was attached to a real terminal (`script -qc "valgrind ./heap_example"`), Valgrind reported **1,066 bytes allocated**. glibc picks the `stdout` buffer size depending on whether the output is a terminal (1,024 bytes) or a pipe or file (4,096 bytes). A student who runs Valgrind in an ordinary terminal will see 1,066, and could wrongly conclude that the program allocates differently. Only the program's own 42 bytes, and the 6-byte leak, are constant.

**Correction:** section 2.5 documents both values and explains why they differ.

### Error 3 — Misleading fix: "set b = NULL after free(a)"

The AI suggested setting `b = NULL` after `free(a)` *"so later reads fail visibly"*.

**Why this is misleading:**
1. It only handles one of the two dangling pointers. After `free(a)`, `a` is dangling too. Following the AI's advice literally leaves `a` dangling. Both aliases must be set to `NULL`, or, better, `b` should not be used after `free` at all.
2. "Fail visibly" here means lines 42 to 45 would dereference `NULL` and crash the program with a segmentation fault. That turns a use-after-free into a NULL pointer dereference. It does make the bug **visible**, which is useful while debugging, but it does not **fix** anything. The real fix is to stop accessing the block after its lifetime ends, for example by moving `free(a)` after the last use of `b`.
3. The task only asks for an analysis, not a fix. The AI went beyond what was asked and, in doing so, offered a "fix" that only works for one of the two aliases.

**Correction:** section 3.3 explains that freeing through one name invalidates **every** alias of the block. A correct fix has to respect that.

### What I take away from this

The AI classified the errors well, because Valgrind's messages are fairly explicit. It went wrong when it had to **justify** a number (the 4,096 bytes) and when it proposed a fix without following the aliasing through to its end. As with Task 0, its answer was a useful starting point, but each claim had to be checked against the code and against a real Valgrind run.
