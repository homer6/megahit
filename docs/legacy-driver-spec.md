# Legacy Python Driver — Port Specification

This is the functional contract of the soon-to-be-deleted Python orchestrator
`src/megahit` (1045 lines). The new Seastar / C++23 orchestrator must replicate
the observable behavior described here: the same `megahit_core` subcommands with
the same argument lists, the same pipeline order, the same on-disk layout, and
the same checkpoint/resume semantics.

Everything below is grounded in `src/megahit`; line numbers and function names
refer to that file unless another path is given. C++ flag names were
cross-checked against `src/main_sdbg_build.cpp`, `src/main_assemble.cpp`,
`src/main_local_assemble.cpp`, `src/main_buildlib.cpp`, and
`src/sequence/io/sequence_lib.cpp`.

`megahit_core` is the multi-tool C++ binary; the driver dispatches by `argv[1]`
(the subcommand). Three CPU-variant copies exist; see §7.

---

## 1. Pipeline sequence

`main()` (lines 977–1041) runs this exact order. Functions marked **CP** are
decorated with `@check_point` (the `Checkpoint` instance `check_point`,
lines 250–284). The checkpoint *number* is assigned in call order at runtime
(see §5), so the decorated call sequence below is the canonical numbering.

| Phase | Call (line) | CP? | CP # |
|------|-------------|-----|------|
| setup | `check_bin()` (983) | no | — |
| setup | `parse_option(argv[1:])` (984) | no | — |
| setup | `setup_output_dir()` (985) | no | — |
| setup | `setup_logger()` (986) | no | — |
| setup | `check_and_correct_option()` (988) | no | — |
| setup | `check_reads()` (990) | no | — |
| setup | `cpu_dispatch()` (991) | no | — |
| setup | `opt.dump()` (992) → `options.json` | no | — |
| lib | `create_library_file()` (994) | **CP** | 0 |
| lib | `build_library()` (995) | **CP** | 1 |
| — | `set_max_k_by_lib()` (997) — may shrink k-list | no | — |
| k_min | `build_first_graph()` (1004) → dispatches (see below) | mixed | 2 (and 3 if 2-pass) |
| k_min | `assemble(opt.k_min)` (1005) | **CP** | next |
| loop | per next k in `k_list[1:]`: see below | **CP**×N | … |
| final | `merge_final(opt.k_max)` (1024) **or** `merge_final(et.kmer_size)` on `EarlyTerminate` (1027) | **CP** | last |

### `build_first_graph()` dispatch (lines 797–811)
Builds the `common_option` list (see §2) then:
- **2-pass** (default, `kmin_1pass` false): `count_mink(common_option)` (**CP**) then `build_graph(opt.k_min, 0)` (**CP**). → two checkpoints.
- **1-pass** (`kmin_1pass` true): `build_first_graph_1pass(common_option)` (**CP**). → one checkpoint.

This conditional changes the checkpoint count between CP 2 and the rest — a
resume of a 1-pass run is NOT checkpoint-compatible with a 2-pass run.

### Per-k loop (lines 1010–1024)
`cur_k = k_min`, `next_k_idx = 0`. While `cur_k < k_max`:
1. `next_k_idx += 1`; `next_k = k_list[next_k_idx]`; `k_step = next_k - cur_k`.
2. if **not** `no_local`: `local_assemble(cur_k, next_k)` (**CP**).
3. `iterate(cur_k, k_step)` (**CP**).
4. `build_graph(next_k, cur_k)` (**CP**) — may raise `EarlyTerminate(cur_k)`.
5. `assemble(next_k)` (**CP**).
6. `cur_k = next_k`.

So the per-iteration order is **local_assemble → iterate → build_graph →
assemble**. Note `local_assemble` is conditional (`no_local`), and skipping it
shifts checkpoint numbering for the whole tail of the run — a `--no-local` run is
not checkpoint-compatible with a default run.

### `EarlyTerminate` (lines 134–137, 846–847, 1026–1027)
`build_graph` raises `EarlyTerminate(kmer_from)` when the next graph would have
**no input data** (`file_size == 0 and kmer_from != 0`). `main()` catches it and
calls `merge_final(et.kmer_size)` using the last successful k instead of `k_max`.

