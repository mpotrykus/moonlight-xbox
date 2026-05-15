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
    { L"wavylines", L"Wavy Lines"         },
    { L"appgrid",     L"App Gallery"        },
    { L"cornerboom",  L"Corner Bloom"       },
    { L"blobs",       L"Morphing Blobs"     },
    { L"moonnight",   L"Moon & Stars"       },
    { L"papercut",    L"Paper Cut"          },
    { L"swipereveal", L"Swipe Reveal"       },
};

static const int kBackgroundCount = 13;

}
