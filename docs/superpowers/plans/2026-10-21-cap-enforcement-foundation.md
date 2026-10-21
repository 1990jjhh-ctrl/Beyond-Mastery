# Cap Enforcement Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a per-skill cap stored in the co-save actually gate skill leveling in-game, on SE 1.5.97, AE 1.6.x and 1.7.x.

**Architecture:** A checked-in Python tool resolves every Address Library ID in `src/HookSites.h` against a shipped Address Library DB and byte-compares the expected instructions in a real `SkyrimSE.exe`, so hook sites are verified offline before any code is armed. A single installer resolves each site by Address Library ID, re-checks its byte signature at runtime, and skips-and-logs on mismatch. Two hooks in the XP routine then replace the engine's hardcoded `100.0` skill cap with the per-skill cap from `BM::State` and apply the XP-gain factor.

**Tech Stack:** C++23, CommonLibSSE-NG (GPL-3.0-or-later, static), xbyak trampolines, SKSE serialization, Python 3 for the offline verification tool.

**Spec:** [PRD.md](../../../PRD.md) — requirements are cited as `FR-n`. Engine-side design: [docs/hook-points.md](../../hook-points.md).

## Test Harness Note

`tests/test_curves.cpp` and `tests/test_hooksites.cpp` define only `CHECK` and
`CHECK_NEAR`. The tests below also use `CHECK_EQ(a, b)`; add it to the harness in
the first task that needs it, in the same style as the existing macros (print
`__FILE__`, `__LINE__`, both values, increment `g_failures`).

## Global Constraints

- Target runtimes: **SE 1.5.97, AE 1.6.x, and 1.7.x** (PRD §4 as amended by Task 2). No VR, no LE (PRD §3).
- Hook addresses resolve through **Address Library IDs only** — never hardcoded offsets (PRD §7).
- Every patch site is **byte-signature checked before writing**; on mismatch the patch is skipped and logged, never written blind (hook-points.md "Conventions").
- No change to any formula at skill ≤ 100; vanilla behavior is untouched up to 100 (PRD §3).
- `src/Curves.h` stays free of `RE/`, `SKSE/` and `PCH.h` includes and of C++23-only features — `scripts/test.sh` compiles it standalone as C++20 with `-Werror` (CLAUDE.md).
- Formula constants come from GMSTs read at runtime or from the disassembly, never from UESP values (PRD §9 risk row).
- `fXPGainFactorPerTier` range `(0, 1.0]`, default **1.0**; values above 1.0 clamp to 1.0 (FR-9).
- Caps are multiples of 10, minimum 100; missing co-save data means all caps 100 (FR-22).
- Licence: GPL-3.0-or-later, no dependencies with incompatible licences.

## Review Focus

Five failure modes the spec implies but no task's own happy-path tests exercise. Each has its test assigned to the task that owns the code.

1. **Address Library absent or for the wrong game version.** CommonLibSSE's `IDDB` calls `stl::report_and_fail`, which shows a message box and *terminates the game*. A player missing the Address Library must get a logged refusal and an unhooked-but-loaded plugin, not a crash to desktop. → Task 3.
2. **Skill level above its cap.** Reinstalling after uninstalling, or a lost co-save, leaves a skill at 150 with the cap back at 100 (FR-22, FR-23). Enforcement must clamp XP gain without corrupting the level or looping. → Task 4.
3. **Perk cost exceeding what the engine can hold.** `PlayerCharacter::perkCount` is a `uint8` (max 255) while step cost `n` grows without bound (FR-4, §6.2). Around step 256 the cost is unpayable and must be reported as unaffordable, never wrap. → Task 5 (cost side is Plan 2; the clamp arithmetic lives in `Curves.h`).
4. **Legendary at a raised cap.** The level resets to 15 while the cap stays at e.g. 300 (FR-12, FR-13). XP thresholds and enforcement must behave across that whole 15→300 span, including the tier multiplier at levels below 100. → Task 5.
5. **Another uncapper already patched the site.** The signature check fails; the plugin must warn exactly once per session and leave the site alone (FR-20, success criterion 6, hook-points.md §8). → Task 3.

---

### Task 1: Offline hook-site verification tool

