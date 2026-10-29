#include "CustomSkills.h"

#include "Compat.h"
#include "Config.h"
#include "Curves.h"
#include "State.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <mutex>

namespace BM::CustomSkills
{
	namespace
	{
		constexpr auto kDir = "Data/SKSE/Plugins/CustomSkills";

		std::vector<Skill> g_skills;
		bool               g_active = false;  // CSF is loaded and Resolve ran
		// Guards every Skill::settings. Never held while writing the INI or
		// logging. Does not guard the rest of g_skills, which is built before
		// the hooks are installed and not changed afterwards.
		std::mutex g_settingsLock;
		// Serializes SetSetting's INI read-modify-write with its memory update, so
		// concurrent calls cannot lose an edit or leave memory and file out of step.
		std::mutex g_writeLock;

		// Whether the skill is opted in (the settings are copied under the lock).
		bool IsEnabled(const Skill& a_skill)
		{
			return SettingsOf(a_skill).enable;
		}

		std::optional<CustomSkillsData::FormRef> FormField(const nlohmann::json& a_object, const char* a_key)
		{
			const auto it = a_object.find(a_key);
			if (it == a_object.end() || !it->is_string()) {
				return std::nullopt;
			}
			return CustomSkillsData::ParseFormRef(it->get<std::string>());
		}

		// path::string() throws for a name the ANSI code page cannot hold.
		std::string Narrow(const std::filesystem::path& a_path)
		{
			try {
				return a_path.string();
			} catch (const std::exception&) {
				return "<unreadable name>";
			}
		}

		bool IsJson(const std::filesystem::path& a_path)
		{
			auto ext = Narrow(a_path.extension());
			std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return ext == ".json";
		}

		void ReadFile(const std::filesystem::path& a_path)
		{
			const auto    file = Narrow(a_path.filename());
			std::ifstream in(a_path);
			const auto    root = nlohmann::json::parse(in, nullptr, false);
			if (root.is_discarded() || !root.is_object()) {
				logger::warn("CustomSkills/{} is not valid JSON, skipping it", file);
				return;
			}
			const auto skills = root.find("skills");
			if (skills == root.end() || !skills->is_array()) {
				return;
			}
			const auto perkPoints = FormField(root, "perkPoints");
			for (const auto& entry : *skills) {
				if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string()) {
					logger::warn("CustomSkills/{}: a skill without an id, skipping it", file);
					continue;
				}
				Skill skill;
				skill.id = entry["id"].get<std::string>();
				skill.file = file;
				const auto level = FormField(entry, "level");
				if (!level) {
					logger::warn("CustomSkills/{}: skill {} has no valid level global, skipping it", file, skill.id);
					continue;
				}
				if (const auto existing = FindById(skill.id)) {
					logger::warn("CustomSkills/{}: skill {} is already defined by {}, skipping it",
						file, skill.id, existing->file);
					continue;
				}
				skill.level = *level;
				const auto name = entry.find("name");
				skill.name = name != entry.end() && name->is_string() ? name->get<std::string>() : skill.id;
				skill.displayName = skill.name;
				skill.legendary = FormField(entry, "legendary");
				skill.perkPoints = perkPoints;
				g_skills.push_back(std::move(skill));
			}
		}

		RE::TESGlobal* Lookup(const std::optional<CustomSkillsData::FormRef>& a_ref, const Skill& a_skill, const char* a_what)
		{
			if (!a_ref) {
				return nullptr;
			}
			const auto global = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(a_ref->localID, a_ref->plugin);
			if (!global) {
				logger::warn("Custom skill {}: {} {}|{:X} not found", a_skill.id, a_what, a_ref->plugin, a_ref->localID);
			}
			return global;
		}

