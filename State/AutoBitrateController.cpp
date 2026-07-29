#include "pch.h"
#define MLOG_TAG_OVERRIDE "AutoBitrate"
#include "AutoBitrateController.h"
#include <algorithm>
#include <ppltasks.h>
#include "Utils.hpp"
#include "UI/Utilities/ToastService.h"

using namespace moonlight_xbox_dx;

AutoBitrateController::AutoBitrateController(std::shared_ptr<Stats> stats, MoonlightClient* client, int ceilingKbps, bool autoBitrateEnabled)
	: m_stats(stats),
	  m_client(client),
	  m_autoBitrateEnabled(autoBitrateEnabled),
	  m_ceilingKbps(ceilingKbps),
	  m_floorKbps(std::max(2000, (int)(ceilingKbps * 0.25))),
	  m_currentKbps(ceilingKbps) {
}

void AutoBitrateController::Tick(DX::StepTimer const& timer) {
	double now = timer.GetTotalSeconds();
	if (now - m_lastEvalTime < kEvalIntervalSeconds) {
		return;
	}
	m_lastEvalTime = now;
	Evaluate(now);
}

void AutoBitrateController::Evaluate(double now) {
	if (!m_stats || !m_client) {
		return;
	}

	NETWORK_CONDITION_SNAPSHOT snapshot = m_stats->GetLastWindowSnapshot();
	bool bad = snapshot.lossPercent > kLossPercentThreshold || snapshot.rttVarianceMs > kRttVarianceThresholdMs;

	if (bad) {
		m_consecutiveBadWindows++;
		m_consecutiveGoodWindows = 0;
	} else {
		m_consecutiveGoodWindows++;
		m_consecutiveBadWindows = 0;
	}

	MLOGF(Utils::LogLevel::Debug,
		"Tick: auto=%d current=%dkbps ceiling=%dkbps floor=%dkbps loss=%.2f%% rttVar=%ums bad=%d good=%d support=%d\n",
		m_autoBitrateEnabled, m_currentKbps, m_ceilingKbps, m_floorKbps,
		snapshot.lossPercent, snapshot.rttVarianceMs, m_consecutiveBadWindows, m_consecutiveGoodWindows,
		(int)m_client->GetAbrSupportState());

	if (!m_autoBitrateEnabled) {
		// Manual bitrate: only ever recommend, never act.
		if (m_consecutiveBadWindows >= kBadWindowsToStepDown &&
			(now - m_lastRecommendationToastTime) >= kRecommendationCooldownSeconds) {
			NotifyPoorConnectionRecommendation();
			m_lastRecommendationToastTime = now;
			m_consecutiveBadWindows = 0;
		}
		return;
	}

	if ((now - m_lastChangeTime) < kCooldownSeconds) {
		return;
	}

	if (m_consecutiveBadWindows >= kBadWindowsToStepDown) {
		StepDown(now);
		m_consecutiveBadWindows = 0;
	} else if (m_consecutiveGoodWindows >= kGoodWindowsToStepUp && m_currentKbps < m_ceilingKbps) {
		StepUp(now);
		m_consecutiveGoodWindows = 0;
	}
}

void AutoBitrateController::StepDown(double now) {
	int newKbps = std::max(m_floorKbps, (int)(m_currentKbps * kStepDownFactor));
	if (newKbps == m_currentKbps) {
		return;
	}
	ApplyBitrate(newKbps, now);
}

void AutoBitrateController::StepUp(double now) {
	int newKbps = std::min(m_ceilingKbps, (int)(m_currentKbps * kStepUpFactor));
	if (newKbps == m_currentKbps) {
		return;
	}
	ApplyBitrate(newKbps, now);
}

void AutoBitrateController::ApplyBitrate(int newKbps, double now) {
	AbrSupportState support = m_client->GetAbrSupportState();
	if (support == AbrSupportState::Unknown) {
		// Capability probe still pending; don't guess which path to take, try again next window.
		MLOG(Utils::LogLevel::Debug, "ApplyBitrate: ABR support state still Unknown, deferring change to next window\n");
		return;
	}

	MLOGF(Utils::LogLevel::Info, "ApplyBitrate: %dkbps -> %dkbps (support=%d)\n", m_currentKbps, newKbps, (int)support);

	m_currentKbps = newKbps;
	m_lastChangeTime = now;

	if (support == AbrSupportState::LiveSupported) {
		MoonlightClient* client = m_client;
		concurrency::create_task([client, newKbps]() {
			bool ok = client->TrySetBitrateLive(newKbps);
			MLOGF(Utils::LogLevel::Info, "TrySetBitrateLive(%d) -> %s\n", newKbps, ok ? "ok" : "failed");
		});

		wchar_t buffer[64];
		swprintf_s(buffer, L"Bitrate adjusted to %.1f Mbps", newKbps / 1000.0);
		Platform::String^ message = ref new Platform::String(buffer);
		DISPATCH_UI(([message]() {
			ShowToast(message);
		}));
	} else if (RequestReconnectWithBitrate) {
		DISPATCH_UI(([]() {
			ShowToast(L"Reconnecting to adjust bitrate...");
		}));
		RequestReconnectWithBitrate(newKbps);
	}
}

void AutoBitrateController::NotifyPoorConnectionRecommendation() {
	DISPATCH_UI(([]() {
		ShowToast(L"Poor connection detected - consider lowering your bitrate");
	}));
}