Turns the one-off analysis that produced this plan into a reproducible, checked-in tool. It parses Address Library DB formats 1/2 (sparse, delta-encoded) and 5 (dense, direct-indexed by ID), resolves every ID in `src/HookSites.h`, and byte-compares the expected pattern in a real `SkyrimSE.exe`.

**Files:**

- Create: `scripts/verify_sites.py`
- Create: `tests/test_verify_sites.py`
- Modify: `scripts/test.sh` (run the Python test when `python3` is available)

**Interfaces:**

- Produces: `load_db(path) -> (format:int, version:tuple[int,int,int,int], id2offset:dict[int,int])`;
  `parse_sites(header_path) -> list[Site]` where `Site` has `.name .se_id .se_off .ae_id .ae_off .pattern_se .pattern_ae .verified`;
  `pattern_bytes(pattern:str) -> list[int|None]` (`None` = `??` wildcard);
  `check_site(exe, db, site, runtime) -> Result` with `.ok .addr .actual_bytes .drift`.
- Consumes: nothing from other tasks.

- [ ] **Step 1: Write the failing tests**

```python
# tests/test_verify_sites.py
def test_pattern_bytes_parses_hex_and_wildcards():
    assert pattern_bytes("FF 50 18 ?? 0F") == [0xFF, 0x50, 0x18, None, 0x0F]

def test_load_db_format5_dense():
    fmt, ver, m = load_db("lib/commonlibsse-ng/tests/REL/versionlib-1-7-99-0.bin")
    assert fmt == 5 and ver == (1, 7, 99, 0)
    assert m[41561] == 0x78da50      # XP routine, confirmed in the 1.7.104 disassembly
    assert m[51449] == 0x927070      # enchanting charge-cost function
    assert m[27284] == 0x447180      # spell cost, holds the magic-skill clamp

def test_load_db_format2_sparse():
    fmt, ver, m = load_db("lib/commonlibsse-ng/tests/REL/versionlib-1-6-1179-0.bin")
    assert fmt == 2 and ver == (1, 6, 1179, 0)
    assert m[41561] == 0x77d090

def test_load_db_format1_se():
    fmt, ver, m = load_db("lib/commonlibsse-ng/tests/REL/version-1-5-97-0.bin")
    assert ver == (1, 5, 97, 0)
    assert m[40554] == 0x6e61d0 and m[37517] == 0x620d60

def test_parse_sites_reads_every_site_from_the_header():
    sites = parse_sites("src/HookSites.h")
    assert [s.name for s in sites][:2] == ["GetSkillCap", "ImprovePlayerSkillPoints"]
    assert len(sites) == 9
    g = next(s for s in sites if s.name == "GetSkillCap")
    assert (g.se_id, g.se_off, g.ae_id, g.ae_off) == (40554, 0x45, 41561, 0x6F)

def test_text_section_picks_the_first_of_two():
    # SkyrimSE.exe has two sections named .text; the code section is the first.
    pe = PEFile(SKYRIM_EXE)
    assert pe.text.vaddr == 0x1000 and pe.text.rawsize > 0x1000000
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m pytest tests/test_verify_sites.py -v` (or the stdlib-only runner if pytest is absent)
Expected: FAIL, `ModuleNotFoundError` / `NameError: load_db`

- [ ] **Step 3: Implement `scripts/verify_sites.py`**

Format 1/2 decoding follows `lib/commonlibsse-ng/src/REL/IDDB.cpp::unpack_file`: per entry a type byte, `lo = type & 0xF` selects the ID encoding (0 = full u64, 1 = prev+1, 2/3 = prev ±u8, 4/5 = prev ±u16, 6 = u16, 7 = u32), `hi = type >> 4`, `hi & 7` selects the offset encoding the same way against `prevOffset`, and when `hi & 8` is set the offset is expressed in pointer-size units and must be multiplied by `pointer_size` after decoding. Header for 1/2: `i32 format`, `i32 version[4]`, `i32 nameLen`, `nameLen` bytes, `i32 pointerSize`, `i32 addressCount`. Header for format 5: `i32 format`, `u32 version[4]`, `char name[64]`, `i32 pointerSize`, `i32 dataFormat`, `i32 offsetCount`, then a dense `u32` array indexed directly by ID.

