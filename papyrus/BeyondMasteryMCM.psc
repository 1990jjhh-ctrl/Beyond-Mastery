Scriptname BeyondMasteryMCM extends SKI_ConfigBase
; The SkyUI menu for Custom Skills Framework skills: one section per skill,
; editing bEnable and sPerkSource through Beyond Mastery's natives. The DLL
; validates and saves every change; this script only lays out the pages and
; shows the value the DLL holds after each change.

; A SkyUI page holds 128 options in two columns of 64. A skill takes three
; (header, toggle, menu), and the first row is kept for the notice of an
; unsupported Custom Skills Framework, so a column holds 20 skills and a page
; 40. More skills go on further pages, "Custom Skills 2" and on, up to
; MAX_PAGES.
int Property COLUMN_SKILLS = 20 AutoReadOnly
int Property PAGE_SKILLS = 40 AutoReadOnly
int Property MAX_PAGES = 8 AutoReadOnly

string[] skillIds
; The skills on the current page start at pageFirst; the option arrays are
; indexed by the skill's position on the page (literal size PAGE_SKILLS).
int pageFirst
int pageCount
int noticeOption = -1
int[] enableOptions
int[] sourceOptions
string[] sourceLabels

Event OnConfigInit()
	ModName = "Beyond Mastery"
	Pages = PageNames(1)
EndEvent

; SkyUI reads Pages right after this, so the page count follows the skills
; discovered in this session.
Event OnConfigOpen()
	int skills = BeyondMastery.GetCustomSkillIds().Length
	Pages = PageNames((skills + PAGE_SKILLS - 1) / PAGE_SKILLS)
EndEvent

Event OnPageReset(string page)
	skillIds = BeyondMastery.GetCustomSkillIds()
	enableOptions = new int[40]
	sourceOptions = new int[40]
	sourceLabels = new string[2]
	sourceLabels[0] = "Your perks"
	sourceLabels[1] = "The skill's own pool"

	; The landing page "" (not in Pages) shows the first page.
	int pageIndex = Pages.Find(page)
	if pageIndex < 0
		pageIndex = 0
	endif
	pageFirst = pageIndex * PAGE_SKILLS
	pageCount = skillIds.Length - pageFirst
	if pageCount > PAGE_SKILLS
		pageCount = PAGE_SKILLS
	elseif pageCount < 0
		pageCount = 0
	endif

	SetCursorFillMode(TOP_TO_BOTTOM)
	if skillIds.Length == 0
		AddTextOption("No Custom Skills Framework skills found.", "")
		return
	endif

	; Without the level limit patch the settings do nothing: say so on the
	; first row, and grey out every option.
	noticeOption = -1
	int top = 0
	int skillFlags = OPTION_FLAG_NONE
	if !BeyondMastery.CustomSkillLimitInstalled()
		noticeOption = AddTextOption("CSF version not supported", "")
		top = 2
		skillFlags = OPTION_FLAG_DISABLED
	endif

	int i = 0
	while i < pageCount
		if i == COLUMN_SKILLS
			SetCursorPosition(top + 1)
		endif
		string id = skillIds[pageFirst + i]
		AddHeaderOption(BeyondMastery.GetCustomSkillName(id))
		enableOptions[i] = AddToggleOption("Raise above 100", BeyondMastery.GetCustomSkillEnabled(id), skillFlags)
		int flags = skillFlags
		if !BeyondMastery.CustomSkillHasOwnPool(id)
			flags = OPTION_FLAG_DISABLED
		endif
		sourceOptions[i] = AddMenuOption("Perk points from", SourceLabel(id), flags)
		i += 1
	endwhile

	; The left column's last slot (126) is still free on a full page.
	int hidden = skillIds.Length - MAX_PAGES * PAGE_SKILLS
	if pageIndex == MAX_PAGES - 1 && hidden > 0
		SetCursorPosition(126)
		AddTextOption("Skills not shown:", hidden)
	endif
EndEvent

Event OnOptionSelect(int option)
	int i = SkillIndex(enableOptions, option)
	if i < 0
		return
	endif
	string id = skillIds[i]
	BeyondMastery.SetCustomSkillEnabled(id, !BeyondMastery.GetCustomSkillEnabled(id))
	SetToggleOptionValue(option, BeyondMastery.GetCustomSkillEnabled(id))
EndEvent

Event OnOptionMenuOpen(int option)
	int i = SkillIndex(sourceOptions, option)
	if i < 0
		return
	endif
	SetMenuDialogOptions(sourceLabels)
	SetMenuDialogStartIndex(SourceIndex(skillIds[i]))
	SetMenuDialogDefaultIndex(0)
EndEvent

Event OnOptionMenuAccept(int option, int index)
	int i = SkillIndex(sourceOptions, option)
	if i < 0 || index < 0
		return
	endif
	string id = skillIds[i]
	string source = "player"
	if index == 1
		source = "own"
	endif
	BeyondMastery.SetCustomSkillPerkSource(id, source)
	SetMenuOptionValue(option, SourceLabel(id))
EndEvent

Event OnOptionHighlight(int option)
	if option == noticeOption || (SkillIndex(enableOptions, option) >= 0 && !BeyondMastery.CustomSkillLimitInstalled())
		SetInfoText("This Custom Skills Framework version is not supported, so its skills stay at 100. BeyondMastery.log names the reason.")
		return
	endif
	if SkillIndex(enableOptions, option) >= 0
		SetInfoText("Lets this skill go past 100: Legendary then offers Upgrade and Reset Skill for it, as for vanilla skills.")
		return
	endif
	int i = SkillIndex(sourceOptions, option)
	if i >= 0
		if BeyondMastery.CustomSkillHasOwnPool(skillIds[i])
			SetInfoText("Which perk points pay for this skill's upgrades: your own, or the pool the skill defines.")
		else
			SetInfoText("This skill defines no perk pool of its own, so its upgrades cost your perk points.")
		endif
	endif
EndEvent

; The index in skillIds of the skill owning option, or -1. Unused slots hold
; 0, which can be a real option id on the landing page, so only the slots of
; skills on this page count.
int Function SkillIndex(int[] options, int option)
	int i = options.Find(option)
	if i < 0 || i >= pageCount
		return -1
	endif
	return pageFirst + i
EndFunction

; "Custom Skills", "Custom Skills 2", …: count names, at least 1, at most
; MAX_PAGES. Papyrus array sizes must be literals, hence the branches.
string[] Function PageNames(int count)
	string[] names
	if count <= 1
		names = new string[1]
	elseif count == 2
		names = new string[2]
	elseif count == 3
		names = new string[3]
	elseif count == 4
		names = new string[4]
	elseif count == 5
		names = new string[5]
	elseif count == 6
		names = new string[6]
	elseif count == 7
		names = new string[7]
	else
		names = new string[8]
	endif
	names[0] = "Custom Skills"
	int i = 1
	while i < names.Length
		names[i] = "Custom Skills " + (i + 1)
		i += 1
	endwhile
	return names
EndFunction

; 0 for "player", 1 for "own": the index in sourceLabels.
int Function SourceIndex(string id)
	if BeyondMastery.GetCustomSkillPerkSource(id) == "own"
		return 1
	endif
	return 0
EndFunction

string Function SourceLabel(string id)
	return sourceLabels[SourceIndex(id)]
EndFunction