### Teardown (lines 1029–1037)
- if not `keep_tmp_files`: `shutil.rmtree(opt.temp_dir)`.
- touch `<out_dir>/done` (empty sentinel file).
- if `keep_tmp_files` is false **and** `test_mode`: `shutil.rmtree(opt.out_dir)`.
- log `ALL DONE. Time elapsed: ...`.

---

## 2. Per-stage subcommand + full argument list

All `<...>` are substituted string values. `graph_prefix(k)` and
`contig_prefix(k)` are defined in §6. Order of flags as constructed in code.

### 2.0 `create_library_file()` — no subcommand (lines 675–705)
Writes the text file `opt.read_lib_path` (= `<temp_dir>/reads.lib`). No process
spawned. Format documented in §7.

### 2.1 `build_library()` → **`buildlib`** (lines 708–752)
```
megahit_core buildlib <reads.lib> <reads.lib>
```
Both args are `opt.read_lib_path` (input lib descriptor; output binary prefix —
same path, different extensions). Before running, the driver creates named FIFOs
and launches decompressor processes feeding them (see §7, `inpipe_cmd`). Run with
`verbose=True`. After the binary build, every decompressor pipe is `wait()`ed;
non-zero exit aborts. FIFOs are removed in a `finally`. (`main_buildlib.cpp`
usage: `buildlib <read_lib_file> <out_prefix>`.)

### 2.2 `count_mink(option)` → **`count`** (2-pass k_min, lines 791–794)
```
megahit_core count <common_option...>
```
where `common_option` is built in `build_first_graph()` (lines 798–804):

| flag | value | source |
|------|-------|--------|
| `-k` | `k_min` | `opt.k_min` |
| `-m` | min multiplicity | `opt.min_count` |
| `--host_mem` | bytes | `opt.host_mem` (§4) |
| `--mem_flag` | 0/1/other | `opt.mem_flag` |
| `--output_prefix` | `graph_prefix(k_min)` | |
| `--num_cpu_threads` | thread count | `opt.num_cpu_threads` |
| `--read_lib_file` | `reads.lib` | `opt.read_lib_path` |

(C++ `count` accepts `kmer_k`/`-k`, `min_kmer_frequency`/`-m`, `host_mem`,
`num_cpu_threads`, `read_lib_file`, `output_prefix`, `mem_flag`.)

### 2.3 `build_first_graph_1pass(option)` → **`read2sdbg`** (1-pass k_min, lines 779–788)
```
megahit_core read2sdbg <common_option...> [--need_mercy]
```
Same `common_option` table as 2.2. Appends `--need_mercy` iff **not**
`opt.no_mercy` (line 782–783). After run, if not `keep_tmp_files`,
`remove_temp_after_build(k_min)` (§6).

### 2.4 `build_graph(kmer_k, kmer_from)` → **`seq2sdbg`** (CP, lines 813–855)
Base `build_comm_opt` (lines 815–820):

| flag | value |
|------|-------|
| `--host_mem` | `opt.host_mem` |
| `--mem_flag` | `opt.mem_flag` |
| `--output_prefix` | `graph_prefix(kmer_k)` |
| `--num_cpu_threads` | `opt.num_cpu_threads` |
| `-k` | `kmer_k` |
| `--kmer_from` | `kmer_from` (previous k; `0` for the first k_min build) |

Then conditional inputs (a `file_size` accumulator tracks bytes of real input):

| condition | flags appended | counts toward `file_size`? |
|-----------|----------------|----------------------------|
| `graph_prefix(kmer_k).edges.0` exists (lines 826–832) | `--input_prefix <graph_prefix(kmer_k)>` | yes — sums sizes of all `.edges.<tid>` for `tid=0,1,…` until missing |
| `contig_prefix(kmer_from).addi.fa` exists (834–836) | `--addi_contig <…>.addi.fa` | yes |
| `contig_prefix(kmer_from).local.fa` exists (838–840) | `--local_contig <…>.local.fa` | yes |
| `contig_prefix(kmer_from).contigs.fa` exists (842–844) | `--contig <…>.contigs.fa --bubble <…>.bubble_seq.fa` | **no** |