Take the **first** section named `.text` (the exe has two). Compute file offset as `rva - text.vaddr + text.rawptr`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 -m pytest tests/test_verify_sites.py -v`
Expected: PASS (6 tests)

- [ ] **Step 5: Add a CLI that reports every site against a given exe + DB**

`python3 scripts/verify_sites.py --exe <SkyrimSE.exe> --db <versionlib.bin> [--runtime ae|se]` prints one line per site with the resolved address, MATCH/DIFFER, and the actual bytes, and exits non-zero if any site differs. When the DB version and the exe version differ it reports the per-site drift instead of failing, since a neighbouring DB is still useful evidence.

- [ ] **Step 6: Verify against the installed game and record the evidence**

Run: `python3 scripts/verify_sites.py --exe "/work/Games/steamapps/common/Skyrim Special Edition/SkyrimSE.exe" --db lib/commonlibsse-ng/tests/REL/versionlib-1-7-99-0.bin --runtime ae`
Expected: all 9 sites MATCH. Eight at drift `+0x260`, `CapMagickaCDR` at drift `+0x0`.

- [ ] **Step 7: Commit**

```bash
git add scripts/verify_sites.py tests/test_verify_sites.py scripts/test.sh
git commit -m "feat: offline Address Library + hook-site verification tool"
```

---

### Task 2: Accept 1.7.x as a target runtime

The AE Address Library IDs, byte offsets **and** patterns in `src/HookSites.h` all hold unchanged on 1.7.104 (verified in Task 1), so 1.7.x needs no new site data — only a widened version gate and updated docs. CommonLibSSE's `REL::RelocationID(se, ae)` already selects the AE id for any runtime at or above 1.6.317, which includes 1.7.x.

**Files:**

- Modify: `PRD.md` §4 and §7 (supported runtimes)
- Modify: `docs/hook-points.md` (confidence column, new verification section)
- Modify: `src/HookSites.h` (set `verified`, document the verification)
- Modify: `src/main.cpp` (version gate)
- Modify: `tests/test_hooksites.cpp`

**Interfaces:**

- Produces: `BM::HookSites::kMinRuntime` / `kMaxKnownRuntime` constants and `bool BM::HookSites::IsSupportedRuntime(REL::Version)`.
- Consumes: Task 1's CLI, as the evidence behind `verified = true`.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_hooksites.cpp — add
static void TestRuntimeGate()
{
    using namespace BM::HookSites;
    CHECK(IsSupportedRuntime({1, 5, 97, 0}));
    CHECK(IsSupportedRuntime({1, 6, 1170, 0}));
    CHECK(IsSupportedRuntime({1, 7, 104, 0}));   // the installed runtime
    CHECK(!IsSupportedRuntime({1, 4, 15, 0}));   // pre-SE-1.5.97
    CHECK(!IsSupportedRuntime({1, 2, 15, 0}));   // VR/LE are out of scope (PRD §3)
}

static void TestEverySiteVerified()
{
    for (const auto* s : BM::HookSites::kAllSites)
        CHECK(s->verified);                       // Task 1 confirmed all 9
}
```

