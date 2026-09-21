#include "Bridge.h"

#include "Settings.h"
#include "SunHelm.h"

namespace
{
	constexpr auto kScriptName = "SunHelmFollowerNeeds"sv;

	// Nothing may consume this queue: CHIM might not be installed, or the bridge mod might be
	// disabled. Capping it means an unconsumed queue costs a fixed handful of short strings forever
	// instead of growing for the length of a playthrough. Oldest entries go first because the
	// newest description of a follower is the only one worth sending.
	constexpr std::size_t kMaxPending = 16;

	struct Pending
	{
		std::string actor;
		std::string payload;
	};

	// What the server is told about. Anything not in here can change freely without producing
	// traffic - gold moving by one coin is not worth a prompt rebuild, so it is bucketed below.
	struct Signature
	{
		int  hungerStage{ -1 };
		int  thirstStage{ -1 };
		bool drunk{ false };
		int  drinkBand{ 0 };
		bool diseased{ false };
		int  goldBand{ 0 };

		bool operator==(const Signature&) const = default;
	};

	std::mutex                                 g_mutex;
	std::deque<Pending>                        g_pending;
	std::unordered_map<RE::FormID, Signature>  g_lastSent;

	// Drink counts drive a described state, not a number, and the description only changes at the
	// thresholds Bridge's PHP side uses. Banding here means a follower working through a tankard
	// doesn't republish on every sip.
	int DrinkBand(int a_drinks)
	{
		if (a_drinks >= 7) {
			return 3;
		}
		if (a_drinks >= 4) {
			return 2;
		}
		if (a_drinks >= 2) {
			return 1;
		}
		return 0;
	}

	// Gold is context colour - "can they afford a room" rather than an exact purse. Coarse bands
	// keep a follower buying ale from republishing every few seconds.
	int GoldBand(std::int32_t a_gold)
	{
		if (a_gold >= 500) {
			return 4;
		}
		if (a_gold >= 100) {
			return 3;
		}
		if (a_gold >= 25) {
			return 2;
		}
		if (a_gold >= 1) {
			return 1;
		}
		return 0;
	}

	void Enqueue(std::string a_actor, std::string a_payload)
	{
		if (g_pending.size() >= kMaxPending) {
			g_pending.pop_front();
		}
		g_pending.push_back({ std::move(a_actor), std::move(a_payload) });
	}

	// --- Papyrus natives -----------------------------------------------------------------------
	// Three calls rather than one combined string: Papyrus has no way to return a struct, and
	// splitting a delimited string in script is both slower and easier to get wrong than asking
	// twice. Peek and Consume are separate so a script that fails mid-loop loses one event rather
	// than silently dropping it.

	std::int32_t GetPendingCount(RE::StaticFunctionTag*)
	{
		std::lock_guard lock(g_mutex);
		return static_cast<std::int32_t>(g_pending.size());
	}

	RE::BSFixedString PeekPendingActor(RE::StaticFunctionTag*)
	{
		std::lock_guard lock(g_mutex);
		return g_pending.empty() ? RE::BSFixedString{ "" } : RE::BSFixedString{ g_pending.front().actor.c_str() };
	}

	RE::BSFixedString ConsumePending(RE::StaticFunctionTag*)
	{
		std::lock_guard lock(g_mutex);
		if (g_pending.empty()) {
			return RE::BSFixedString{ "" };
		}
		const auto payload = g_pending.front().payload;
		g_pending.pop_front();
		return RE::BSFixedString{ payload.c_str() };
	}
}

bool Bridge::RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm)
{
	if (!a_vm) {
		return false;
	}
	a_vm->RegisterFunction("GetPendingCount", kScriptName, GetPendingCount);
	a_vm->RegisterFunction("PeekPendingActor", kScriptName, PeekPendingActor);
	a_vm->RegisterFunction("ConsumePending", kScriptName, ConsumePending);
	logger::info("Papyrus bridge registered ({}.psc)", kScriptName);
	return true;
}

void Bridge::NoteFollower(const Followers::State& a_state, RE::Actor& a_actor)
{
	if (!Settings::Get().chimBridge) {
		return;
	}
	if (a_state.name.empty()) {
		return;  // Without a name the server has nothing to key on.
	}

	Signature signature{};
	signature.hungerStage = SunHelm::StageOf(SunHelm::Need::kHunger, a_state.hunger);
	signature.thirstStage = SunHelm::StageOf(SunHelm::Need::kThirst, a_state.thirst);
	signature.drunk = a_state.drunk;
	signature.drinkBand = DrinkBand(a_state.drinksHad);
	signature.diseased = SunHelm::IsDiseased(&a_actor);
	const auto gold = SunHelm::GoldAmount(&a_actor);
	signature.goldBand = GoldBand(gold);

	std::lock_guard lock(g_mutex);
	if (const auto it = g_lastSent.find(a_state.formID); it != g_lastSent.end() && it->second == signature) {
		return;
	}
	g_lastSent[a_state.formID] = signature;

	// Matches the grammar in plugin/ext/sunhelm_needs/lib/sunhelm_followers.php. Positional, and
	// new fields append to the end so an older server ignores what it doesn't recognise instead of
	// failing to parse the whole line.
	Enqueue(a_state.name,
		std::format("sunhelm_follower@{}@{}@{}@{}@{}@{}@{}", a_state.name, signature.hungerStage,
			signature.thirstStage, signature.drunk ? 1 : 0, a_state.drinksHad,
			signature.diseased ? 1 : 0, gold));
}

void Bridge::NoteDismissed(RE::FormID a_formID, std::string_view a_name)
{
	if (!Settings::Get().chimBridge || a_name.empty()) {
		return;
	}

	std::lock_guard lock(g_mutex);
	// Erased whether or not anything was queued: if they are ever re-hired, the first tick should
	// publish them again rather than compare against a signature from a previous engagement.
	g_lastSent.erase(a_formID);
	Enqueue(std::string{ a_name }, std::format("sunhelm_follower_gone@{}", a_name));
}

void Bridge::Reset()
{
	std::lock_guard lock(g_mutex);
	g_pending.clear();
	g_lastSent.clear();
}