- If `file_size == 0 and kmer_from != 0` → raise `EarlyTerminate(kmer_from)` (846–847).
- Append `--need_mercy` iff **not** `opt.no_mercy` **and** `kmer_k == opt.k_min` (849–850).
- After run, if not `keep_tmp_files`: `remove_temp_after_build(kmer_k)` (854–855).

(C++ `seq2sdbg` flags confirmed in `main_sdbg_build.cpp:158–224`: `host_mem`,
`kmer_size`/`-k`, `kmer_from`, `num_cpu_threads`/`-t`, `contig`, `bubble`,
`addi_contig`, `local_contig`, `input_prefix`, `output_prefix`/`-o`,
`need_mercy`, `mem_flag`. Requires at least one of input_prefix/contig/addi_contig.)

### 2.5 `assemble(cur_k)` → **`assemble`** (CP, lines 873–911)
Compute `min_standalone` first (lines 875–877):
```
min_standalone = max( min(k_max*3 - 1, int(min_contig_len*1.5)), min_contig_len )
if max_tip_len >= 0:
    min_standalone = max( max_tip_len + k_max - 1, min_contig_len )
```
Base command (lines 879–892):

| flag | value |
|------|-------|
| `-s` | `graph_prefix(cur_k)` (SdBG prefix) |
| `-o` | `contig_prefix(cur_k)` (output prefix) |
| `-t` | `opt.num_cpu_threads` |
| `--min_standalone` | computed above |
| `--prune_level` | `opt.prune_level` |
| `--merge_len` | `int(opt.merge_len)` |
| `--merge_similar` | `opt.merge_similar` |
| `--cleaning_rounds` | `opt.cleaning_rounds` |
| `--disconnect_ratio` | `opt.disconnect_ratio` |
| `--low_local_ratio` | `opt.low_local_ratio` |
| `--cleaning_rounds` | `opt.cleaning_rounds` (emitted twice — duplicate; last wins) |
| `--min_depth` | `opt.prune_depth` |
| `--bubble_level` | `opt.bubble_level` |

Conditional tail:

| condition | flags appended |
|-----------|----------------|
| `max_tip_len == -1 and cur_k*3 - 1 > min_contig_len*1.5` (894–895) | `--max_tip_len <max(1, min_contig_len*1.5 + 1 - cur_k)>` |
| else (896–897) | `--max_tip_len <opt.max_tip_len>` |
| `cur_k < opt.k_max` (899–900) | `--careful_bubble` |
| `cur_k == opt.k_max` (902–903) | `--is_final_round` |
| `opt.no_local` (905–906) | `--output_standalone` |

Note the `--max_tip_len` value can be a Python float string (e.g. `"180.5"`)
because `min_contig_len*1.5` is float and `max(1, …)` keeps the float; the port
should reproduce whatever the C++ parser accepts (it reads an int field, so the
new orchestrator should pass an integer — verify against `main_assemble.cpp`,
which declares `max_tip_len` as int). After run, if not `keep_tmp_files` **and**
`cur_k != k_max`: `remove_temp_after_assemble(cur_k)` (910–911).

(C++ `assemble` flags confirmed `main_assemble.cpp`: `-s`, `-o`, `-t`,
`min_standalone`, `prune_level`, `merge_len`, `merge_similar`,
`cleaning_rounds`, `disconnect_ratio`, `low_local_ratio`, `min_depth`,
`bubble_level`, `max_tip_len`, `is_final_round`, `output_standalone`,
`careful_bubble`. Outputs `<out>.contigs.fa`, `<out>.bubble_seq.fa`,
`<out>.addi.fa`, and on final round `<out>.final.contigs.fa`.)

### 2.6 `iterate(cur_k, step)` → **`iterate`** (CP, lines 858–870)
`next_k = cur_k + step`.
```
megahit_core iterate \
  -c <contig_prefix(cur_k)>.contigs.fa \
  -b <contig_prefix(cur_k)>.bubble_seq.fa \
  -t <num_cpu_threads> \
  -k <cur_k> \
  -s <step> \
  -o <graph_prefix(next_k)> \
  -r <reads.lib>.bin
```
`-r` points at the binary library produced by `buildlib` (suffix `.bin` on
`opt.read_lib_path`). Output is the iterative-edges set under `graph_prefix(next_k)`.

