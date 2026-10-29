#include "Menu.h"

#include "Curves.h"
#include "CustomSkills.h"
#include "Hooks.h"
#include "Resources.h"
#include "Skills.h"
#include "State.h"
#include "Texts.h"

#include <cstring>
#include <format>
#include <functional>
#include <set>
#include <variant>

namespace BM::Menu
{
	namespace
	{
		// What vanilla's Legendary box passes to MessageBoxMenu::Create, so ours
		// shows at the same priority and depth.
		constexpr std::uint8_t kButtonPressOffset = 0;
		constexpr std::int32_t kWarningType = 25;
		constexpr std::int32_t kMenuDepth = 10;

		// The button LegendarySkillResetConfirmCallback::Run resets on
		// (hook-points.md, "Legendary box": `cmp bl, 1; jne <return>`).
		constexpr std::uint8_t kVanillaConfirm = 1;

		using Callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>;

		struct Button
		{
			std::string           label;
			std::function<void()> action;  // empty: just close the box
		};

		// A box whose buttons each run one action. Labels and actions are built
		// from the same list, so a button can never run another button's
		// action. With buttonPressOffset 0, Run gets the pressed button's
		// 0-based index.
		class ActionCallback : public RE::IMessageBoxCallback
		{
		public:
			explicit ActionCallback(std::vector<std::function<void()>> a_actions) :
				_actions(std::move(a_actions))
			{}
			~ActionCallback() override = default;

			void Run(std::uint8_t a_button) override
			{
				if (a_button < _actions.size() && _actions[a_button]) {
					_actions[a_button]();
				}
			}

		private:
			std::vector<std::function<void()>> _actions;
		};

		// Boxes made by MessageBoxMenu::Create are not cancellable -- the
		// MessageBoxData constructor sets isCancellable = false and
		// cancelButtonIndex = -1 -- so Escape never presses a button, and every
		// box must carry its own way out.
		void Show(const std::string& a_body, const std::vector<Button>& a_buttons)
		{
			RE::BSString                       body{ std::string_view{ a_body } };
			RE::BSTArray<RE::BSString>         labels;
			std::vector<std::function<void()>> actions;
			for (const auto& button : a_buttons) {
				labels.push_back(RE::BSString{ std::string_view{ button.label } });
				actions.push_back(button.action);
			}
			const Callback callback = RE::make_smart<ActionCallback>(std::move(actions));
			RE::MessageBoxMenu::Create(body, callback, kButtonPressOffset, kWarningType, kMenuDepth, labels);
		}

		// One of the 18 vanilla skills, or an enabled Custom Skills Framework
		// skill (FR-33). The helpers below are the only places that tell them
		// apart, so the menu itself is one flow for both.
		using SkillRef = std::variant<RE::ActorValue, CustomSkills::Skill*>;

		[[nodiscard]] std::string SkillName(const SkillRef& a_skill)
		{
			if (const auto* custom = std::get_if<CustomSkills::Skill*>(&a_skill)) {
				// The name CSF gave its ActorValueInfo, which is how it was found.
				return (*custom)->displayName.empty() ? "this skill" : (*custom)->displayName;
			}
			const auto* info = RE::ActorValueList::GetActorValueInfo(std::get<RE::ActorValue>(a_skill));
			const char* name = info ? info->GetFullName() : nullptr;
			return name && *name ? name : "this skill";
		}

		[[nodiscard]] float BaseLevel(const SkillRef& a_skill)
		{
			if (const auto* custom = std::get_if<CustomSkills::Skill*>(&a_skill)) {
				// CSF keeps a custom skill's level in its level global.
				return (*custom)->levelGlobal ? (*custom)->levelGlobal->value : 0.0f;
			}
			const auto* player = RE::PlayerCharacter::GetSingleton();
			return player ? player->AsActorValueOwner()->GetBaseActorValue(std::get<RE::ActorValue>(a_skill)) : 0.0f;
		}

		// The skill's cap and the gates paid in its current climb.
		[[nodiscard]] CustomSkillsData::CustomCap CapOf(const SkillRef& a_skill)
		{
			if (const auto* custom = std::get_if<CustomSkills::Skill*>(&a_skill)) {
				return State::Get().CustomCapOf((*custom)->id);
			}
			const auto  index = Skills::ToIndex(std::get<RE::ActorValue>(a_skill));
			const auto& state = State::Get();
			return { state.Cap(index), state.GatesPaid(index) };
		}

