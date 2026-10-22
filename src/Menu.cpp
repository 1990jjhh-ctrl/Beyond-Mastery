#include "Menu.h"

#include "Curves.h"
#include "Resources.h"
#include "Skills.h"
#include "State.h"

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

		// FR-31: Reset Skill is vanilla Legendary, so it runs vanilla's own
		// callback with the button vanilla resets on.
		void ResetSkill(RE::ActorValue a_skill, const Callback& a_vanilla)
		{
			if (!a_vanilla) {
				return;
			}
			const auto before = BaseLevel(a_skill);
			a_vanilla->Run(kVanillaConfirm);
			const auto after = BaseLevel(a_skill);

			// Run checks a precondition first and can do nothing. Only a skill
			// that actually dropped starts a new climb (FR-13). If the reset
			// lands later than this, Hooks::GetSkillCap clears the gates on the
			// skill's next XP gain below 100 instead.
			if (after < before) {
				State::Get().ClearGates(Skills::ToIndex(a_skill));
				logger::info("Reset Skill: {} {} -> {}, gates cleared", SkillName(a_skill), before, after);
			} else {
				logger::info("Reset Skill: {} still {} right after vanilla Legendary; gates kept until it drops",
					SkillName(a_skill), before);
			}
		}

		void Apply(RE::ActorValue a_skill, const Curves::Upgrade& a_upgrade, const Curves::GemPlan& a_gems)
		{
			if (!Resources::Spend(a_upgrade.cost, a_gems)) {
				RE::SendHUDMessage::ShowHUDMessage("Not enough to upgrade any more. Nothing was spent.");
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
