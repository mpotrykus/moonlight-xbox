#pragma once
#include "Backgrounds\OrbsBackground.g.h"
#include <vector>

namespace moonlight_xbox_dx {

static const int kOrbCount   = 8;
static const int kOrbTailLen = 48;

struct OrbState {
    float cx, cy;               // orbit center
    float rx, ry;               // orbit semi-axes
    float tilt;                 // orbit rotation angle (radians)
    float angle;                // current orbital angle
    float speed;                // radians per frame
    float tailX[kOrbTailLen];
    float tailY[kOrbTailLen];
};

public ref class OrbsBackground sealed {
public:
    OrbsBackground();
    void StartAnimations();
    void StopAnimations();
    void ReloadColors();
private:
    Windows::UI::Color m_palette[2];
    Windows::UI::Xaml::DispatcherTimer^ m_timer;
    Windows::Foundation::EventRegistrationToken m_tickToken;
    std::vector<OrbState> m_orbs;
    float m_canvasW     = 0;
    float m_canvasH     = 0;
    bool  m_initialized = false;

    // Dance <-> circle blend state (0 = full dance, 1 = full circle)
    float m_lerpT       = 0.0f;
    float m_targetLerpT = 0.0f;
    int   m_holdTicks   = 0;
    float m_circleAngle = 0.0f;

    void Canvas_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
    void OnTick(Platform::Object^ sender, Platform::Object^ args);
    void OnLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
    void InitOrbs();
    void LoadPalette();
};

}
