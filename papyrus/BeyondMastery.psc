Scriptname BeyondMastery Hidden
; The native functions of Beyond Mastery's DLL (BeyondMastery.dll), used by the
; MCM to configure Custom Skills Framework skills. For an unknown skill id,
; GetCustomSkillName returns the id, the other getters return false or
; "player", and the setters return false.

; The ids of the discovered skills, in discovery order. Empty without Custom
; Skills Framework.
string[] Function GetCustomSkillIds() global native

; Whether Custom Skills Framework's level limit is patched. False for an
; unsupported CSF version: enabled skills then stay at 100.
bool Function CustomSkillLimitInstalled() global native

; The translated display name, or the id for an unknown skill.
string Function GetCustomSkillName(string id) global native

; Whether the skill is opted in (bEnable).
bool Function GetCustomSkillEnabled(string id) global native

; Opts the skill in or out. False if the skill is unknown.
bool Function SetCustomSkillEnabled(string id, bool enabled) global native

; Whether the skill's JSON defines a perk point pool that resolved.
bool Function CustomSkillHasOwnPool(string id) global native

; "player" or "own" (sPerkSource).
string Function GetCustomSkillPerkSource(string id) global native

; Sets the perk point source to "player" or "own". False if rejected: unknown
; skill, invalid value, or "own" without a pool.
bool Function SetCustomSkillPerkSource(string id, string source) global native
