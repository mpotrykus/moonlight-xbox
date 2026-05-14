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
    { L"bubbles",   L"Dancing Bubbles"    },
    { L"spheres",   L"Cell-Shaded Spheres"},
    { L"streaks",   L"Neon Streaks"       },
};

static const int kBackgroundCount = 6;

}