Note: `IsSupportedRuntime` must be testable natively, so it takes a plain `{major, minor, patch, build}` aggregate declared in `HookSites.h`, not `REL::Version`. `main.cpp` converts.

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/test.sh`
Expected: FAIL — `IsSupportedRuntime` undefined, and `verified` is still `false` on all 9 sites.

- [ ] **Step 3: Implement the gate and flip `verified`**

Add to `HookSites.h`: `struct RuntimeVersion { std::uint16_t major, minor, patch, build; }` with `operator<=>`, the two bounds, and `IsSupportedRuntime`. Set `verified = true` on all nine sites, and extend the file's header comment to record that verification was done with `scripts/verify_sites.py` against SkyrimSE.exe 1.7.104.0 using the 1.7.99 DB, noting that the AE entries apply unchanged to 1.7.x.

- [ ] **Step 4: Run to verify it passes**

Run: `scripts/test.sh`
Expected: PASS

- [ ] **Step 5: Amend the PRD and hook-points docs**

In `PRD.md` §4 replace the runtime list with "SE **1.5.97**, AE **1.6.x** and **1.7.x**" and note in §7 that 1.7.x reuses the AE Address Library IDs. In `docs/hook-points.md` add a "§9a Verification results" section recording, per site, the resolved 1.7.104 address and the drift, and raise each verified site's confidence to **H (own disassembly)**. Keep §6 (barter) at **L** — still unlocated.

- [ ] **Step 6: Add the version gate to plugin load**

In `SKSEPluginLoad`, log the runtime and, when `IsSupportedRuntime` is false, log `critical` and return `true` without installing hooks (load but stay inert — refusing to load would break the player's whole session).

- [ ] **Step 7: Commit**

```bash
git add PRD.md docs/hook-points.md src/HookSites.h src/main.cpp tests/test_hooksites.cpp
git commit -m "feat: support 1.7.x runtimes; mark all nine hook sites verified"
```

---

### Task 3: Signature-checked hook installer

One place that resolves a `Site` to an address, re-checks its bytes at runtime, and reports the outcome. Nothing is patched here yet; this task ends with an installer that can report every site as ready without writing a byte.

**Files:**

- Create: `src/Hooks.h`, `src/Hooks.cpp`
- Create: `tests/test_patterns.cpp`
- Modify: `CMakeLists.txt` (add the new sources), `scripts/test.sh`, `src/main.cpp`

**Interfaces:**

- Produces:
  - `bool BM::Patterns::Matches(std::string_view pattern, const std::uint8_t* at)` — game-free, unit-tested.
  - `std::size_t BM::Patterns::Length(std::string_view pattern)` — byte count, `0` for an empty pattern.
  - `std::vector<std::uint8_t> BM::Patterns::Synthesise(std::string_view pattern, std::uint8_t fill)` — bytes matching the pattern, wildcards replaced by `fill`; test support.
  - `enum class BM::Hooks::SiteStatus { kReady, kSignatureMismatch, kUnresolved };`
  - `SiteStatus BM::Hooks::Check(const HookSites::Site&)`
  - `bool BM::Hooks::InstallAll()` — returns false when any site needed by the current config is not `kReady`.
  - `bool BM::Hooks::AnySignatureMismatch()` — feeds the FR-20 warning.
- Consumes: `HookSites::kAllSites`, `HookSites::IsSupportedRuntime` (Task 2).

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/test_patterns.cpp
static void TestMatches()
{
    const std::uint8_t buf[] = { 0xFF, 0x50, 0x18, 0xAB, 0x0F };
    CHECK(BM::Patterns::Matches("FF 50 18 ?? 0F", buf));
    CHECK(BM::Patterns::Matches("FF 50", buf));              // prefix
    CHECK(!BM::Patterns::Matches("FF 51 18 ?? 0F", buf));    // one byte differs
    CHECK(BM::Patterns::Matches("", buf));                   // empty = no check
    CHECK_EQ(BM::Patterns::Length("FF 50 18 ?? 0F"), 5u);
    CHECK_EQ(BM::Patterns::Length(""), 0u);
}

static void TestMatchesEveryVerifiedPatternAgainstItself()
{
    // Each site's own pattern must match bytes synthesised from it, with
    // wildcards filled by an arbitrary value. Guards against a typo in a
    // pattern making a site permanently unpatchable.
    for (const auto* s : BM::HookSites::kAllSites)
        for (auto p : { s->patternSE, s->patternAE }) {
            if (p.empty()) continue;
            std::vector<std::uint8_t> synth = BM::Patterns::Synthesise(p, 0x5A);
            CHECK(BM::Patterns::Matches(p, synth.data()));
        }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts/test.sh`
Expected: FAIL — `BM::Patterns` undefined.

- [ ] **Step 3: Implement `BM::Patterns` in `src/Hooks.h`**

Header-only, game-free (same constraint as `Curves.h`) so `tests/test_patterns.cpp` compiles natively: `Matches`, `Length`, and `Synthesise(pattern, fill) -> std::vector<std::uint8_t>`. Parsing rules match `tests/test_hooksites.cpp`'s existing validator: two hex digits or `??` per byte, single-space separated.

- [ ] **Step 4: Run to verify it passes**

Run: `scripts/test.sh`
Expected: PASS

- [ ] **Step 5: Implement `BM::Hooks::Check` and `InstallAll` in `src/Hooks.cpp`**

