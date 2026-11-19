# Text-buffer scale benchmark — 2026-11-19

Raw output of `./build/bench/vellum_bench_text_buffer`, three consecutive runs on
the development host. This is the recorded evidence for the Phase 3 scale exit
criteria (100 MB file open and editing near the end of a 1M-line file) against
the targets in [`../ARCHITECTURE.md`](../ARCHITECTURE.md) §10.

## How to reproduce

```sh
cmake --preset bench
cmake --build --preset bench
./build/bench/vellum_bench_text_buffer
```

## Fixed configuration (every run)

| Setting | Value |
| --- | --- |
| Build | bench (`RelWithDebInfo`, no sanitizers), warnings as errors |
| Host | Linux, Clang, SDL 3.4.20 (only used for monotonic timers) |
| 100 MB workload | 104,857,600 bytes = 1,048,576 lines × (99 × `a` + `\n`) |
| 1M-line workload | 10,000,000 bytes = 1,000,000 lines × (9 × `b` + `\n`) |
| End-typing workload | 2,000 single-byte appends at the end of the document, each followed by a line-index insert notification |
| Random-edit workload | 10,000 single-byte inserts/deletes at random offsets, each followed by the matching line-index notification (xorshift seed `0x5EED1234`) |
| Timing | `SDL_GetPerformanceCounter()` / `SDL_GetPerformanceFrequency()`; percentiles from the full sample set |
| Validation | `vellum_buffer_validate()` and `vellum_line_index_validate()` after the timed section |

The 100 MB source is generated in memory and copied by `vellum_buffer_create()`;
the read-only `mmap` fast path described in the architecture is not implemented
yet, so the open number includes one 100 MB `memcpy`. The source allocation is
still live during the timed section, so peak memory is about 2× the file size.

## Results

### 100 MB file open (ms)

| Run | `buffer_create` | `line_index_rebuild` | total |
| --- | --- | --- | --- |
| 1 | 17.644 | 147.917 | 165.562 |
| 2 | 17.886 | 150.256 | 168.143 |
| 3 | 16.662 | 148.110 | 164.773 |

The buffer holds one piece and 1,048,577 line starts (the trailing newline
produces one final empty line).

### 1M-line end typing (microseconds per edit)

| Run | mean | p50 | p95 | p99 | max |
| --- | --- | --- | --- | --- | --- |
| 1 | 13.558 | 13.760 | 25.709 | 39.484 | 74.265 |
| 2 | 12.908 | 12.006 | 23.979 | 44.770 | 70.923 |
| 3 | 13.456 | 12.153 | 27.102 | 58.704 | 97.731 |

### 1M-line random edits (microseconds per edit)

| Run | mean | p50 | p95 | p99 | max |
| --- | --- | --- | --- | --- | --- |
| 1 | 1503.689 | 1486.979 | 2902.795 | 3310.113 | 5404.167 |
| 2 | 1489.450 | 1477.369 | 2879.394 | 3244.267 | 5126.218 |
| 3 | 1470.295 | 1456.559 | 2851.331 | 3209.101 | 5214.105 |

## Verdict against §10 targets

| Target | Measured | Verdict |
| --- | --- | --- |
| Open 100 MB file < 1 s | 164.8–168.1 ms total | **Pass** |
| Typing in 1M-line file < 16 ms p95 | 24.0–27.1 µs p95 | **Pass** (≈600× margin) |
| Edit operation, typical line < 1 ms p99 | end typing 39.5–58.7 µs p99 | **Pass** for the end-of-file case |

Both Phase 3 scale exit criteria are met with wide margin. The edge append
touches the tail of the piece table and the end of the line-index array, so it
stays at a few tens of microseconds even on a 1M-line document.

## Known limitation: random middle edits

The random-edit workload is an additional stress case, not a Phase 3 exit
criterion. Its p99 of roughly 3.2–3.3 ms exceeds the general §10 "edit
operation < 1 ms p99" target. The cost is dominated by the flat sorted array of
line starts: an insert or delete that adds/removes a line in the middle of a
1M-line document shifts up to 8 MB of `size_t` entries. End-typing and
near-caret editing are unaffected because they operate at the array tail.

The two-level line index described in the architecture is the planned remedy
and is explicitly deferred; this benchmark records the current flat-array
behaviour rather than claiming the target is met for arbitrary random edits.
No optimisation was attempted in this increment.

## Notes

- These are CPU times for the buffer and line index only. Rendering, layout,
  and the command/undo layer are not part of this benchmark; the frame budget
  evidence is in [`2026-11-19-text-frame.md`](2026-11-19-text-frame.md).
- The random-edit distribution is bimodal (inserts that add a line and deletes
  that remove one pay the array shift; edits that do not cross a terminator do
  not), which is why the p95 is about twice the p50.
