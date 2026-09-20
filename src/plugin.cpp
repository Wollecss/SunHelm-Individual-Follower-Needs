#include "DevBenchTools.h"
#include "Followers.h"
#include "SunHelm.h"
#include "UI.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace
{
	void InitializeLogging()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			return;
		}
		*path /= "SunHelmFollowerNeeds.log";

		// Keep one previous session around. Truncating on boot destroys exactly the log you need
		// after a crash, since the next launch wipes it before you can read it.
		std::error_code ec;
		auto previous = *path;
		previous.replace_extension(".prev.log");
		std::filesystem::rename(*path, previous, ec);

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("Global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		switch (a_message->type) {
		case SKSE::MessagingInterface::kPostLoad:
			// Menu registration resolves the framework's exports via GetModuleHandle, so it has to
			// run after all SKSE plugins are loaded. The framework header caches each resolved
			// function pointer in a function-local static, so resolving too early would cache a
			// permanent nullptr and silently disable the UI for the whole session.
			UI::Register();
			// DevBench's own interface-fetch dispatch requires all plugins to already be loaded,
			// same reason as the menu registration above.
			DevBenchTools::Register();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			// Forms only exist once all plugins are loaded. This also fires at the main menu,
			// where there is no loaded world - form lookup is fine there, touching actors is not.
			if (SunHelm::Resolve()) {
				Followers::Start();
			}
			break;
		case SKSE::MessagingInterface::kNewGame:
			Followers::Reset();
			Followers::SetGameReady(true);
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			Followers::SetGameReady(true);
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			// Clearing here keeps one save's followers from leaking into the next. Until state is
			// persisted in the quest's aliases, a loaded save simply re-detects who is with you.
			Followers::SetGameReady(false);
			Followers::Reset();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	InitializeLogging();

	logger::info("SunHelm Follower Needs v{} loaded", SKSE::PluginDeclaration::GetSingleton()->GetVersion().string());

	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