### 2.7 `local_assemble(cur_k, kmer_to)` → **`local`** (CP, lines 914–922)
```
megahit_core local \
  -c <contig_prefix(cur_k)>.contigs.fa \
  -l <reads.lib> \
  -t <num_cpu_threads> \
  -o <contig_prefix(cur_k)>.local.fa \
  --kmax <kmer_to>
```
`-l` is the lib *prefix* (`opt.read_lib_path`, no extension). (C++ `local` flags:
`contig_file`/`-c`, `lib_file_prefix`/`-l`, `kmin`, `kmax`, `step`, `seed_kmer`,
`min_contig_len`, `min_mapping_len`, `sparsity`, `similarity`,
`num_threads`/`-t`, `output_file`/`-o` — `main_local_assemble.cpp:37–51`. The
driver passes only `-c -l -t -o --kmax`; everything else uses C++ defaults.)

### 2.8 `merge_final(final_k)` → shell pipe + **`filterbylen`** (CP, lines 925–944)
Output path: `<out_dir>/final.contigs.fa`, or `<out_dir>/<out_prefix>.contigs.fa`
if `out_prefix` set (928–930). Runs (via `shell=True`):
```
cat <contig_dir>/*.final.contigs.fa <contig_prefix(final_k)>.contigs.fa \
  | megahit_core filterbylen <min_contig_len>   > <final_contig_name>
```
stdout → final file; stderr captured and logged. Non-zero exit aborts. The
`*.final.contigs.fa` glob picks up the per-k final contigs emitted by each
non-final `assemble` round, plus the final round's plain `.contigs.fa`.

---

## 3. k-list rules

Parsed/validated in `parse_option()` (367–371, 355–366) and
`check_and_correct_option()` (494–576). `software_info.max_k_allowed` is obtained
at runtime from `megahit_core kmax` (= `kMaxK = 255`, `src/sdbg/sdbg_def.h`).

### Defaults (`Options.__init__`, lines 167–172)
- `k_min = 21`, `k_max = 141`, `k_step = 10`.
- `k_list = [21, 29, 39, 59, 79, 99, 119, 141]` (default explicit list).
- `auto_k = True`, `set_list_by_min_max_step = False`.

### Option interactions
- `--k-list a,b,…`: parsed to ints, **sorted ascending**, `auto_k=False`,
  `set_list_by_min_max_step=False` (367–371).
- `--k-min` / `--k-max` / `--k-step`: each sets `set_list_by_min_max_step=True`
  and `auto_k=False` (355–366). `--k-step` must be **even** (516–517).
- `--presets` (499–513) forces `auto_k=True` and overrides:
  - `meta-sensitive`: `min_count=1`, `k_list=[21,29,…,129,141]`, list mode.
  - `meta-large`: `min_count=1`, `k_min=27,k_max=127,k_step=10`, min/max/step mode.

### Building the list when `set_list_by_min_max_step` (515–526)
```
k = k_min; k_list = []
while k < k_max: k_list.append(k); k += k_step
k_list.append(k_max)
```

### Validation (528–542), all raise `Usage` on failure
1. `k_list` non-empty.
2. `k_list[0] >= 15` and `k_list[-1] <= max_k_allowed (255)`.
3. every k is **odd** (`k % 2 == 0` → error).
4. adjacent increment `k_list[i] - k_list[i-1] <= 28`.
5. then `k_min, k_max = k_list[0], k_list[-1]` (resynced from the list).
6. `k_max >= k_min` (redundant post-sort, still checked, 544).

### `set_max_k_by_lib()` (764–776) — runs after `build_library()` (997)
Only when `auto_k` **and** `len(k_list) > 1`:
- `max_read_len = get_max_read_len()`.
- `new_k_list = [k for k in k_list if k < max_read_len + 20]`.
- if `new_k_list` empty → return False (k-list unchanged).
- else replace `k_list`, resync `k_min`/`k_max`, return True (logs "k-max reset").

