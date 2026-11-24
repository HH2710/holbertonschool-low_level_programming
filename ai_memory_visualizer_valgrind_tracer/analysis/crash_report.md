# Crash Report — `crash_example`

**Environment:** x86-64 Linux, gcc 13.3.0, glibc 2.39, Valgrind 3.22.0. Built with the Makefile flags `-Wall -Wextra -Werror -pedantic -std=gnu89 -g` (no optimisation, `-O0`).

Note: a plain `make` stops when it fails to build `aliasing_example` (see `memory_maps.md`, section 3.1), so `crash_example` is never built that way. I built it on its own with `make crash_example`. It compiles with no warnings.

---

## 1. Summary

| Item | Finding |
|---|---|
| Faulting line | `crash_example.c:32`, `nums[0] = 42;` |
| Faulting instruction | `movl $0x2a,(%rax)` with `rax = 0x0`: a 4-byte **write** to address `0x0` |
| Signal | `SIGSEGV` (signal 11), exit status 139 (128 + 11) |
| Category of undefined behaviour | **NULL pointer dereference** (C11 §6.5.3.2p4: applying `*` or `[]` to a null pointer is undefined) |
| Memory involved | The pointer `nums` lives on the **stack** and is valid. The address it holds, `0x0`, is **neither stack nor heap**: it lies in the unmapped page zero. **No heap memory is involved at all**, because `malloc` is never called. |
| Root cause | `main` uses the return value of `allocate_numbers` without checking for `NULL`, which that function documents as its failure value. With `n = 0`, that failure path is taken every time. |

---

## 2. Description of the crash

The program prints its first two messages and then dies on the assignment `nums[0] = 42`. The `printf` on line 34 and the `free` on line 36 are never executed.

Whether the two messages actually appear depends on where `stdout` goes:

- **In a terminal**, `stdout` is line-buffered. Each `\n` flushes the line, so both messages appear before the crash:
  ```
  crash_example: deterministic NULL dereference (segmentation fault)
    requesting n=0
  Segmentation fault (core dumped)
  ```
- **When output is piped or redirected to a file**, `stdout` is fully buffered (4,096 bytes). The two messages are still sitting in that buffer when the process is killed, and since a signal kill does not flush `stdio` buffers, **nothing is printed**. Valgrind confirms it: at the moment of the crash it reports `still reachable: 4,096 bytes in 1 blocks`, which is exactly that unflushed `stdout` buffer.

These missing lines are not part of the bug. They are a side effect of buffering, and they show why the moment of the crash cannot be inferred from the last message printed.

Valgrind's diagnosis:

```
Invalid write of size 4
   at 0x10928A: main (crash_example.c:32)
 Address 0x0 is not stack'd, malloc'd or (recently) free'd

Process terminating with default action of signal 11 (SIGSEGV)
 Access not within mapped region at address 0x0
   at 0x10928A: main (crash_example.c:32)
```

`gdb` at the moment of the fault:

```
Program received signal SIGSEGV, Segmentation fault.
0x000055555555528a in main () at crash_example.c:32
32          nums[0] = 42;
=> movl   $0x2a,(%rax)        rax = 0x0
(gdb) p nums   -> (int *) 0x0
(gdb) p &nums  -> (int **) 0x7fffffff72f8     (on the stack, rbp-8)
(gdb) p n      -> 0
```

---

## 3. Root cause analysis: the full causal chain

The crash can be explained entirely by reading the code. No run is needed: every value involved is a compile-time constant.

**Step 1: `main`, lines 24–25.** Two locals are created in `main`'s stack frame: `nums = NULL` (at `rbp-8`) and `n = 0` (at `rbp-12`). Nothing ever changes `n` afterwards.

**Step 2: line 30, `nums = allocate_numbers(n)`.** `allocate_numbers` is called with `n = 0`. A new stack frame is created, with `arr = NULL` and `i = 0`.

**Step 3: line 9 in `allocate_numbers`, `if (n <= 0) return NULL;`.** The condition `0 <= 0` is true, so the function returns `NULL` immediately. `malloc` on line 12 is **never reached**: no heap block exists. The frame of `allocate_numbers` is destroyed, and `NULL` is copied into `nums`.

So `allocate_numbers` uses `NULL` as its "no array" result. It returns `NULL` in two cases: `n <= 0` (line 10) and `malloc` failure (line 14). The **contract** of this function is therefore: *the caller must check for `NULL` before using the result.*

**Step 4: line 32, `nums[0] = 42;` — the bug.** `main` breaks that contract: it never compares `nums` with `NULL`. `nums[0]` is defined in C as `*(nums + 0)`, so the statement means "write the `int` 42 at the address held in `nums`", that is, at address `0x0`. In the C language this is already **undefined behaviour**: dereferencing a null pointer. **This line is the bug. The segmentation fault is only its observable consequence.**

**Step 5: the CPU executes `movl $0x2a, (%rax)` with `rax = 0`.** The processor translates virtual address `0x0` through the page tables. Linux never maps the first page of a process's address space. The minimum mappable address is set by `vm.mmap_min_addr`, which is 4,096 on this machine and usually 65,536. The translation finds no valid page, so the CPU raises a **page fault**.

**Step 6: the kernel handles the page fault.** The faulting address does not belong to any memory region of the process (not the stack, not the heap, not code, not a library). The kernel concludes the access is illegal and sends **`SIGSEGV`** to the process.

**Step 7: the process terminates.** The program installs no handler for `SIGSEGV`, so the default action applies: the process ends with signal 11 (exit status 139), and no `stdio` buffer is flushed (section 2).

Summary of the chain:

```
n = 0 ──► allocate_numbers returns NULL (malloc never called)
      ──► main stores NULL in nums without checking it          <- the bug
      ──► nums[0] = 42 writes to address 0x0                     <- undefined behaviour
      ──► page 0 is not mapped -> CPU page fault
      ──► kernel sends SIGSEGV -> process killed
```

---

## 4. Why this memory access is invalid

### 4.1 Stack, heap, or both?

| Object | Where it lives | Valid? |
|---|---|---|
| The variable `nums` (8 bytes, holds `0x0`) | **Stack**, `main`'s frame, `rbp-8` | Yes. Reading it is legal: it is a local variable that is alive and initialised. |
| The variable `n` | **Stack**, `main`'s frame, `rbp-12` | Yes |
| `arr`, `i` in `allocate_numbers` | **Stack**, destroyed when the function returns | Not involved in the crash |
|
