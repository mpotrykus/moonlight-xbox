#pragma once

namespace moonlight_xbox_dx {

struct BackgroundEntry {
    const wchar_t* key;
    const wchar_t* displayName;
};

static const BackgroundEntry kBackgrounds[] = {
    { L"none",      L"None"               },
    { L"gradient",  L"Animated Gradient"  },
    { L"particles", L"Floating Particles" },
};

static const int kBackgroundCount = 3;

}
