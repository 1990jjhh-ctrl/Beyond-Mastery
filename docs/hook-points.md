# Hook points: reverse-engineering record

How each engine patch site was found, and the evidence behind it. What a site *is* — Address Library ID, offset, expected bytes — lives in [`src/HookSites.h`](../src/HookSites.h), and what each hook does lives in the code that installs and handles it: [`src/Hooks.cpp`](../src/Hooks.cpp), [`src/Scaling.h`](../src/Scaling.h), [`src/Stubs.h`](../src/Stubs.h) and [`src/Menu.cpp`](../src/Menu.cpp). This file keeps what the code cannot carry: sources, method, the findings that ruled options in or out, and the verification record. Requirements are cited by their PRD IDs.

Everything was established against `SkyrimSE.exe` 1.7.104 (AE ids); SE 1.5.97 is out of scope (PRD §3). Every site has confidence **H**: confirmed in our own disassembly and checked byte for byte by `scripts/verify_sites.py` (see [Verification record](#verification-record)).

## Sources

- **Skyrim AE Uncapper (Rust)**, <https://github.com/TheDreadedAndy/SkyrimAEUncapper-Rust>: the starting point for the skill-cap, XP and enchanting sites. The repository has no license file, so none of its code is used. Its IDs, offsets and patterns were taken only as facts about the binary and re-verified.
- **CommonLibSSE-NG**, <https://github.com/alandtse/CommonLibSSE-NG>: struct layouts, vtable and function IDs (`RE::VTABLE_*`, `RELOCATION_ID`), and the Address Library test fixtures used for the drift check.

## Method

- **Disassembly:** `llvm-objdump` on `SkyrimSE.exe`, with Address Library IDs resolved from `versionlib-1-7-104-0.bin`. A function's ID is the nearest ID at or below an address.
- **Callers:** found by scanning `.text` for `call rel32` instructions whose target is the function. A site is only hooked at its calls when that list is complete.
- **Vtables:** found by searching the data sections for the function's address, and mapping each hit back to the vtable ID at or below it.
- **GMST operands:** a `Setting` object is `{vtable, value, name*}`, so a `[rip + x]` operand is identified by reading the name pointer at `x + 8`.
- **Patterns:** each one is long enough to be unique within `verify_sites.py`'s ±0x1000 drift window, and ends on a whole instruction. Call targets are wildcarded and checked separately at install, against the expected callee.

## Skill cap and XP

- **Cap:** the XP routine (41561) compares the skill's *base* value (`call [rax+0x18]`, vtable slot 3) against the constant at `0x141b5bf14`, which reads exactly 100.0. Replacing that load with the skill's limit is the whole cap mechanic (FR-1 … FR-4, FR-13). The same constant is shared with the engine's skill clamp, which is why the constant is not patched in place.
- **The cap at the limit:** when a level-up reaches the limit, the engine sets `levelThreshold` to 0 and stops (`0x14078df1f`), and at the limit the routine adds no XP. `player.advskill` still accumulates `xp`. Upgrade therefore restores the threshold and zeroes `xp` (FR-8).
- **Other uncappers** (the Rust uncapper) patch the same cap site, overwriting at least 14 bytes. The same site is a hard conflict, and the byte check sees it (FR-20).
- **Experience 3.7.4** (from its AE DLL and PDB) does not touch the cap site. It replaces the two calls into the XP routine (40488+0x25, 41562+0x98) with a wrapper that skips the call while the skill's base value is at or above `fSkillCapBase + level × fSkillCapMult` (plus racial bonuses, no limit at 100). Both caps therefore apply, the lower one first (FR-32). The check runs once per grant, before the routine turns the XP into levels, so one large grant (`player.advskill` with a big amount) climbs past Experience's cap up to Beyond Mastery's; normal use overshoots by at most one level. Kept that way by decision, rather than enforcing Experience's cap inside the routine through its unpublished API. It also writes inside the character-XP clamp site (41561+0x2CF), so that clamp stays out while Experience is loaded, and it sets the skills menu's progress value to −100 for a capped skill (52525+0x104).
- **Legendary** needs no extra hook for the cap: the cap lives in the co-save, and Legendary only resets the level (FR-12).

## Formula scaling

- **Why the vtable slot, not a patch:** `Actor::GetActorValue` (38462) is slot 1 of three ActorValueOwner vtables and has no direct callers. Patching it would remap NPCs too. Replacing only the player's slot catches every read of the player's skills and no NPC's.
- **The clamp has 13 callers:** `ActorValueOwner::GetClampedActorValue` (27284) is not a spell-cost clamp but the engine's generic clamp by `ActorValueInfo::flags`. Its callers include Pickpocket, Lockpicking and Sneak (constant AV 0xD/0xE/0xF), both spell-cost callers (11017, 11494) and barter Speech (50945+0x7A0). Without replacing it, `s_eff` above 100 would be cut back to 100 for all of them.
- **Spell cost reads any skill:** 26505 takes the skill as an int, and the caller passes whichever skill the effect is tied to (any AV from 6 to 0x17), so a modded effect tied to a non-magic skill reaches it too. The player and NPCs use separate GMST sets (`fMagicCasterPC*` vs `fMagicCaster*`), selected by a virtual call on the caster.
- **Max charge needs no hook:** max charge is the soul's charge passed through perk entry 0x4F (`ModSoulGemEnchanting`). It never reads the skill.
- **Barter pipeline:** Speech is read clamped, then 26464 turns it into the buy and sell factors (`fBarterMin`/`fBarterMax`, then `fBarterBuyMin`/`fBarterSellMax`). Perk entries 8 and 0x3C follow, then the final clamp (`fMinBuyMult`, `fMaxSellMult`, 1.0). The perk-entry call before that clamp has already trashed every volatile register, which is what makes it safe to replace the clamp with a C++ call.
- **Menus that display a skill:** these were found by searching for the Flash strings they send. The skills menu (`UpdateSkillList`) and the crafting menus' bottom bar (`UpdateCraftingInfo`, one function for smithing, alchemy and enchanting) read slot 1 and needed fixing (FR-29). The trainer menu (`TrainingMenuObj`) reads slots 3 and 2 and is unaffected.
- **Out of scope:** 26507, next to the spell-cost code, has its own 100 thresholds but takes percentages, not skills.

## Legendary box

- **Construction sites:** cross-referencing the vtable of `RE::LegendarySkillResetConfirmCallback` (AE 215997) finds exactly two construction sites, both in `StatsMenu`. Each passes the callback straight to `MessageBoxMenu::Create`, so replacing those two calls replaces the box (FR-14).
- **`Run` confirms twice:** it acts only for button 1 (Yes, in *[No] [Yes]*). On the first call, with `unk18` (+0x18) = 1, it clears the flag and shows a second box with the same callback, using the text from GMST `sLegendaryResetSecondConfirm`. Only that box's Yes resets the skill. Reset Skill therefore keeps vanilla's second box, and gates are cleared once the skill is seen below 100, not around `Run` (FR-31).
- **The second box's text is swapped:** the setting's pointer is replaced only while `Run` builds the box, because `Run` copies the text. `Setting::SetString` must never be used for this: it frees the old string, which belongs to the engine's heap, not ours.
- **Offered at any level ≥ 100:** seen in-game with One-Handed at 110.

## Verification record

`scripts/verify_sites.py` resolves every site in `src/HookSites.h` through an Address Library database and byte-compares its pattern in the real `SkyrimSE.exe`. Run it to repeat the check; `tests/test_verify_sites.py` runs it in the test suite.

- **Exact run:** against `SkyrimSE.exe` 1.7.104.0 with its own `versionlib-1-7-104-0.bin` (Address Library All in One v13), every site matches at drift 0.
- **Corroborating run:** against CommonLibSSE-NG's `versionlib-1-7-99-0.bin` fixture, every site is found again through a region-local drift: +0x260 in the skill-XP and enchanting functions, 0 for `GetClampedActorValue`. The drift is an artefact of comparing against a neighbouring database, not a correction: at runtime the player's own library gives exact offsets. It is also how sites can be re-found on a game version no database covers yet.
- **In-game:** every hook has run on 1.7.104 (SKSE64 2.3.1, Proton); see PRD §8 for the acceptance results.
