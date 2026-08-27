# Profiling Ambience on the device

Hardware profile of `AmbienceReverb.lv2` running inside mod-host on `modpad`
(Raspberry Pi 4B, Cortex-A72 @ 1.5 GHz, JACK 48 kHz / 64 frames / 1333 µs),
26 Aug 2026. This is the record of what was measured, how, and what it means.
`lv2/README.md` § CPU says the authoritative measurement is on the device —
this is that measurement.

The short version: Ambience is the most expensive pedal on the board by ~7x,
three stages are 70% of it, and **most of that time is spent waiting on memory
rather than computing reverb**. The single biggest lever is not in the DSP, it
is in `DelayMemoryPool`.

```sh
/data/perf/amb-profile.sh            # counters + verdict, ~30 s
/data/perf/amb-profile.sh --record   # ...plus a 30 s cycle profile
```

## What it costs

Per-pedal CPU from mod-ui's `/plugin_stats/`, as a share of one core. The other
eighteen pedals sit near the ~2% floor a JACK callback costs no matter what it
does.

| pedal | % of one core |
|---|---|
| **Ambience Reverb** (`eff_2`) | **42.0** peak observed |
| Looperlative LP1 | 6.2 |
| dm-BigMuff | 3.9 |
| dm-DS1 / dm-GrainDelay / dm-SpaceEcho | 3.3 |
| 14 others | 1.6 – 2.6 |

## Where the cycles go

50,364 samples over 30 s on the live `eff_2` audio thread with the pedalboard
running normally. 93% of sampled cycles landed inside `AmbienceReverb.so`; the
rest is JACK, mod-host and the kernel wakeup path. Percentages are of the
plugin's own cycles, attributed by **inline chain** back to the calling stage in
`processBlock()` — that is what puts an inlined `BiquadState::tick` under the
absorption cascade instead of under itself.

| stage | % of plugin |
|---|---|
| Absorption GEQ — 10 biquads × 16 ch (`UniversalEngine.cpp:566`) | **32.9** |
| Serial allpass — 3 stages × 16 ch (`UniversalEngine.cpp:607`) | **20.0** |
| FDN Thiran delay read (`UniversalEngine.cpp:563`) | **17.3** |
| Micro-saturation | 6.6 |
| FWHT + sign flip | 5.2 |
| Early reflection taps | 4.0 |
| LFOs, noise + chorus | 3.1 |
| FDN write / stereo spread | 1.6 |
| LV2 wrapper (run / ctl / mix) | 1.5 |
| Saturator / output EQ / limiter | 1.4 |
| Input diffusers × 4 | 1.3 |
| AcousticMetrics (UI only) | 1.2 |
| DC blocker | 1.0 |
| RMS compressor | 0.8 |
| pre-delay, ducking, block setup, misc | 2.1 |

Ranked by source line instead of by stage:

| % | line | what it is |
|---|---|---|
| 20.3 | `DelayMemory.h:64` | `buffer[readIdx1] + frac * (buffer[readIdx2] - buffer[readIdx1])` — the two loads of the interpolated read |
| 16.2 | `BiquadFilters.h:20` | `s2 = c.b2 * x - c.a2 * y` |
| 12.9 | `DelayMemory.h:112` | `float xn = buffer[(uWrite - uId) & uMask]` — the Thiran read's single load |
| 7.7 | `BiquadFilters.h:19` | `s1 = c.b1 * x - c.a1 * y + s2` |
| 6.7 | `BiquadFilters.h:18` | `float y = c.b0 * x + s1` |

**Three of the top five are a load from a delay buffer.** That is the thread to
pull, and it leads somewhere other than the DSP.

## The real finding: the buffers collide in cache

A72 PMU counters on the same thread, 15 s, divided down to one audio sample at
48 kHz:

| per sample | value |
|---|---|
| cycles | 7,518 (of 31,250 available) |
| instructions | 6,636 |
| IPC | **0.88** |
| L1D loads | 2,485 |
| L1D misses | **132.6** (5.34% of loads) |