`get_max_read_len()` (755–761) reads `<reads.lib>.lib_info`, iterates
`readlines()[2::2]` (every other line starting at index 2), and takes
`max(int(line.split()[2]))`. The `.lib_info` layout
(`src/sequence/io/sequence_lib.cpp:84–89`, `sequence_lib.h:56–60`): line 0 =
`<total_bases> <total_reads>`; then per library **two** lines — a description
line, then `<begin> <end> <max_read_len> <is_paired>`. So index `[2::2]` selects
the metadata-value lines and field `[2]` is `max_read_len`.

---

## 4. Option set + defaults

From `Options.__init__` (158–197) and `parse_option` (292–432). CLI long/short
options come from the `getopt` spec (294–335). Defaults:

| field | default | CLI flag(s) | notes |
|-------|---------|-------------|-------|
| `out_dir` | `''` → `./megahit_out` | `-o`,`--out-dir` | `abspath()`; in test mode a temp dir |
| `temp_dir` | `''` → `<out>/tmp` | `--tmp-dir` | if set, a `megahit_tmp_` mkdtemp under it |
| `test_mode` | False | `--test` | toy dataset (§7) |
| `continue_mode` | False | `--continue` | §5 |
| `force_overwrite` | False | `-f`,`--force` | allow existing out_dir |
| `memory` | `0.9` | `-m`,`--memory` | <1 = fraction of total RAM, ≥1 = bytes |
| `min_contig_len` | `200` | `--min-contig-len` | |
| `k_min` | `21` | `--k-min` | §3 |
| `k_max` | `141` | `--k-max` | §3 |
| `k_step` | `10` | `--k-step` | must be even |
| `k_list` | `[21,29,39,59,79,99,119,141]` | `--k-list` | §3 |
| `auto_k` | True | (implicit) | cleared by k-* flags |
| `set_list_by_min_max_step` | False | (implicit) | |
| `min_count` | `2` | `--min-count` | must be >0; `==1` ⇒ `kmin_1pass=True, no_mercy=True` (548–550) |
| `has_popcnt` | True | (via `--no-hw-accel`) | §7 |
| `hw_accel` | True | `--no-hw-accel` | sets both `hw_accel=False, has_popcnt=False` |
| `max_tip_len` | `-1` | `--max-tip-len` | `-1` ⇒ auto (2·k heuristic) |
| `no_mercy` | False | `--no-mercy` | |
| `no_local` | False | `--no-local` | skips local assembly |
| `bubble_level` | `2` | `--bubble-level` | clamped to [0,2] (571–576) |
| `merge_len` | `20` | `--merge-level` (`l,s`) | int part of `l` |
| `merge_similar` | `0.95` | `--merge-level` (`l,s`) | `s` |
| `prune_level` | `2` | `--prune-level` | must be 0–3 |
| `prune_depth` | `2` | `--prune-depth` | float; if `<0 and prune_level<3` set to `min_count` (569–570) |
| `num_cpu_threads` | `0` → all CPUs | `-t`,`--num-cpu-threads` | clamped to `_available_cpus()`; 0 ⇒ all |
| `disconnect_ratio` | `0.1` | `--disconnect-ratio` | must be [0,0.5] |
| `low_local_ratio` | `0.2` | `--low-local-ratio` | must be (0,0.5] |
| `cleaning_rounds` | `5` | `--cleaning-rounds` | must be ≥1 |
| `keep_tmp_files` | False | `--keep-tmp-files` | suppresses all temp cleanup + final rmtree |
| `mem_flag` | `1` | `--mem-flag` | 0=min,1=moderate,other=all |
| `out_prefix` | `''` | `--out-prefix` | affects log + final contig names |
| `kmin_1pass` | False | `--kmin-1pass` | 1-pass `read2sdbg` build for k_min |
| `pe1`/`pe2` | `[]` | `-1` / `-2` | comma-split, `abspath`-ed, appended |
| `pe12` | `[]` | `--12` | interleaved |
| `se` | `[]` | `-r`,`--read` | single-end |
| `presets` | `''` | `--presets` | `meta-sensitive` / `meta-large` |
| `verbose` | False | `--verbose` | promotes subcommand stderr to INFO |

