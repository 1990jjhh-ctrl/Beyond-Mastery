#include "Resources.h"

namespace BM::Resources
{
	namespace
	{
		// From Skyrim.esm's SLGM records. The Black Star is reusable: paying with
		// it empties it rather than consuming it (FR-6).
		constexpr RE::FormID kBlackStar = 0x00063B29;  // DA01SoulGemBlackStar

		// One stack of filled black soul gems: a_extra is the stack's own extra
		// data, or null for copies that carry the base form's soul.
		struct GemStack
		{
			RE::TESSoulGem*    gem{ nullptr };
			RE::ExtraDataList* extra{ nullptr };
			std::int32_t       count{ 0 };
		};

		struct Scan
		{
			std::vector<GemStack> consumable;
			std::uint32_t         consumableCount{ 0 };
			RE::ExtraDataList*    blackStar{ nullptr };  // the filled Black Star's extra data
		};

		// A filled black soul gem is a soul gem the game marks NPC-capable,
		// holding a grand soul. SOUL_LEVEL has no "black" value, so a black soul
		// and a white grand soul in a black gem are indistinguishable here; the
		// plan records this as a ruling.
		[[nodiscard]] bool IsFilledBlack(RE::SOUL_LEVEL a_soul) noexcept
		{
			return a_soul == RE::SOUL_LEVEL::kGrand;
		}

		[[nodiscard]] Scan ScanBlackGems(RE::PlayerCharacter* a_player)
		{
			Scan scan;
			for (auto& [object, data] : a_player->GetInventory()) {
				auto* gem = object ? object->As<RE::TESSoulGem>() : nullptr;
				if (!gem || !gem->CanHoldNPCSoul()) {
					continue;
				}
				const auto& [total, entry] = data;
				const bool isBlackStar = gem->GetFormID() == kBlackStar;

				// Stacks with their own extra data: a trapped soul lives here.
				std::int32_t inExtra = 0;
				if (entry && entry->extraLists) {
					for (auto* xList : *entry->extraLists) {
						if (!xList) {
							continue;
						}
						const auto count = xList->GetCount();
						inExtra += count;
						const auto* soul = xList->GetByType<RE::ExtraSoul>();
						if (!IsFilledBlack(soul ? soul->GetContainedSoul() : gem->GetContainedSoul())) {
							continue;
						}
						if (isBlackStar) {
							scan.blackStar = xList;
						} else {
							scan.consumable.push_back({ gem, xList, count });
							scan.consumableCount += static_cast<std::uint32_t>(count);
						}
					}
				}

				// The remaining copies carry the base form's soul, which is how
				// SoulGemBlackFilled comes prefilled. The Black Star's base soul
				// is empty, so its plain copies never count.
				const auto plain = total - inExtra;
				if (plain > 0 && !isBlackStar && IsFilledBlack(gem->GetContainedSoul())) {
					scan.consumable.push_back({ gem, nullptr, plain });
					scan.consumableCount += static_cast<std::uint32_t>(plain);
				}
			}
			return scan;
		}
	}

	Holdings Read()
	{
		Holdings holdings;
		auto*    player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return holdings;
		}

		// perkCount moves at 1.6.629 and again at 1.7.99; only NG's runtime
		// accessor knows where (CLAUDE.md, "Working on hooks").
		holdings.perks = player->GetGameStatsData().perkCount;

		const auto souls = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kDragonSouls);
		holdings.dragonSouls = souls > 0.0f ? static_cast<std::uint32_t>(souls) : 0;

		const auto scan = ScanBlackGems(player);
		holdings.consumableGems = scan.consumableCount;
		holdings.blackStarFilled = scan.blackStar != nullptr;
		return holdings;
	}

	std::uint32_t GemsAvailable(const Holdings& a_holdings) noexcept
	{
		return a_holdings.consumableGems + (a_holdings.blackStarFilled ? 1u : 0u);
	}

	SpendResult Spend(const Curves::StepCost& a_cost, const Curves::GemPlan& a_gems)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return SpendResult::kNotAffordable;
		}

		// FR-5: everything is checked before anything is taken. Re-read rather
		// than trust the box the player was looking at.
		const auto have = Read();
		const auto plannedGems = a_gems.consumable + (a_gems.blackStar ? 1u : 0u);
		if (plannedGems != a_cost.blackSoulGems ||
			!Curves::Affordable(a_cost, have.perks, GemsAvailable(have), have.dragonSouls) ||
			a_gems.consumable > have.consumableGems ||
			(a_gems.blackStar && !have.blackStarFilled)) {
			logger::warn("Upgrade no longer affordable when confirmed; nothing spent");
			return SpendResult::kNotAffordable;
		}

		// Gems first: they are the only part the engine can refuse. Perks and
		// dragon souls follow only once every gem is taken, so a failure here
		// never costs them and never applies the upgrade.
		//
		// The Black Star before any removal can reshuffle the inventory under the
		// pointer: drop its soul in place, so the item itself -- and anything
		// attached to that instance -- stays (FR-6).
		if (a_gems.blackStar) {
			if (auto* star = ScanBlackGems(player).blackStar) {
				star->RemoveByType(RE::ExtraDataType::kSoul);
			}
			if (ScanBlackGems(player).blackStar) {
				logger::error("The Black Star kept its soul; nothing else taken");
				return SpendResult::kGemRemovalFailed;
			}
		}

		// Consumable gems, one stack at a time with a re-scan after each removal:
		// removing from one stack can merge or free another stack's extra data.
		// Every removal must visibly take what it asked for -- if the engine
		// refuses one, the next scan would offer the same stack forever.
		auto left = a_gems.consumable;
		while (left > 0) {
			const auto scan = ScanBlackGems(player);
			if (scan.consumable.empty()) {
				logger::error("Ran out of black soul gems while spending ({} short)", left);
				return SpendResult::kGemRemovalFailed;
			}
			const auto& stack = scan.consumable.front();
			const auto  take = std::min<std::uint32_t>(left, static_cast<std::uint32_t>(stack.count));
			player->RemoveItem(stack.gem, static_cast<std::int32_t>(take), RE::ITEM_REMOVE_REASON::kRemove,
				stack.extra, nullptr);
			const auto after = ScanBlackGems(player).consumableCount;
			if (after + take != scan.consumableCount) {
				logger::error("Removing {} black soul gem(s) took {} instead; stopping ({} still owed)", take,
					scan.consumableCount - std::min(after, scan.consumableCount), left);
				return SpendResult::kGemRemovalFailed;
			}
			left -= take;
		}

		player->GetGameStatsData().perkCount -= static_cast<std::uint8_t>(a_cost.perks);

		// The way the engine itself spends a dragon soul when a shout is learned:
		// ModActorValue(kPermanent, kDragonSouls, -1.0) (SkyrimSE.exe 1.7.104,
		// 0x1409538e3). The count lives in the permanent modifier, not the base.
		if (a_cost.dragonSouls > 0) {
			player->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIERS::kPermanent,
				RE::ActorValue::kDragonSouls, -static_cast<float>(a_cost.dragonSouls));
		}

		logger::info("Spent {} perk(s), {} dragon soul(s), {} black soul gem(s){}", a_cost.perks,
			a_cost.dragonSouls, a_gems.consumable, a_gems.blackStar ? " and The Black Star's soul" : "");
		return SpendResult::kSpent;
	}
}