		// The perk points an upgrade is paid with: the skill's own pool when its
		// sPerkSource is own, otherwise the player's (nullptr).
		[[nodiscard]] RE::TESGlobal* PerkPool(const SkillRef& a_skill)
		{
			const auto* custom = std::get_if<CustomSkills::Skill*>(&a_skill);
			return custom && CustomSkills::SettingsOf(**custom).perkSource == CustomSkillsData::PerkSource::kOwn ?
			           (*custom)->perkPointsGlobal :
			           nullptr;
		}

		void ShowMain(SkillRef a_skill, Callback a_vanilla);
		void ShowUpgrade(SkillRef a_skill, RE::TESGlobal* a_pool, Callback a_vanilla);

		// Vanilla's second Legendary box takes its text from this game setting
		// when Run builds it; the level it resets to is the other one.
		constexpr const char* kSecondConfirmSetting = "sLegendaryResetSecondConfirm";
		constexpr const char* kResetValueSetting = "fLegendarySkillResetValue";
		constexpr int         kVanillaResetValue = 15;

		// FR-31: Reset Skill is vanilla Legendary. Vanilla confirms twice, and our
		// menu stands in for the first box: the first Run(1) on this callback
		// shows vanilla's second box, and only that box's Yes resets. Gates are
		// cleared by Hooks::GetSkillCap once the skill is seen below 100, which
		// is right whichever answer the player gives -- for a custom skill, by
		// CustomSkills::LevelLimit. A custom skill's callback runs CSF's own
		// Legendary: CSF builds this same vanilla callback for it.
		void ResetSkill(const SkillRef& a_skill, const Callback& a_vanilla)
		{
			if (!a_vanilla) {
				return;
			}

			auto*       settings = RE::GameSettingCollection::GetSingleton();
			auto*       setting = settings ? settings->GetSetting(kSecondConfirmSetting) : nullptr;
			const auto* resetValue = settings ? settings->GetSetting(kResetValueSetting) : nullptr;
			const auto  cap = CapOf(a_skill);
			const auto  confirm = Texts::ResetConfirm(SkillName(a_skill),
				resetValue ? static_cast<int>(resetValue->GetFloat()) : kVanillaResetValue, cap.cap,
				Curves::DragonSoulsPaid(cap.gatesPaid));

			// The box gets our text in place of vanilla's for just the moment Run
			// builds it -- it copies the text -- and the setting is restored
			// straight after. Setting::SetString is not used: it frees the old
			// string, which is exe- or engine-owned memory that our static CRT
			// must never free.
			if (setting && setting->data.s) {
				static std::array<char, 1024> text{};  // fixed address, valid while the setting points at it
				char* const                   original = setting->data.s;
				const auto                    length = std::min(confirm.size(), text.size() - 1);
				std::memcpy(text.data(), confirm.data(), length);
				text[length] = '\0';

				setting->data.s = text.data();
				a_vanilla->Run(kVanillaConfirm);
				setting->data.s = original;
			} else {
				logger::warn("{} not found; Reset Skill shows vanilla's text", kSecondConfirmSetting);
				a_vanilla->Run(kVanillaConfirm);
			}
			logger::info("Reset Skill: {} handed to vanilla Legendary", SkillName(a_skill));
		}

		// FR-8: when a level-up reaches the limit, the engine sets that skill's
		// levelThreshold to 0 and stops, and its XP routine adds nothing at the
		// limit (SkyrimSE.exe 1.7.104, 0x14078df1f and 0x14078dd33). Left at 0,
		// the first XP gain after an Upgrade satisfies xp >= 0 and the level is
		// free. Other paths still add XP at the limit -- player.advskill left
		// millions in the field -- which the restored threshold would turn into
		// many free levels. So Upgrade restores the threshold the engine itself
		// computes for the level -- improveMult x level^fSkillUseCurve +
		// improveOffset, with the AVSK defaults 1 and 0 -- and zeroes the XP.
		// PlayerSkills lists the skills in the same order as Skills.h.
		using PlayerSkill = RE::PlayerCharacter::PlayerSkills::Data::Skill;
		static_assert(PlayerSkill::kTotal == Skills::kCount);
		static_assert(PlayerSkill::kEnchanting == Skills::kLastAV - Skills::kFirstAV);