		// CSF names a skill's ActorValueInfo after the JSON name passed through
		// SKSE's translation, so a "$KEY" name shows translated; anything else,
		// or a key with no translation, stays as written.
		std::string Translated(const std::string& a_name)
		{
			std::string out;
			return SKSE::Translation::Translate(a_name, out) ? out : a_name;
		}
	}

	void Discover()
	{
		g_skills.clear();
		// Runs in SKSEPlugin_Load: nothing here may throw.
		std::vector<std::filesystem::path> files;
		try {
			std::error_code                     ec;
			std::filesystem::directory_iterator it(kDir, ec);
			for (; !ec && it != std::filesystem::directory_iterator{}; it.increment(ec)) {
				std::error_code typeEc;
				if (it->is_regular_file(typeEc) && IsJson(it->path())) {
					files.push_back(it->path());
				}
			}
			std::ranges::sort(files);
		} catch (const std::exception& e) {
			logger::warn("Could not list {}: {}", kDir, e.what());
		}
		for (const auto& f : files) {
			try {
				ReadFile(f);
			} catch (const std::exception& e) {
				logger::warn("CustomSkills/{} could not be read ({}), skipping it", Narrow(f.filename()), e.what());
			}
		}
		logger::info("Found {} custom skill(s) in {} file(s)", g_skills.size(), files.size());
	}

	void Resolve()
	{
		if (!Compat::CustomSkillsLoaded()) {
			if (!g_skills.empty()) {
				logger::info("Custom Skills Framework is not loaded: the {} custom skill(s) in {} stay untouched", g_skills.size(), kDir);
			}
			return;
		}
		for (auto& s : g_skills) {
			s.levelGlobal = Lookup(s.level, s, "level global");
			s.legendaryGlobal = Lookup(s.legendary, s, "legendary global");
			s.perkPointsGlobal = Lookup(s.perkPoints, s, "perk points global");
			s.displayName = Translated(s.name);
			if (s.displayName != s.name) {
				logger::info("Custom skill {}: name {} shows as {}", s.id, s.name, s.displayName);
			}
		}
		g_active = true;
	}

	const std::vector<Skill>& All() noexcept
	{
		return g_skills;
	}

	Skill* FindById(std::string_view a_id) noexcept
	{
		const auto it = std::ranges::find_if(g_skills, [&](const Skill& a_skill) { return CustomSkillsData::SameId(a_skill.id, a_id); });
		return it != g_skills.end() ? &*it : nullptr;
	}

	Skill* FindByLevelGlobal(const RE::TESGlobal* a_global) noexcept
	{
		if (!a_global) {
			return nullptr;
		}
		const auto it = std::ranges::find(g_skills, a_global, &Skill::levelGlobal);
		return it != g_skills.end() ? &*it : nullptr;
	}

	Skill* FindByName(std::string_view a_name) noexcept
	{
		try {
			std::vector<std::string> names;
			for (const auto& s : g_skills) {
				names.push_back(s.displayName);
			}
			const auto index = CustomSkillsData::UniqueIndex(names, a_name);
			return index ? &g_skills[*index] : nullptr;
		} catch (...) {
			return nullptr;  // the copies can throw bad_alloc
		}
	}

	bool AnyEnabled() noexcept
	{
		if (!g_active) {
			return false;
		}
		try {
			return std::ranges::any_of(g_skills, [](const Skill& s) { return s.levelGlobal && IsEnabled(s); });
		} catch (...) {
			return false;  // locking a mutex can throw
		}
	}

	CustomSkillsData::Settings SettingsOf(const Skill& a_skill)
	{
		const std::lock_guard lock(g_settingsLock);
		return a_skill.settings;
	}

	void ApplySettings(Skill& a_skill, CustomSkillsData::Settings a_settings)
	{
		const std::lock_guard lock(g_settingsLock);
		a_skill.settings = std::move(a_settings);
	}

	std::int32_t LevelLimit(const RE::TESGlobal* a_level) noexcept
	{
		constexpr std::int32_t kVanilla = State::kDefaultCap;  // CSF's own limit
		try {
			const auto skill = FindByLevelGlobal(a_level);
			if (!skill) {
				return kVanilla;
			}
			// As Hooks::GetSkillCap does for the vanilla skills: paid gates exist
			// only at 100 and above, so a skill below 100 that still holds some
			// has been reset and starts a new climb (FR-13). CSF calls this on
			// every XP gain, so the reset is caught whenever it lands, and never
			// after a declined Legendary, because then the level never dropped.
			// Cleared for a disabled skill too: re-enabled at 100, it must pay
			// its gates again rather than climb on to its old limit.
			auto& state = State::Get();
			if (a_level->value < Curves::kVanillaCap) {
				if (const auto cleared = state.ClearCustomGates(skill->id); cleared > 0) {
					logger::info("Custom skill {} below 100: its {} paid gate(s) are owed again", skill->id, cleared);
				}
			}
			return IsEnabled(*skill) ? state.CustomLimitOf(skill->id) : kVanilla;
		} catch (...) {
			return kVanilla;  // State builds a std::string key and locks a mutex, both of which can throw
		}
	}

	float UseXPFactor(const RE::TESGlobal* a_level) noexcept
	{
		try {
			const auto skill = FindByLevelGlobal(a_level);
			if (!skill || !IsEnabled(*skill) || !Config::Get().scaleXPGainAbove100) {
				return 1.0f;
			}
			return static_cast<float>(Curves::XPGainMultiplier(a_level->value));
		} catch (...) {
			return 1.0f;  // locking a mutex can throw
		}
	}

	bool SetSetting(std::string_view a_id, std::string_view a_key, std::string_view a_value)
	{
		const std::lock_guard write(g_writeLock);  // not g_settingsLock: that one is never held during file I/O
		const auto            skill = FindById(a_id);
		const bool            set = skill && Config::Get().SetCustomSkillValue(*skill, a_key, a_value, [&](CustomSkillsData::Settings a_settings) {
			ApplySettings(*skill, std::move(a_settings));
		});
		logger::info("MCM: {} {} = {}{}", skill ? skill->id : std::string(a_id), a_key, a_value, set ? "" : " (rejected)");
		return set;
	}
}