IPC of 0.88 on a core that retires three per cycle is the first sign this code
is waiting, not working. The miss count is the loud one.

Ambience streams through ~70 delay-line positions per sample — pre-delay, ER,
4 diffusers, 16 FDN, 48 allpass — each advancing one float. A cache line holds
16 floats, so pure streaming compels about **8.8 misses per sample**. The
hardware reports **132.6**. Fifteen of every sixteen misses are avoidable.

### Why

`DelayMemoryPool::requestMemory()` (`DelayMemory.h:19-31`) rounds every request
up to the next power of two so the wrap can be a mask instead of a modulo, and
the allocations are packed back-to-back inside one `std::vector`. The
consequence is that **every buffer starts at a large power-of-two offset from
every other one**: the 16 FDN lines are exactly 128 KB apart, the 48 allpass
lines exactly 8 KB apart.

A 32 KB, 4-way L1D indexes on address bits [12:6]. Buffers spaced an exact
multiple of 8 KB apart land on the *same cache set* at the same read offset.
Forty-eight allpass lines competing for four ways is not a cache, it is a queue.

The power-of-two mask is a good idea. Packing the results contiguously is what
turns it into a pathology, and that part is free to change.

### The working set doesn't help

What `prepare()` (`UniversalEngine.cpp:70-102`) actually allocates:

| buffer | allocated | longest delay it can hold | could be |
|---|---|---|---|
| FDN × 16 | 2,048 KB | 683 ms | 1,024 KB |
| Serial allpass × 48 | 768 KB | 42.7 ms | 384 KB |
| ER delay | 256 KB | 1365 ms | 64 KB † |
| Pre-delay | 128 KB | 683 ms | 128 KB |
| Input diffusers × 4 | 64 KB | 21.3 ms | 64 KB |
| **total pool** | **3,264 KB** | | **~1,664 KB** |

The A72's L2 is **1 MB shared across all four cores**, with eighteen other
pedals using it too. The FDN lines are sized at 32,768 samples, but the longest
delay they can ever be asked for is `maxDelayMs = 50 + 2.0 * 75 = 200 ms`
(`UniversalEngine.cpp:179`) plus ~216 samples of modulation — under 9,900
samples. They are 4x larger than they need to be, and 16,384 is still a power
of two.

† the ER bound needs verifying against `erSizeScale` (`UniversalEngine.cpp:344`)
before shrinking that one.

## Why the number moves

**Reproduced and diagnosed, 27 Aug 2026.** A power cycle brought the board up
slow; `/data/perf/amb-profile.sh` was run 41 s into that boot.

| per sample | good boot | slow boot | ratio |
|---|---|---|---|
| instructions | 6,636 | 6,642 | **1.00x** |
| cycles | 7,518 | 13,993 | **1.86x** |
| IPC | 0.88 | 0.47 | |
| L1D misses | 132.6 | 141.4 | 1.07x |

Identical work, 86% more cycles — and **the L1 data cache is not the culprit**;
its miss count barely moved. Only Ambience is affected: LP1, the dm-* pedals
and everything else came in within noise of their own baselines, so this is not
a system-wide or process-wide effect. It is this plugin's allocation.

The A72 raw counters say where the time goes (`perf stat -e r05,r16,r17,r19`):

| event | per sample |
|---|---|
| `L1D_TLB_REFILL` (r05) | **112.6** |
| `L2D_CACHE_REFILL` (r17) — L2 miss to DRAM | **79.6** |
| `L2D_CACHE` (r16) | 377 |
| `BUS_ACCESS` (r19) | 588 |

Transparent hugepages are ruled out — the kernel has none
(`/sys/kernel/mm/transparent_hugepage/` absent, `AnonHugePages: 0`).

### The aggressor is the pool; the victim is the GEQ

Attributing the slow boot's cycles by stage, next to the good boot:

| stage | good | slow | absolute change |
|---|---|---|---|
| Absorption GEQ | 32.9% | **47.1%** | **2.4x** |
| Serial allpass | 20.0% | 14.0% | 1.15x |
| FDN Thiran read | 17.3% | 12.1% | 1.15x |

The delay-line reads barely moved. **The GEQ more than doubled** — and
attributing the L2-refill event by stage says **41.3% of all DRAM traffic is in
the GEQ**, with the allpass and Thiran reads next at 19.4% and 12.1%.

That is the whole story, and it is not the one the first pass guessed:

- `currentAbsorptionCoeffsS2` is 16 x 10 x `BiquadCoeffs` = **3,200 bytes**,
  swept in full **every sample** — about 50 cache lines per sample.
- It is far too small to miss to DRAM on its own. It misses because the
  3.19 MB delay pool streaming past evicts it between samples.
- In the good boot those re-fetches are served by the **1 MB L2**. In the slow
  boot they go to **DRAM**. Same L1 miss count, ~120 cycles of latency instead
  of ~20.
- L1 and L2 are physically indexed, so which of the two happens depends on the
  physical pages the pool got — and that is decided once, in `prepare()`, when
  mod-host loads the pedalboard.

The dTLB looks alarming at 112.6 refills per sample — Ambience touches ~70
delay-line positions per sample, each in a different 4 KB page, against a
**32-entry L1 dTLB** — but it turned out to be a red herring. It measures
**112.4 with the fix applied and the plugin running at half the cycles**, so it
is inherent to the access pattern and is not what moves. **`L2D_CACHE_REFILL`
is the signal**: 79.7 per sample slow, 18.6 fixed.

So the causal chain is: power-of-two strides -> L2 set conflicts -> the GEQ
coefficient table gets evicted every sample -> its refills come from DRAM
instead of L2 -> IPC collapses.

### What could not be measured

Locating the pool's pages directly would settle placement beyond argument, but
`/proc/<pid>/pagemap` is only useful with the pool's virtual address, and the
pool is **not a standalone mapping** — glibc served the 3.19 MB request out of
mod-host's 266 MB arena rather than its own `mmap`. Getting the address needs a
one-line log from `prepare()` in an instrumented build; see below.

## Measuring it again

`perf` is not in the image and there is no `gdb` or `strace` either, so it was
built from the image's own kernel tree and installed to **`/data/perf`** — a
separate ext4 partition mounted `rw,noatime` with no `noexec`, so **it survives
a power cycle**. Nothing was added to the rootfs and no A/B upgrade was needed.

```sh
/data/perf/amb-profile.sh
```

The script resolves which `eff_<n>` is Ambience from `/plugin_stats/` rather
than assuming `eff_2` (the number is a mod-host instance id and a re-saved
pedalboard can renumber it), picks the busier of the plugin's two threads, and
prints machine state, CPU share, PMU counters and a verdict against the
good-boot baseline. It only reads `/proc`, the stats endpoint and the PMU — it
never touches the audio graph.

**Instructions per sample is fixed by the code and the parameters. Cycles per
sample is not.** That is the whole discriminator:

| observation | conclusion |
|---|---|
| same instructions, more cycles, more misses | memory placement — the fixes below are the answer |
| more instructions | the plugin is doing more work; a parameter or preset differs |

Good-boot baseline, 15 s window: **4.78 G instructions / 5.41 G cycles /
95.5 M cache-misses**, IPC 0.88, ~25% of a core. Reproduced across three runs
to within 0.3% on instructions.

Saved profiles on the device: `amb-idle.data` (good state, cycles),
`amb-bad.data` (slow boot, cycles), `amb-tlb.data` (slow boot, `L1D_TLB_REFILL`)
and `amb-l2.data` (slow boot, `L2D_CACHE_REFILL`) — all in `/data/perf`.

### Rebuilding perf

