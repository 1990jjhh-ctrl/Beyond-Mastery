#include "CustomSkills.h"

#include "Compat.h"
#include "Config.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace BM::CustomSkills
{
	namespace
	{
		constexpr auto kDir = "Data/SKSE/Plugins/CustomSkills";

		std::vector<Skill> g_skills;
		bool               g_active = false;  // CSF is loaded and Resolve ran

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
		}
		g_active = true;
	}

	const std::vector<Skill>& All() noexcept
	{
		return g_skills;
	}

	Skill* FindById(std::string_view a_id) noexcept
	{
		const auto it = std::ranges::find(g_skills, a_id, &Skill::id);
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
		std::vector<std::string> names;
		for (const auto& s : g_skills) {
			names.push_back(s.name);
		}
		if (!CustomSkillsData::UniqueNames(names).contains(std::string(a_name))) {
			return nullptr;
		}
		const auto it = std::ranges::find(g_skills, a_name, &Skill::name);
		return it != g_skills.end() ? &*it : nullptr;
	}

	bool AnyEnabled() noexcept
	{
		return g_active && std::ranges::any_of(g_skills, [](const Skill& s) { return s.settings.enable && s.levelGlobal; });
	}

	bool SetSetting(std::string_view a_id, std::string_view a_key, std::string_view a_value)
	{
		const auto skill = FindById(a_id);
		return skill && Config::Get().SetCustomSkillValue(*skill, a_key, a_value);
	}
}