**Computed properties (`Options`):**
- `host_mem` (227–239): `memory<=0`→Usage; `<1`→`floor(detect_available_mem()*memory)`; else `floor(memory)`.
- `log_file_name`: `<out>/log` or `<out>/<out_prefix>.log`.
- `option_file_name`: `<out>/options.json`.
- `contig_dir`: `<out>/intermediate_contigs`.
- `read_lib_path`: `<temp_dir>/reads.lib`.
- `megahit_core`: variant binary selector (§7).

**Deprecated options** (410–413), accepted but ignored with a stderr warning:
`--cpu-only`, `-l`, `--max-read-len`, `--no-low-local`, `--use-gpu`, `--gpu-mem`.

**Immediate-exit options:** `-h`/`--help` prints version+usage and `exit(0)`;
`-v`/`--version` prints version and `exit(0)`. Empty argv → prints usage as a
`Usage` error. Unknown option → `Usage('Invalid option …')`.

**Other validation in `check_and_correct_option()`:** `memory < 0` → Usage (496);
thread count > available → warn and clamp (563–566).

---

## 5. Checkpoint / resume

### `@check_point` decorator + `Checkpoint` class (250–284)
- `Checkpoint` holds `_current_checkpoint` (next number to assign, starts 0),
  `_logged_checkpoint` (highest completed number loaded from disk, or `None`),
  and `_file` (the checkpoint file path).
- `set_file(path)` is called in `setup_output_dir()` (442) with
  `<out_dir>/checkpoints.txt`.
- The decorator wraps each stage function. On call (`checked_or_call`, 261–268):
  - If `_logged_checkpoint is None` (fresh run) **or**
    `_current_checkpoint > _logged_checkpoint` (this stage not yet done) → run
    the wrapped function, then **append** `"<n>\tdone\n"` to the checkpoint file.
  - Else log `passing check point <n>` and skip execution.
  - Always `_current_checkpoint += 1` afterward.

### File format / location
`<out_dir>/checkpoints.txt`, append-only, one record per completed checkpoint:
`<int>\tdone`. `load_for_continue()` (272–279) sets `_logged_checkpoint = -1`,
then scans the file; for each line that splits into exactly two tokens with the
second `== 'done'`, it sets `_logged_checkpoint = int(token0)`. (It takes the
*last* such value seen; records are appended in order so this is the max.)

### `--continue -o <out>` flow (`setup_output_dir`, 444–451)
- If `continue_mode` and `options.json` missing → warn, fall back to normal mode.
- Else: print "Continue mode … Ignore all options except for -o/--out-dir.",
  call `opt.load_for_continue()` (re-loads **all** options from `options.json`,
  245–247) and `check_point.load_for_continue()`.
- On the rerun, every decorated stage with number `<= _logged_checkpoint` is
  skipped; execution resumes at the first not-yet-`done` checkpoint.

### Why reordering/adding checkpoints breaks resume
Checkpoint numbers are **positional** — assigned by call order at runtime, not
tied to a stage name. Adding, removing, or reordering a `@check_point` function
(or hitting a different conditional branch, e.g. 1-pass vs 2-pass at k_min, or
`--no-local` toggling `local_assemble`) shifts every subsequent number. A resume
then maps the on-disk numbers to the wrong stages and either re-runs or skips the
wrong work. The port must preserve the same ordered set of checkpointable stages
(and the same branch-dependent count) to stay resume-compatible with existing
output dirs — or define a new, name-keyed checkpoint scheme and not claim
compatibility.

**The full ordered checkpoint list (default 2-pass, local on):**
0 `create_library_file`, 1 `build_library`, 2 `count_mink`, 3 `build_graph(k_min)`,
4 `assemble(k_min)`, then per next-k: `local_assemble`, `iterate`, `build_graph`,
`assemble` (4 per iteration), finally `merge_final`.
(1-pass collapses 2+3 into a single `build_first_graph_1pass`; `--no-local`
drops the `local_assemble` from each iteration.)

---

## 6. Output dir layout + intermediate file naming

### `setup_output_dir()` (435–465)
- Resolve `out_dir` (test mode → `tempfile.mkdtemp('megahit_test_')`, else
  `./megahit_out`).