`Check` resolves the site with `REL::RelocationID(site.se.id, site.ae.id)`, adds the runtime-appropriate offset, and calls `Patterns::Matches` on the resolved address with the runtime-appropriate pattern. Returns `kUnresolved` if the ID cannot be resolved, `kSignatureMismatch` if bytes differ, else `kReady`. `InstallAll` logs one line per site (`name`, resolved address, status) and installs nothing yet.

**Review Focus 1:** guard the Address Library before it can terminate the game. `REL::IDDB` fails via `stl::report_and_fail`, so check that the expected `Data/SKSE/Plugins/versionlib-<version>.bin` (or `version-<version>.csv`) exists *before* the first `RelocationID` resolution; if it is missing, log `critical` with the required filename and leave every site `kUnresolved`.

**Review Focus 5:** `AnySignatureMismatch()` returns true when any checked site reported `kSignatureMismatch`, so `main.cpp` can fold it into the single once-per-session FR-20 warning rather than emitting a second notification.

- [ ] **Step 6: Wire into plugin load and verify in-game**

Call `Hooks::InstallAll()` from the `kDataLoaded` message handler, after `DetectConflicts`. Fold `AnySignatureMismatch()` into `NotifyConflicts` so the warning still appears exactly once (success criterion 6).

Run: launch the game, then check `BeyondMastery.log`.
Expected: one line per site reading `kReady`, and no message box.

- [ ] **Step 7: Commit**

```bash
git add src/Hooks.h src/Hooks.cpp tests/test_patterns.cpp CMakeLists.txt scripts/test.sh src/main.cpp
git commit -m "feat: signature-checked hook site resolution and reporting"
```

---

### Task 4: Enforce the per-skill cap (FR-2, FR-8)

The XP routine loads a constant `100.0` and compares the skill's current level against it, branching past the XP gain when the level is at or above it. Replacing that constant load with the skill's own cap is the whole mechanic.

Confirmed at `ID 41561 + 0x6F` (AE/1.7.x), 1.7.104 address `0x78dd1f`:

```txt
ff 50 18              call  qword [rax+0x18]     ; current skill level -> xmm0
44 0f 28 c0           movaps xmm8, xmm0
f3 44 0f 10 15 ....   movss xmm10, [rip+..]      ; <- the 100.0 constant (0x141b5bf14)
41 0f 2f c2           comiss xmm0, xmm10
0f 83 ...             jae   <skip XP gain>
```

**Files:**

- Modify: `src/Hooks.cpp`, `src/Hooks.h`
- Modify: `src/State.h`, `src/State.cpp` (cap lookup by `ActorValue`)
- Modify: `tests/test_curves.cpp`

**Interfaces:**

- Produces: `float BM::Hooks::GetSkillCap(RE::ActorValue)` — the trampoline callee; returns `State::Cap` for a skill AV, `100.0f` for anything else.
- Consumes: `BM::State::Cap`, `BM::Skills::IsSkill`, `BM::Skills::ToIndex`.

- [ ] **Step 1: Identify the register holding the skill at the site**

The callee needs to know *which* skill is being levelled. Disassemble backwards from `0x78dd1f` to the function entry (`ID 41561`) with Task 1's tooling and determine which register or stack slot holds the skill `ActorValue` at the site. Record the finding in a comment on `kGetSkillCap` in `src/HookSites.h`, for SE and for AE, since the two wrappers differ (hook-points.md §1).

This is an investigation step with a written outcome, not a code step — do not write the trampoline until the register is known.

- [ ] **Step 2: Write the failing test**