Enable `BR2_PACKAGE_ELFUTILS` and `BR2_PACKAGE_LINUX_TOOLS_PERF` in
`LoopPad_Jack2/Buildroot/build/output/.config` **only** — never in `configs/`,
or the shipping image grows a profiler. A ready-made copy of that config is
saved at `build/output/.config.withperf`. Then build *targeted*, never a bare
`make`, so Buildroot does not regenerate the rootfs images:

```sh
cd LoopPad_Jack2/Buildroot/build/buildroot
make O=../output elfutils
make O=../output linux-tools
# then ship target/usr/bin/perf plus libelf.so.1 / libdw.so.1 to /data/perf
```

Two host gotchas: a leading empty entry in `LD_LIBRARY_PATH` makes Buildroot
refuse to start, and the tree's `dl` symlink is dangling, so pass
`BR2_DL_DIR=<somewhere writable>` explicitly.

### Symbolizing

`lv2/build.sh --debug-info` appends `-g` and nothing else. Verified: the
`.text` of the `-g` build and the shipping build have the same MD5, so a
profile taken against it is a profile of the code that ships. Without it perf
collapses the entire engine into one inlined `run` symbol.

The device `perf` cannot resolve source lines (no `addr2line` on the image) and
the host `perf` on this workstation is a stub, so the route that works is:
weight samples by cycle period, fold into a `symbol+offset` histogram via
`perf script`, and resolve with
`aarch64-buildroot-linux-gnu-addr2line -i -p -f -C`. The `-i` inline chain is
what makes the per-stage attribution possible at all.

## What to change

Ranked by value per unit of risk. Savings are estimates derived from the
profile, not measurements — each is one 15 s `perf stat` away from being real.

| change | where | est. saving | audio risk |
|---|---|---|---|
| **Right-size the buffers.** FDN 32768 -> 16384, allpass 2048 -> 1024. Halves the pool to ~1.7 MB, so far more of it sits in the 1 MB L2 alongside the GEQ coefficients instead of evicting them. Now the *primary* fix: the variance is an L2-capacity effect. | `UniversalEngine.cpp:70-75` | 10-30% | **none** if the bound holds |
| **Pad between allocations.** Insert a rotating `(i mod 8) * 64`-byte gap between buffers. Sizes stay powers of two and the mask arithmetic is relative to each buffer's own pointer, so nothing else changes. Attacks the baseline L1 aliasing. | `DelayMemory.h:20-31` | 10-25% | **none** — bit-identical output |
| **Log the pool's address and page colours in `prepare()`.** Not an optimisation — it makes the next slow boot self-diagnosing instead of requiring this whole investigation again. | `UniversalEngine.cpp:77` | — | **none** — diagnostic |
| **NEON the absorption GEQ across channels.** The 16 channels are independent; run 4 per vector through the 10-stage cascade. Cuts the instruction count on the stage that takes the most damage. | `BiquadFilters.h:17-22` | 15-22% | low — fp ordering only |
| **Fewer streaming positions.** The 48 serial-allpass lines are the largest single source of dTLB pressure. Dropping to 2 stages removes 16 of them. | `UniversalEngine.cpp:607-614` | 5-8% | medium — late-field density |
| **Decimate AcousticMetrics.** Three integer `%` per sample feeding a display readout; every 8th sample is plenty. | `AcousticMetrics.cpp:39-79` | ~1% | none — UI only |
| **GEQ 10 bands -> 6.** The largest single lever, and the only one that changes the reverb's voice. | `DSPConstants.h:29` | ~13% | **high** — changes the tone |

The first two change no arithmetic whatsoever and are confined to about sixty
lines of allocator code. They are the place to start, and they are aimed
squarely at the measured cause: a pool that halves in size and stops
self-colliding has far less ability to evict the GEQ coefficients, and far less
to gain or lose from where the kernel happens to put it.

**How to prove it.** The states cannot be A/B'd inside one boot — stopping the
plugin reallocates and it comes back fast — so the protocol is: deploy the
fix, then power-cycle several times and run `amb-profile.sh` on each boot. The
fix is confirmed when the slow boot stops appearing, not when one boot is fast.