- Set checkpoint file `<out>/checkpoints.txt`.
- Continue-mode handling (§5).
- Fresh run: if not `force_overwrite` and not `test_mode` and `out_dir` exists →
  Usage error (refuse to overwrite, 453–456).
- temp_dir: empty → `<out>/tmp`; else a `megahit_tmp_` mkdtemp under the given dir
  (458–461).
- `mkdir_if_not_exists` for `out_dir`, `temp_dir`, `contig_dir`.

### Path builders
- `graph_prefix(k)` (640–642): ensures `<temp_dir>/k<k>/` exists, returns
  `<temp_dir>/k<k>/<k>`. SdBG / edge / mercy files for k live here.
- `contig_prefix(k)` (645–646): `<contig_dir>/k<k>` =
  `<out>/intermediate_contigs/k<k>`. Per-k contigs live here.

### Files by location
- `<out>/log` (or `<out_prefix>.log`), `<out>/options.json`,
  `<out>/checkpoints.txt`, `<out>/done` (final sentinel).
- `<out>/final.contigs.fa` (or `<out_prefix>.contigs.fa`).
- `<out>/intermediate_contigs/`: `k<k>.contigs.fa`, `k<k>.bubble_seq.fa`,
  `k<k>.addi.fa`, `k<k>.local.fa`, `k<k>.final.contigs.fa` (per assemble/local round).
- `<temp_dir>/reads.lib`, `reads.lib.bin`, `reads.lib.lib_info` (+ binary pkg).
- `<temp_dir>/k<k>/<k>.*`: SdBG (`.w .last .isd .dn .f .mul .mul2`, `.sdbg.<i>`),
  edges (`.edges.<i>`), mercy (`.mercy_cand.<i>`, `.mercy.<i>`), `.cand`.
- `<temp_dir>/inpipe.{pe12,pe1,pe2,se}.<i>` — FIFOs for compressed inputs.

### Temp cleanup
- `remove_temp_after_build(kmer_k)` (649–656): removes `.edges.<i>` for
  `i in range(num_cpu_threads)`, `.mercy_cand.<i>` for `i in range(64)`,
  `.mercy.<i>` for `i in range(num_cpu_threads)`, and `.cand`. Called after every
  graph build unless `keep_tmp_files`.
- `remove_temp_after_assemble(kmer_k)` (659–663): removes
  `.{w,last,isd,dn,f,mul,mul2}` and `.sdbg.<i>` for `i in range(num_cpu_threads)`.
  Called after each non-final `assemble` unless `keep_tmp_files`.
- End of run: whole `temp_dir` removed unless `keep_tmp_files`.

### `merge_final` output
`cat <contig_dir>/*.final.contigs.fa <contig_prefix(final_k)>.contigs.fa |
filterbylen <min_contig_len>` → `final.contigs.fa`. See §2.8.

---

## 7. Host integration

### `cpu_dispatch()` → binary selection (620–637, property `megahit_core` 219–225)
Three binaries: `megahit_core` (BMI2+POPCNT), `megahit_core_popcnt` (POPCNT),
`megahit_core_no_hw_accel` (portable). Selection:
- If `--no-hw-accel` was given (`hw_accel` False): log and use no-hw-accel binary.
- Else run `megahit_core checkcpu` (stdout `'1'` ⇒ BMI2+POPCNT present): keep
  `hw_accel=True`, use `megahit_core`.
- Else set `hw_accel=False`, run `megahit_core checkpopcnt`: stdout `'1'` ⇒
  `has_popcnt=True`, use `megahit_core_popcnt`; otherwise use no-hw-accel binary.

The `Options.megahit_core` property maps `(hw_accel, has_popcnt)` → the path:
`hw_accel`→`megahit_core`; else `has_popcnt`→`megahit_core_popcnt`; else
`megahit_core_no_hw_accel`. On arm64 the x86 checks return non-`1`, so the
no-hw-accel binary is selected. `SoftwareInfo` also exposes `dumpversion`
(version string) and `kmax` (max k).