```cpp
// tests/test_curves.cpp — cap clamping arithmetic, game-free
static void TestCapClamp()
{
    // Review Focus 2: a level above its cap must clamp, never go negative or wrap.
    CHECK_EQ(ClampLevelToCap(150.0, 100), 100.0);
    CHECK_EQ(ClampLevelToCap(100.0, 100), 100.0);
    CHECK_EQ(ClampLevelToCap(42.0, 100), 42.0);
    CHECK_EQ(ClampLevelToCap(305.0, 300), 300.0);
    CHECK(AtCap(100.0, 100));            // FR-2: raise allowed only when level == cap
    CHECK(AtCap(150.0, 100));            // above cap still counts as "at cap"
    CHECK(!AtCap(99.0, 100));
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `scripts/test.sh`
Expected: FAIL — `ClampLevelToCap` / `AtCap` undefined.

- [ ] **Step 4: Implement `ClampLevelToCap(double level, std::uint16_t cap) -> double` and `AtCap(double level, std::uint16_t cap) -> bool` in `src/Curves.h`**

- [ ] **Step 5: Run to verify it passes**

Run: `scripts/test.sh`
Expected: PASS

- [ ] **Step 6: Implement the trampoline**

Add `GetSkillCap(RE::ActorValue) -> float` to `src/Hooks.cpp` and install it at `kGetSkillCap` with an xbyak trampoline that preserves the original `call`/`movaps` and replaces the `movss xmm10, [rip+..]` so `xmm10` receives the per-skill cap. Patch width and the trailing NOPs follow hook-points.md §1 (≥14 bytes written, last two NOPed). Use `SKSE::AllocTrampoline` sized for both hooks in this task and Task 5.

- [ ] **Step 7: Verify in-game**

Run: set a skill to 100, confirm it stops; set its cap to 110 via a debug command or a hand-edited co-save, confirm it now levels to 110 and stops there.
Expected: matches PRD success criterion 1 (minus the purchase, which is Plan 2).

- [ ] **Step 8: Document the uninstall behaviour (FR-23)**

Add a section to `README.md` stating that after uninstalling, skills above 100 keep
their current level — the engine stores skill levels as floats — but stop
progressing, and that re-installing with a lost co-save returns every cap to 100
while leaving those levels in place (the case Review Focus 2 covers).

- [ ] **Step 9: Commit**

```bash
git add src/Curves.h src/Hooks.cpp src/Hooks.h src/State.h src/State.cpp tests/test_curves.cpp src/HookSites.h README.md
git commit -m "feat: enforce per-skill cap in the XP routine (FR-2, FR-8)"
```

---

### Task 5: XP scaling above 100 (FR-9, FR-10, FR-11)

**Files:**

- Modify: `src/Curves.h`, `src/Hooks.cpp`, `src/State.h`, `src/State.cpp`, `src/Config.cpp`
- Modify: `tests/test_curves.cpp`

**Interfaces:**

- Produces:
  - `double BM::Curves::XPThreshold(double level, double improveMult, double skillUseCurve, double improveOffset)` (FR-9's vanilla formula, extended past 100).
  - `bool BM::Curves::ThresholdNeedsDouble(double threshold)` — true past the FR-10 precision limit.
  - `float BM::Hooks::ImprovePlayerSkillPoints(RE::ActorValue, float base, float offset)` — the XP-gain callee.
  - `BM::State::XPProgress(std::size_t) / SetXPProgress(std::size_t, double)`, persisted as co-save record `'XPRG'` version 1, separate from `'CAPS'` so existing saves keep loading.
- Consumes: `Curves::XPGainMultiplier` (already implemented and tested), `Config::xpGainFactorPerTier`, `Config::clampCharXPAbove100`.

- [ ] **Step 1: Write the failing tests**

```cpp
static void TestXPThreshold()
{
    // FR-9: ImproveMult * level^fSkillUseCurve + ImproveOffset, extended past 100.
    CHECK_NEAR(XPThreshold(100, 1.0, 1.95, 0.0), std::pow(100.0, 1.95), 1e-6);
    CHECK(XPThreshold(200, 1.0, 1.95, 0.0) > XPThreshold(100, 1.0, 1.95, 0.0));
    // FR-10: past ~1.6e7 a float can no longer represent consecutive integers.
    CHECK(!ThresholdNeedsDouble(1.0e6));
    CHECK(ThresholdNeedsDouble(1.7e7));
    const double big = 1.7e7;
    CHECK(static_cast<float>(big) + 1.0f == static_cast<float>(big));                  // float loses it
    CHECK(big + 1.0 != big); // double keeps it
}

static void TestXPGainMultiplierAcrossLegendary()
{
    // Review Focus 4: after Legendary the level is 15 while the cap stays high.
    CHECK_EQ(XPGainMultiplier(15.0, 0.5), 1.0);     // below 100 -> vanilla
    CHECK_EQ(XPGainMultiplier(99.0, 0.5), 1.0);
    CHECK_EQ(XPGainMultiplier(100.0, 0.5), 1.0);    // T=1 -> f^0
    CHECK_NEAR(XPGainMultiplier(200.0, 0.5), 0.5, 1e-12);
    CHECK_NEAR(XPGainMultiplier(300.0, 0.5), 0.25, 1e-12);
    CHECK_EQ(XPGainMultiplier(300.0, 1.0), 1.0);    // default f = vanilla everywhere
}