		constexpr const char* kSkillUseCurveSetting = "fSkillUseCurve";

		void RestartLevel(RE::ActorValue a_skill)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* skills = player ? player->GetInfoRuntimeData().skills : nullptr;
			auto* settings = RE::GameSettingCollection::GetSingleton();
			auto* curve = settings ? settings->GetSetting(kSkillUseCurveSetting) : nullptr;
			if (!skills || !skills->data || !curve) {
				logger::error("Player skill data or {} unavailable; {}'s next level may be free",
					kSkillUseCurveSetting, SkillName(a_skill));
				return;
			}

			const auto* info = RE::ActorValueList::GetActorValueInfo(a_skill);
			const auto* avsk = info ? info->skill : nullptr;
			const auto  level = BaseLevel(a_skill);
			const auto  threshold = Curves::XPThreshold(level, avsk ? avsk->improveMult : 1.0,
				curve->GetFloat(), avsk ? avsk->improveOffset : 0.0);

			auto& data = skills->data->skills[Skills::ToIndex(a_skill)];
			logger::info("{} at {}: threshold {} -> {}, xp {} -> 0", SkillName(a_skill), level,
				data.levelThreshold, threshold, data.xp);
			data.levelThreshold = static_cast<float>(threshold);
			data.xp = 0.0f;
		}

		// A custom skill only gets the new cap: RestartLevel works on the
		// engine's PlayerSkills data, which CSF does not use, and CSF's own
		// progress (its Ratio global) is left as it is -- FR-8 is not carried
		// over to custom skills.
		//
		// a_pool is the perk pool the Upgrade box showed. The MCM can change a
		// custom skill's settings while the box is open, so they are read again
		// here and nothing is charged if the skill was disabled or its perk
		// source changed: the player agreed to that pool's price.
		void Apply(const SkillRef& a_skill, RE::TESGlobal* a_pool, const Curves::Upgrade& a_upgrade, const Curves::GemPlan& a_gems)
		{
			if (const auto* custom = std::get_if<CustomSkills::Skill*>(&a_skill);
				custom && !CustomSkills::SettingsOf(**custom).enable) {
				const auto message = std::format("{} is no longer raised above 100; open Legendary again.", SkillName(a_skill));
				RE::SendHUDMessage::ShowHUDMessage(message.c_str());
				return;
			}
			if (PerkPool(a_skill) != a_pool) {
				const auto message = std::format("{}'s perk source changed; open Legendary again.", SkillName(a_skill));
				RE::SendHUDMessage::ShowHUDMessage(message.c_str());
				return;
			}
			switch (Resources::Spend(a_upgrade.cost, a_gems, a_pool)) {
			case Resources::SpendResult::kSpent:
				break;
			case Resources::SpendResult::kNotAffordable:
				RE::SendHUDMessage::ShowHUDMessage("Not enough to upgrade any more. Nothing was spent.");
				return;
			case Resources::SpendResult::kGemRemovalFailed:
				RE::SendHUDMessage::ShowHUDMessage("Could not take the soul gems; the upgrade was not applied. See BeyondMastery.log.");
				return;
			}
			if (const auto* custom = std::get_if<CustomSkills::Skill*>(&a_skill)) {
				State::Get().SetCustomCap((*custom)->id, { a_upgrade.newCap, a_upgrade.newGatesPaid });
			} else {
				const auto av = std::get<RE::ActorValue>(a_skill);
				State::Get().ApplyUpgrade(Skills::ToIndex(av), a_upgrade);
				RestartLevel(av);
			}
			const auto cap = CapOf(a_skill);
			const auto limit = Curves::Limit(cap.cap, cap.gatesPaid);
			logger::info("Upgrade: {} cap {} gates {} limit {}", SkillName(a_skill), cap.cap, cap.gatesPaid, limit);
			const auto message = std::format("{} can now reach {}.", SkillName(a_skill), limit);
			RE::SendHUDMessage::ShowHUDMessage(message.c_str());
		}

