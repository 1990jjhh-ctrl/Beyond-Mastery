# Description

Beyond Mastery lifts the skill cap of 100, but makes you pay for it. Instead of uncapping everything through an INI, you raise each skill's cap separately in steps of +10. You pay with perk points, dragon souls and filled black soul gems, through the skill tree's own **Legendary** button.

Above 100, skills keep paying off, but less and less. Every effect that depends on a skill keeps growing, either more slowly or toward a fixed limit, so nothing ever becomes free or breaks. At 100 and below, everything is exactly vanilla.

Source code (GPL-3.0-or-later): https://github.com/1990jjhh-ctrl/Beyond-Mastery

# Installation instructions

1. Install the requirements below.
2. Install `BeyondMastery-<version>.zip` with your mod manager (Mod Organizer 2, Vortex).
   For a manual install, extract that archive into `Data/`, or extract `BeyondMastery-<version>-SKSE-Plugins.zip` straight into `Data/SKSE/Plugins/`.
3. Start the game once. The mod creates `Data/SKSE/Plugins/BeyondMastery.ini` with all settings and their descriptions. Updates keep your settings.

The mod has no ESP/ESL, so it takes no load-order slot. It's safe to add to an existing save.

**Uninstalling:** skills above 100 keep the level they reached, but stop progressing. Nothing you spent is refunded. Your raised caps stay in saves made with the mod; a save made without it drops them.

# Main features

**Raise your caps through Legendary.** At a skill's limit, press **Legendary** in the skill tree. Instead of vanilla's box you get:

- **Upgrade** shows the cost and what you have, then raises that skill's cap by 10.
- **Reset Skill** is vanilla Legendary: the skill goes back to 15 and its perk points are refunded. Your raised cap is kept.
- **Cancel** closes the box.

**What an upgrade costs:**

- **1 perk point** per +10.
- **From cap 200 on, filled black soul gems:** 1 per step from 200, 2 from 300, and so on. The Black Star counts as one gem: its soul is used, and you keep the Star.
- **Dragon souls at every hundred:** breaking through 100, 200, 300 … costs 10, 20, 30 … dragon souls.
- **Gates are paid again after a reset:** after Reset Skill, you pay each one again on the next climb.

**Skills above 100 keep paying off:**

- **Most effects grow with the square root of the skill:** weapon damage, armor, smithing, alchemy, enchanting strength, block, lockpicking, pickpocket and sneak.
- **Spell cost keeps falling toward a floor.** The skill's share of a spell's cost never drops below 25%.
- **Buying always costs at least 1.2× the selling price,** even with every barter perk and Fortify Barter.
- **Enchanted weapons always cost charge per hit,** even at extreme Enchanting levels.
- **Menus show your real level:** the skills menu, the crafting menus, notifications and the level-up screen.

**Optional settings** (`BeyondMastery.ini`):

- `bScaleXPGainAbove100`: above 100, skills level more slowly as they rise (0.71× XP at 200, 0.5× at 400). Trainers and skill books still give exact levels.
- `bClampCharXPAbove100`: skill level-ups above 100 give the character XP of one at 100.
- Tunable limits for spell cost, barter and enchanting, plus a switch to turn all formula scaling off.

**Only you are affected.** NPCs stay completely vanilla. Other mods and scripts that read your skill above 100 see the effective value, not the level shown in the skills menu.

# Requirements

- **Skyrim Special Edition / Anniversary Edition, version 1.6.x or 1.7.x**, tested on 1.7.104. **Not** supported: SE 1.5.97 (the mod loads but does nothing), VR, and Legendary Edition.
- [SKSE64](https://www.nexusmods.com/skyrimspecialedition/mods/30379?tab=files)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

**Compatible with Experience:** Experience keeps character XP and its player-level skill cap, Beyond Mastery the paid caps above 100. Both caps apply, and the lower one wins. With Experience installed, `bClampCharXPAbove100` has no effect.

**Incompatible:**

- **Other skill uncappers** such as Skyrim Uncapper and Custom Skills Framework. Beyond Mastery detects them and shows a warning once per session.
- **Mods that change Legendary or the skill-cap code** need a compatibility patch. If Beyond Mastery finds such a change, it leaves that part vanilla, and its log (`Documents/My Games/Skyrim Special Edition/SKSE/BeyondMastery.log`) names the conflict.

# Shout outs

- **The SKSE team** for SKSE64, and **meh321** for Address Library.
- **Ryan-rsm-McKenzie, powerof3, alandtse** and everyone behind **CommonLibSSE-NG**, which Beyond Mastery is built on.
- **The authors of Skyrim AE Uncapper**, whose research into where the engine keeps its skill caps was the starting point. No code from it is used.
- The open-source libraries inside the DLL are listed with their licenses in `THIRD-PARTY-NOTICES.txt`, next to this readme.