## Device state left behind

- `/data/perf/` — `perf` 6.12.94, `libelf`/`libdw`, `amb-profile.sh`,
  `amb-idle.data` (good-state profile), `graph-before.txt` (the JACK graph as
  found, for comparison).
- `/data/mod/lv2/AmbienceReverb.lv2/` carries the `--debug-info` bundle
  (744 KB rather than 83 KB, **identical machine code**), so a profile can be
  taken after the next power cycle without a rebuild.

To put the normal bundle back:

```sh
lv2/build.sh --deploy root@modpad
ssh root@modpad '/etc/init.d/S65modhost restart && /etc/init.d/S66modui restart'
```

The `jack_metro` client used for the signal test was killed and its connections
removed; `jack_lsp -c` matches `graph-before.txt` exactly.

## The fix, measured

`DelayMemoryPool` now rotates each block's start by `(allocIndex % 16)` cache
lines before handing it out (`DelayMemory.h`). Block sizes stay powers of two
and each mask is still relative to that buffer's own pointer, so **no wrap
computation and no DSP arithmetic changes at all**. The pool carries a bounded
`padHeadroom()` so `requestMemory()` can never start returning `nullptr` on
account of the padding — `prepare()` does not null-check its result.

Measured on the device in a *single* slow boot, so this is a true A/B and not a
comparison across two boots that might have differed for other reasons:

| per sample | unpadded, slow boot | **padded** | vs slow |
|---|---|---|---|
| cycles | 14,021 | **7,075** | **0.50x** |
| instructions | 6,650 | 6,649 | 1.00x |
| IPC | 0.47 | **0.94** | 2.0x |
| L1D misses | 142.3 | **81.1** | 0.57x |
| `L2D_CACHE_REFILL` (DRAM) | 79.7 | **18.6** | **0.23x** |
| `BUS_ACCESS` | 589.0 | **94.3** | **0.16x** |
| `L1D_TLB_REFILL` | 112.6 | 112.4 | 1.00x |

mod-ui reports Ambience at **22.7% of a core**, down from 44.8% in the same
boot — and below the 25-26.6% of the *lucky* fragmented state, because the
padding beats what fragmentation achieved by accident. Zero xruns. Every other
pedal unchanged.

The stage mix confirms the mechanism rather than merely the outcome. The
absorption GEQ was the victim, and it is the stage that recovers:

| stage | unpadded slow | unpadded fast | **padded** |
|---|---|---|---|
| Absorption GEQ | 47.1% | 32.9% | **20.1%** |
| Serial allpass | 14.0% | 20.0% | 22.2% |
| FDN Thiran read | 12.1% | 17.3% | 21.6% |

DRAM traffic fell 6.2x and the TLB refill rate did not move at all, which is
what confirms the cause was L2 set conflicts and not the TLB.

### Verified unchanged

`lv2/tools/smoke_test.sh` **PASS** with the padding: all 7 algorithms produce
output, no NaN/Inf across a 41-port control sweep, out-of-range and NaN control
values clamp cleanly, block sizes 1..256 clean, preset recall works.

Run against the unpadded and padded sources in turn, the two smoke-test reports
are **byte-identical** — every per-algorithm peak and RMS matches to the printed
digit. The change is an allocator change and nothing else.

Note: `lv2/tools/smoke_test.sh` still hardcodes `~/Sources/LoopPad_Jack` and
needs `LOOPPAD_ROOT=/home/bob/Sources/LoopPad_Jack2` — the same stale-path bug
that `build.sh` fixed in commit b9acd0d. Worth fixing there too.

### Still on the table

The padding was the zero-risk half. Right-sizing the buffers (FDN 32768 ->
16384, allpass 2048 -> 1024, pool 3.19 MB -> ~1.7 MB) attacks L2 *capacity*
rather than aliasing and is still unclaimed, as is NEON-ing the GEQ. With DRAM
traffic already down 6.2x the remaining headroom is smaller than it was, so
measure before assuming.