		// FR-7: when the player could pay either with or without The Black Star,
		// ask. When only one way works, use it -- the Upgrade box already said
		// whether the Star is part of the count.
		void Confirm(SkillRef a_skill, RE::TESGlobal* a_pool, Callback a_vanilla, Curves::Upgrade a_upgrade)
		{
			const auto have = Resources::Read(a_pool);
			const auto needed = a_upgrade.cost.blackSoulGems;
			const auto withStar = Curves::PlanGems(needed, have.consumableGems, have.blackStarFilled, true);
			const auto withoutStar = Curves::PlanGems(needed, have.consumableGems, have.blackStarFilled, false);

			if (withStar && withoutStar && withStar->blackStar) {
				Show("Pay with The Black Star? Its soul is used and the Star kept.",
					{ { "Use The Black Star", [=] { Apply(a_skill, a_pool, a_upgrade, *withStar); } },
						{ "Keep The Black Star", [=] { Apply(a_skill, a_pool, a_upgrade, *withoutStar); } },
						{ "Back", [=] { ShowUpgrade(a_skill, a_pool, a_vanilla); } } });
				return;
			}
			const auto plan = withoutStar ? withoutStar : withStar;
			if (!plan) {
				ShowUpgrade(a_skill, a_pool, a_vanilla);  // no longer affordable: show what is missing
				return;
			}
			Apply(a_skill, a_pool, a_upgrade, *plan);
		}

		// FR-16: the cost and the player's holdings, shown only once Upgrade is
		// chosen. Unaffordable: name what is missing and offer only Back (FR-5).
		void ShowUpgrade(SkillRef a_skill, RE::TESGlobal* a_pool, Callback a_vanilla)
		{
			const auto [cap, gatesPaid] = CapOf(a_skill);
			const auto limit = Curves::Limit(cap, gatesPaid);
			const auto upgrade = Curves::UpgradeFor(cap, gatesPaid);
			const auto have = Resources::Read(a_pool);
			const auto name = SkillName(a_skill);

			std::string body = upgrade.kind == Curves::UpgradeKind::kGate ?
			                       std::format("Break through {} in {}.\n\n", limit, name) :
			                       std::format("Raise {}'s cap to {}.\n\n", name, upgrade.newCap);

			bool affordable = true;
			auto line = [&](std::string_view a_what, std::uint32_t a_need, std::uint32_t a_have, std::string_view a_note = {}) {
				if (a_need == 0) {
					return;
				}
				body += std::format("{}: {} (you have {}{})", a_what, a_need, a_have, a_note);
				if (a_have < a_need) {
					body += std::format(", {} short", a_need - a_have);
					affordable = false;
				}
				body += "\n";
			};
			line("Perk points", upgrade.cost.perks, have.perks);
			line("Dragon souls", upgrade.cost.dragonSouls, have.dragonSouls);
			line("Filled black soul gems", upgrade.cost.blackSoulGems, Resources::GemsAvailable(have),
				have.blackStarFilled ? ", counting The Black Star" : "");

			std::vector<Button> buttons;
			if (affordable) {
				buttons.push_back({ "Confirm", [=] { Confirm(a_skill, a_pool, a_vanilla, upgrade); } });
			}
			buttons.push_back({ "Back", [=] { ShowMain(a_skill, a_vanilla); } });
			Show(body, buttons);
		}

		// FR-15: Reset Skill, Upgrade, Cancel. Upgrade only at the limit; below
		// it, the box names the level still to reach.
		void ShowMain(SkillRef a_skill, Callback a_vanilla)
		{
			const auto [cap, gatesPaid] = CapOf(a_skill);
			const auto limit = Curves::Limit(cap, gatesPaid);
			const auto name = SkillName(a_skill);
			const auto pool = PerkPool(a_skill);  // resolved once; the Upgrade box carries it
			if (pool) {
				logger::info("Legendary for {}: perk points from its own pool, global {:08X} ({})", name, pool->GetFormID(), pool->value);
			}
			const bool atLimit = Curves::AtCap(BaseLevel(a_skill), limit);
			// At the cap a raise must stay loadable; an unpaid gate can always be paid.
			const bool raisable = limit < cap || Curves::CapRaisable(cap);

			std::string         body = name;
			std::vector<Button> buttons{ { "Reset Skill", [=] { ResetSkill(a_skill, a_vanilla); } } };
			if (atLimit && raisable) {
				buttons.push_back({ "Upgrade", [=] { ShowUpgrade(a_skill, pool, a_vanilla); } });
			} else if (!atLimit) {
				body = std::format("Reach {} to upgrade {}.", limit, name);
			} else {
				// kMaxPlausibleCap: 990 upgrades and 49 500 dragon souls in.
				body = std::format("{} {}. There is nothing left to learn. Perhaps try going outside.", name, cap);
			}
			buttons.push_back({ "Cancel", {} });
			Show(body, buttons);
		}

