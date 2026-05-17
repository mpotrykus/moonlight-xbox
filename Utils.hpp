#pragma once
#include "pch.h"

namespace moonlight_xbox_dx {
	namespace Utils {
		extern std::vector<std::wstring> logLines;
		extern bool showLogs;
		extern bool showStats;
		extern std::mutex logMutex;

		Platform::String^ StringPrintf(const char* fmt, ...);

		void Log(const char* msg);
		void Log(const std::string_view& msg);
		void Logf(const char* msg, ...);

		std::vector<std::wstring> GetLogLines();
		Platform::String^ StringFromChars(const char* chars);
		Platform::String^ StringFromStdString(std::string st);
		std::string PlatformStringToStdString(Platform::String^ input);
		std::string WideToNarrowString(const std::wstring_view& str);
		std::wstring NarrowToWideString(const std::string_view& str);

		// Inserts the given color as SystemAccentColor into Application.Resources
		// ThemeDictionaries so that {ThemeResource SystemAccentColor} picks it up
		// app-wide. Call this whenever a per-host accent should become active.
		void ApplyAccentColor(Windows::UI::Color color);
	}
}
