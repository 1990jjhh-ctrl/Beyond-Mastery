#include "Menu.h"

#include "Curves.h"
#include "Resources.h"
#include "Skills.h"
#include "State.h"

#include <cstring>
#include <format>
#include <functional>

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
		// (hook-points.md §7.1: `cmp bl, 1; jne <return>`).
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

		[[nodiscard]] std::string SkillName(RE::ActorValue a_skill)
		{
			const auto* info = RE::ActorValueList::GetActorValueInfo(a_skill);
			const char* name = info ? info->GetFullName() : nullptr;
			return name && *name ? name : "this skill";
		}

		[[nodiscard]] float BaseLevel(RE::ActorValue a_skill)
		{
			const auto* player = RE::PlayerCharacter::GetSingleton();
			return player ? player->AsActorValueOwner()->GetBaseActorValue(a_skill) : 0.0f;
		}

		void ShowMain(RE::ActorValue a_skill, Callback a_vanilla);
		void ShowUpgrade(RE::ActorValue a_skill, Callback a_vanilla);

		// Vanilla's second Legendary box takes its text from this game setting
		// when Run builds it.
		constexpr const char* kSecondConfirmSetting = "sLegendaryResetSecondConfirm";

		// What the reset does to the cap here (PRD FR-31): "<Skill> keeps its cap
		// of N", plus the dragon souls this climb's gates cost, which are lost.
		// Empty when the cap was never raised, so that box stays pure vanilla.
		[[nodiscard]] std::string CapNotice(RE::ActorValue a_skill)
		{
			const auto  index = Skills::ToIndex(a_skill);
			const auto& state = State::Get();
			const auto  cap = state.Cap(index);
			const auto  souls = Curves::DragonSoulsPaid(state.GatesPaid(index));
			if (cap <= State::kDefaultCap && souls == 0) {
				return {};
			}
			return souls > 0 ?
			           std::format("{} keeps its cap of {}, but the {} dragon souls spent on it are lost.",
						   SkillName(a_skill), cap, souls) :
			           std::format("{} keeps its cap of {}.", SkillName(a_skill), cap);
		}

		// FR-31: Reset Skill is vanilla Legendary. Vanilla confirms twice, and our
		// menu stands in for the first box: the first Run(1) on this callback
		// shows vanilla's second box ("...set it to 15 and return its perks.
		// Are you sure?"), and only that box's Yes resets. Gates are cleared by
		// Hooks::GetSkillCap once the skill is seen below 100, which is right
		// whichever answer the player gives.
		void ResetSkill(RE::ActorValue a_skill, const Callback& a_vanilla)
		{
			if (!a_vanilla) {
				return;
			}

			// The cap notice goes in front of vanilla's own, localised text, for
			// just the moment Run builds the box -- it copies the text -- and the
			// setting is restored straight after. Setting::SetString is not used:
			// it frees the old string, which is exe- or engine-owned memory that
			// our static CRT must never free.
			auto*      settings = RE::GameSettingCollection::GetSingleton();
			auto*      setting = settings ? settings->GetSetting(kSecondConfirmSetting) : nullptr;
			const auto notice = CapNotice(a_skill);
			if (setting && setting->data.s && !notice.empty()) {
				static std::array<char, 1024> text{};  // fixed address, valid while the setting points at it
				char* const                   original = setting->data.s;
				const auto                    composed = std::format("{} {}", notice, original);
				const auto                    length = std::min(composed.size(), text.size() - 1);
				std::memcpy(text.data(), composed.data(), length);
				text[length] = '\0';

				setting->data.s = text.data();
				a_vanilla->Run(kVanillaConfirm);
				setting->data.s = original;
			} else {
				a_vanilla->Run(kVanillaConfirm);
			}
			logger::info("Reset Skill: {} handed to vanilla Legendary{}", SkillName(a_skill),
				notice.empty() ? "" : " with the cap notice");
		}

		void Apply(RE::ActorValue a_skill, const Curves::Upgrade& a_upgrade, const Curves::GemPlan& a_gems)
		{
			switch (Resources::Spend(a_upgrade.cost, a_gems)) {
			case Resources::SpendResult::kSpent:
				break;
			case Resources::SpendResult::kNotAffordable:
				RE::SendHUDMessage::ShowHUDMessage("Not enough to upgrade any more. Nothing was spent.");
				return;
			case Resources::SpendResult::kGemRemovalFailed:
				RE::SendHUDMessage::ShowHUDMessage("Could not take the soul gems; the upgrade was not applied. See BeyondMastery.log.");
				return;
			}
			const auto index = Skills::ToIndex(a_skill);
			auto&      state = State::Get();
			state.ApplyUpgrade(index, a_upgrade);
			const auto limit = Curves::Limit(state.Cap(index), state.GatesPaid(index));
			logger::info("Upgrade: {} cap {} gates {} limit {}", SkillName(a_skill), state.Cap(index),
				state.GatesPaid(index), limit);
			const auto message = std::format("{} can now reach {}.", SkillName(a_skill), limit);
			RE::SendHUDMessage::ShowHUDMessage(message.c_str());
		}

		// FR-7: when the player could pay either with or without The Black Star,
		// ask. When only one way works, use it -- the Upgrade box already said
		// whether the Star is part of the count.
		void Confirm(RE::ActorValue a_skill, Callback a_vanilla, Curves::Upgrade a_upgrade)
		{
			const auto have = Resources::Read();
			const auto needed = a_upgrade.cost.blackSoulGems;
			const auto withStar = Curves::PlanGems(needed, have.consumableGems, have.blackStarFilled, true);
			const auto withoutStar = Curves::PlanGems(needed, have.consumableGems, have.blackStarFilled, false);

			if (withStar && withoutStar && withStar->blackStar) {
				Show("Pay with The Black Star? Its soul is used and the Star kept.",
					{ { "Use The Black Star", [=] { Apply(a_skill, a_upgrade, *withStar); } },
						{ "Keep The Black Star", [=] { Apply(a_skill, a_upgrade, *withoutStar); } },
						{ "Back", [=] { ShowUpgrade(a_skill, a_vanilla); } } });
				return;
			}
			const auto plan = withoutStar ? withoutStar : withStar;
			if (!plan) {
				ShowUpgrade(a_skill, a_vanilla);  // no longer affordable: show what is missing
				return;
			}
			Apply(a_skill, a_upgrade, *plan);
		}

		// FR-16: the cost and the player's holdings, shown only once Upgrade is
		// chosen. Unaffordable: name what is missing and offer only Back (FR-5).
		void ShowUpgrade(RE::ActorValue a_skill, Callback a_vanilla)
		{
			const auto  index = Skills::ToIndex(a_skill);
			const auto& state = State::Get();
			const auto  cap = state.Cap(index);
			const auto  limit = Curves::Limit(cap, state.GatesPaid(index));
			const auto  upgrade = Curves::UpgradeFor(cap, state.GatesPaid(index));
			const auto  have = Resources::Read();
			const auto  name = SkillName(a_skill);

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
				buttons.push_back({ "Confirm", [=] { Confirm(a_skill, a_vanilla, upgrade); } });
			}
			buttons.push_back({ "Back", [=] { ShowMain(a_skill, a_vanilla); } });
			Show(body, buttons);
		}

		// FR-15: Reset Skill, Upgrade, Cancel. Upgrade only at the limit; below
		// it, the box names the level still to reach.
		void ShowMain(RE::ActorValue a_skill, Callback a_vanilla)
		{
			const auto  index = Skills::ToIndex(a_skill);
			const auto& state = State::Get();
			const auto  cap = state.Cap(index);
			const auto  limit = Curves::Limit(cap, state.GatesPaid(index));
			const auto  name = SkillName(a_skill);
			const bool  atLimit = Curves::AtCap(BaseLevel(a_skill), limit);
			// At the cap a raise must stay loadable; an unpaid gate can always be paid.
			const bool raisable = limit < cap || Curves::CapRaisable(cap);

			std::string         body = name;
			std::vector<Button> buttons{ { "Reset Skill", [=] { ResetSkill(a_skill, a_vanilla); } } };
			if (atLimit && raisable) {
				buttons.push_back({ "Upgrade", [=] { ShowUpgrade(a_skill, a_vanilla); } });
			} else if (!atLimit) {
				body = std::format("Reach {} to upgrade {}.", limit, name);
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
		if (!Skills::IsSkill(skill)) {
			return RE::MessageBoxMenu::Create(a_message, a_callback, a_buttonPressOffset, a_warningType, a_menuDepth, a_buttons);
		}
		ShowMain(skill, a_callback);
		return true;
	}
}
