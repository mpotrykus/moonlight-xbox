#pragma once
#include "Backgrounds\SpheresBackground.g.h"
#include <vector>
#include <random>

namespace moonlight_xbox_dx {

struct SphereState {
    float x, y;
    float vx, vy;
    float radius;
    float opacity;
    int   colorIndex;
};

public ref class SpheresBackground sealed {
public:
    SpheresBackground();
    void StartAnimations();
    void StopAnimations();
private:
    Windows::UI::Xaml::DispatcherTimer^ m_timer;
    std::vector<SphereState> m_spheres;
    float m_canvasW = 0;
    float m_canvasH = 0;
    bool  m_initialized = false;
	std::mt19937 m_rng;
	Windows::UI::Color m_colorA; // system accent color
	Windows::UI::Color m_colorB; // darkened accent, derived in constructor

    void Canvas_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
    void OnTick(Platform::Object^ sender, Platform::Object^ args);
    void InitSpheres();
};

}