		// The vtable LegendarySkillResetConfirmCallback objects carry; checked
		// before trusting the callback's layout.
		[[nodiscard]] bool IsLegendaryCallback(const RE::IMessageBoxCallback* a_callback)
		{
			static REL::Relocation<std::uintptr_t> vtable{ RE::LegendarySkillResetConfirmCallback::VTABLE[0] };
			return a_callback && *reinterpret_cast<const std::uintptr_t*>(a_callback) == vtable.address();
		}

		// Why a custom skill keeps CSF's own Legendary box; nullptr when ours
		// can open. A raise needs the cap enforced (CSF's limit patched), a
		// level to compare and a Legendary global for CSF's reset; an own perk
		// pool that is missing would otherwise charge the player instead.
		[[nodiscard]] const char* VanillaBoxReason(const CustomSkills::Skill& a_skill)
		{
			const auto settings = CustomSkills::SettingsOf(a_skill);  // one copy: a concurrent edit cannot split the checks
			if (!settings.enable) {
				return "it is not enabled";
			}
			if (!Hooks::CustomSkillLimitInstalled()) {
				return "Custom Skills Framework's level limit is not patched";
			}
			if (!a_skill.levelGlobal) {
				return "it has no level global";
			}
			if (!a_skill.legendaryGlobal) {
				return "it has no Legendary global";
			}
			if (settings.perkSource == CustomSkillsData::PerkSource::kOwn && !a_skill.perkPointsGlobal) {
				return "its own perk pool was not found";
			}
			return nullptr;
		}

		// The custom skill a_value names, or nullptr after logging -- once per
		// value and session -- why it gets CSF's own box. CSF's GetActorValueInfo
		// hook answers for its own values with the skill's info, whose full name
		// is the translated JSON name.
		[[nodiscard]] CustomSkills::Skill* ResolveCustom(RE::ActorValue a_value)
		{
			static std::set<RE::ActorValue> logged;
			const auto                      once = [&](const auto& a_message) {
				if (logged.insert(a_value).second) {
					logger::info("Legendary on custom skill {:#x}: {}; CSF's own box opens", std::to_underlying(a_value), a_message);
				}
			};

			const auto* info = RE::ActorValueList::GetActorValueInfo(a_value);
			const char* name = info ? info->GetFullName() : nullptr;
			if (!name || !*name) {
				once("no name for it");
				return nullptr;
			}
			auto* skill = CustomSkills::FindByName(name);
			if (!skill) {
				// Also a name two skills share: neither can be picked (Review Focus 5).
				once(std::format("no single custom skill is named \"{}\"", name));
				return nullptr;
			}
			if (const auto* reason = VanillaBoxReason(*skill)) {
				once(std::format("{} ({}): {}", skill->id, name, reason));
				return nullptr;
			}
			return skill;
		}
	}

	bool OpenLegendary(RE::BSString& a_message, const RE::BSTSmartPointer<RE::IMessageBoxCallback>& a_callback,
		std::uint8_t a_buttonPressOffset, std::int32_t a_warningType, std::int32_t a_menuDepth,
		const RE::BSTArray<RE::BSString>& a_buttons)
	{
		// The two hooked call sites only ever build this callback, but anything
		// unexpected -- another mod swapping it, a non-skill value -- falls back
		// to vanilla's own box rather than guessing.
		if (!IsLegendaryCallback(a_callback.get())) {
			return RE::MessageBoxMenu::Create(a_message, a_callback, a_buttonPressOffset, a_warningType, a_menuDepth, a_buttons);
		}
		const auto skill = static_cast<const RE::LegendarySkillResetConfirmCallback*>(a_callback.get())->skill;
		if (Skills::IsSkill(skill)) {
			ShowMain(skill, a_callback);
			return true;
		}
		if (CustomSkillsData::IsCustomActorValue(std::to_underlying(skill))) {
			if (auto* custom = ResolveCustom(skill)) {
				ShowMain(custom, a_callback);
				return true;
			}
		}
		return RE::MessageBoxMenu::Create(a_message, a_callback, a_buttonPressOffset, a_warningType, a_menuDepth, a_buttons);
	}
}
