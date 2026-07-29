#pragma once

#include "pch.h"
#include <memory>
#include <functional>
#include "Common\StepTimer.h"
#include "State\Stats.h"
#include "State\MoonlightClient.h"

namespace moonlight_xbox_dx
{
	// Runs on the render loop thread (ticked from moonlight_xbox_dxMain::Update()), reading
	// per-second network condition snapshots from Stats and deciding whether to adjust the
	// streaming bitrate. All network-triggering work is dispatched off this thread.
	class AutoBitrateController
	{
	public:
		AutoBitrateController(std::shared_ptr<Stats> stats, MoonlightClient* client, int ceilingKbps, bool autoBitrateEnabled);

		// Called once per frame; self-gates to its own evaluation cadence.
		void Tick(DX::StepTimer const& timer);

		// Set by the owner (moonlight_xbox_dxMain) to the reconnect fallback used when the
		// connected host has no live bitrate renegotiation support.
		std::function<void(int)> RequestReconnectWithBitrate;

	private:
		void Evaluate(double nowSeconds);
		void StepDown(double nowSeconds);
		void StepUp(double nowSeconds);
		void ApplyBitrate(int newKbps, double nowSeconds);
		void NotifyPoorConnectionRecommendation();

		std::shared_ptr<Stats> m_stats;
		MoonlightClient* m_client;

		bool m_autoBitrateEnabled;
		int m_ceilingKbps;
		int m_floorKbps;
		int m_currentKbps;

		double m_lastEvalTime = 0.0;
		double m_lastChangeTime = 0.0;
		double m_lastRecommendationToastTime = -1000.0;
		int m_consecutiveBadWindows = 0;
		int m_consecutiveGoodWindows = 0;

		static constexpr double kEvalIntervalSeconds = 2.0;
		static constexpr double kCooldownSeconds = 5.0;
		static constexpr double kRecommendationCooldownSeconds = 30.0;
		static constexpr int kBadWindowsToStepDown = 2;
		static constexpr int kGoodWindowsToStepUp = 8;
		static constexpr double kLossPercentThreshold = 2.0;
		static constexpr double kRttVarianceThresholdMs = 40.0;
		static constexpr double kStepDownFactor = 0.80;
		static constexpr double kStepUpFactor = 1.10;
	};
}