### `detect_available_mem()` (604–617)
Tries `sysconf('SC_PAGE_SIZE') * sysconf('SC_PHYS_PAGES')`. On failure: macOS →
parse `sysctl hw.memsize`; Linux → parse `free` line 2 field 2 ×1024. Used by
`host_mem` when `-m` < 1.

### `_available_cpus()` (486–491)
`len(os.sched_getaffinity(0))` (Linux); on macOS/BSD `AttributeError` → fall back
to `os.cpu_count() or 1`. (This is the macOS port fix flagged in CLAUDE.md.)

### Library descriptor `reads.lib` (`create_library_file`, 675–705)
Plain text; **two lines per library entry**. The first line is a human-readable
source spec; the second line is the typed descriptor `buildlib` consumes:
- **interleaved (`--12`)**: line1 = original filename; line2 =
  `interleaved <path>` where `<path>` is the FIFO `inpipe.pe12.<i>` if compressed,
  else the file itself.
- **paired-end (`-1`/`-2`)**: line1 = `"<file1>,<file2>"`; line2 =
  `pe <f1> <f2>` where each `f` is the FIFO `inpipe.pe1.<i>` / `inpipe.pe2.<i>`
  if compressed, else the file.
- **single-end (`-r`)**: line1 = original filename; line2 = `se <path>`
  (FIFO `inpipe.se.<i>` if compressed, else file).

The C++ `buildlib` reader (`sequence_lib.cpp`) keys on the type token
`interleaved` / `pe` / `se` and treats anything else as fatal.

### gzip/bzip2 FIFO decompression (`inpipe_cmd` 666–672, `build_library` 708–752)
- `inpipe_cmd(name)`: `.gz` → `gzip -cd <name>`; `.bz2` → `bzip2 -cd <name>`;
  else `''` (no pipe → use file directly).
- For each compressed input, `build_library` mkfifos `inpipe.<type>.<i>` and
  launches `<decompress-cmd> > <fifo>` via `subprocess.Popen(shell=True,
  preexec_fn=os.setsid)` (new process group). `buildlib` reads the FIFOs named in
  `reads.lib`. After buildlib exits, all pipes are `wait()`ed (non-zero ⇒ abort);
  `finally` kills any survivors with `os.killpg(p.pid, SIGTERM)` and removes the
  FIFOs.

### `check_reads()` (589–601) + test data (`find_test_data_path` 579–586)
- In `test_mode`, sets fixed inputs from `test_data/`: pe12 =
  `r1.il.fa.gz, r2.il.fa.bz2`; pe1 = `r3_1.fa`; pe2 = `r3_2.fa`; se =
  `r4.fa, loop.fa`. (`find_test_data_path` searches `<script>/..` and
  `<script>/../share/megahit` for a `test_data/` containing those files.)
- Validates `len(pe1) == len(pe2)`, and that every input file exists.

### Subprocess execution + error handling (`run_sub_command` 947–974)
All stages run via `subprocess.Popen(cmd, stderr=PIPE)`; stderr is streamed line
by line to the logger (INFO if `verbose`/`opt.verbose`, else DEBUG). Non-zero
exit → log error + `exit(ret_code)`. `KeyboardInterrupt` → terminate child,
`exit(SIGINT)`. `check_output` (107–112) is the helper for the
`checkcpu`/`checkpopcnt`/`dumpversion`/`kmax` capture calls (`assert wait()==0`).

---

## Port checklist (parity-critical)

1. Same subcommand names and exact flag spellings/values (§2) — including the
   duplicate `--cleaning-rounds`, the conditional `--need_mercy`,
   `--careful_bubble`, `--is_final_round`, `--output_standalone`, and the
   `seq2sdbg` input conditionals + `file_size`/`EarlyTerminate` logic.
2. Same ordered checkpoint stages and branch-dependent counts (§1, §5) if resume
   compatibility with existing `checkpoints.txt` is required.
3. Same on-disk paths and cleanup (§6) — downstream stages locate inputs by these
   exact prefixes.
4. Same k-list derivation, validation, and `set_max_k_by_lib` shrink (§3).
5. Same `reads.lib` descriptor format and FIFO-based compressed-input handling
   (§7) — `buildlib` parses these literally.
6. Same `host_mem`/`num_cpu_threads`/binary-variant resolution (§4, §7).