static void TestPerkCostDoesNotWrap()
{
    // Review Focus 3: perkCount is a uint8; cost n grows unbounded.
    CHECK_EQ(CostOfNextStep(100 + 10 * 254).perks, 255u);
    CHECK(!Affordable(CostOfNextStep(100 + 10 * 300), /*perks*/ 255, /*gems*/ 99, /*souls*/ 99));
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `scripts/test.sh`
Expected: FAIL — `XPThreshold`, `ThresholdNeedsDouble`, `Affordable` undefined.

- [ ] **Step 3: Implement the three functions in `src/Curves.h`**

`Affordable(StepCost, std::uint32_t perks, std::uint32_t gems, std::uint32_t souls) -> bool` compares each resource and returns false if any falls short; it is also what Plan 2's FR-5 check calls.

- [ ] **Step 4: Run to verify they pass**

Run: `scripts/test.sh`
Expected: PASS

- [ ] **Step 5: Install the XP-gain hook (FR-9)**

Add `ImprovePlayerSkillPoints` to `src/Hooks.cpp`, returning `base * Curves::XPGainMultiplier(level, Config::xpGainFactorPerTier) + offset`, installed at `kImprovePlayerSkillPoints`. Per hook-points.md §2, skip installation entirely while `xpGainFactorPerTier == 1.0`, since the hook is then a no-op — fewer hooks, fewer conflicts.

- [ ] **Step 6: Add the `double` XP accumulator (FR-10)**

Track per-skill progress as `double` in `State` and persist it as `'XPRG'`, engaging it only once `ThresholdNeedsDouble` is true for the skill's current threshold; below that, defer to the engine's own float state so vanilla behaviour is bit-identical.

- [ ] **Step 7: Install the character-XP clamp (FR-11), config-gated**

Install at `kImproveLevelExpBySkillLevel` only when `bClampCharXPAbove100` is true (hook-points.md §3). When installed, the callee clamps the skill level fed to `fXPPerSkillRank` at 100.

- [ ] **Step 8: Verify in-game**

Run: with the clamp off, confirm a skill-up at 150 yields 1.5× the character XP of one at 100; with the clamp on, confirm they are equal.
Expected: PRD success criterion 4.

- [ ] **Step 9: Commit**

```bash
git add src/Curves.h src/Hooks.cpp src/State.h src/State.cpp src/Config.cpp tests/test_curves.cpp
git commit -m "feat: XP scaling above 100 (FR-9, FR-10, FR-11)"
```

---

## Follow-on plans

This plan deliberately stops where the next subsystem starts. Each of these produces working software on its own and should get its own plan:

- **Plan 2 — Cap-raise transaction and UI** (FR-1, FR-3…FR-7, FR-12…FR-17): the lesser power and its ESL-flagged ESP (one SPEL + one MGEF, written by a hand-rolled `scripts/make_plugin.py`), the paged message-box menu, the all-or-nothing cost check, black-soul-gem selection including The Black Star, and perk/dragon-soul deduction. Depends on this plan only for `Curves::Affordable` and `State`; otherwise independent and testable without hooks.
- **Plan 3 — Formula scaling above 100** (FR-24…FR-30): the `GetActorValue` remap with the skills-menu bypass, removing the vanilla magic-skill clamp, and the enchanting charge-cost replacement. Depends on Task 3's installer. Barter (FR-26) is blocked on reverse engineering: hook-points.md §6 is still confidence **L** and the price function is unlocated, so it needs its own investigation before it can be planned.

## Prerequisites outside the repo

Tasks 1 and 2 need nothing. Tasks 3–5 can be written and unit-tested without the game, but their in-game verification steps need, in the game install:

- **SKSE64** for 1.7.104 (`skse.silverlock.org`) — not currently installed.
- **Address Library for SKSE Plugins** matching 1.7.104 — not currently installed; without it the plugin will log a refusal and stay inert by design (Review Focus 1).

The offline verification in Task 1 is what makes Tasks 3–5 safe to write before those exist.
