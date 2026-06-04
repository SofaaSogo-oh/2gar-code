# Enumeration of binary Convex Structures

This program enumerates all binary convex structures (also called binary convexities) on a finite set of small cardinality. It can output the found convex structures in several formats.

The implementation is written in C++23, uses OpenMP for parallelisation and Boost.Program_options for command‑line parsing. All subsets are represented as 64‑bit masks for efficiency.

## Features
* Enumerate all binary convexities on a set of size dim ($0\dots6$, but 6 is extremely slow).
* Resumable computation - save progress and continue later.
* Various output modes (index, set, table, comma‑separated).
* Configurable timer unit (seconds, milliseconds, etc.).

## Requirements
* C++23 compiler (Clang/GCC recommended)
* CMake 3.10 or higher
* Boost (only program_options component)
* OpenMP (e.g. libomp for Clang, libgomp for GCC)

## Command‑line arguments

| Option        | Type     | Default   | Description                                                                    |
| --------------| -------- | --------- | ------------------------------------------------------------------------------ |
| `--help`      | flag     | -         | produce help message                                                           |
| `--dim`       | uint64_t | required  | dimension of the space                                                         |
| `--start`     | uint64_t | 0         | starting index for convex sets                                                 |
| `--success`   | uint64_t | 0         | initial count of successful convex sets                                        |
| `--output`    | string   | "none"    | output mode (see below)                                                        |
| `--block`     | uint64_t | 100000000 | block size for checkpointing                                                   | 
| `--timermode` | string   | "seconds" | Unit for time output: "seconds", "milliseconds", "microseconds", "nanoseconds" |

## Output modes for `--output`

| Mode              | Description                                                                          |
| ----------------- | ------------------------------------------------------------------------------------ |
|`none`             | No output file (`convex_sets.txt`) is created.                                       |
|`index`            | One integer (family index) per line.                                                 |
|`set`              | One family per line printed as `{{...},{...},...}`                                   |
|`table`            | Index + family in a two-column table.                                                |
|`delim`            | All indices in one line, separated by commas.                                        |

All outputs go to `convex_sets.txt` (except `none`).

## Checkpointing and resuming

The program writes a line to `dest.txt` after every block:

``
<processed_families> <found_so_far> <elapsed_time>
``

To resume an interrupted run, use the last values as `--start` and `--success`.

## Performance notes
* For $|X|\leq4$ the enumeration finishes almost instantly (well under 1 second).
* For $|X|=5$ the total number of families is $2^{31}$. Running is about ~1.5 minutes on a modern multi‑core CPU.
* For $|X|=6$ a full enumeration would take many years with the current brute‑force approach.

## Known issues/limitations
* $|X|>6$ is not supported (the code would use 64‑bit masks, but runtime is prohibitive).
* The program does not check for overflow of `total_convex` when $|X|$ is large - but for $|X|\leq 6$ this is fine.