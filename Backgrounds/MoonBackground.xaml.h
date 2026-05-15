#pragma once
#include "Backgrounds\MoonBackground.g.h"
#include <vector>
#include <random>

namespace moonlight_xbox_dx {

struct StarState {
    float x, y;
    float radius;
    float baseOpacity;
    float phase;
    float phaseSpeed;
};

struct CloudState {
    float x, y;
    float width, height;
    float opacity;
    float speed;
};

public ref class MoonBackground sealed {
public:
    MoonBackground();
    void StartAnimations();
    void StopAnimations();
private:
    Windows::UI::Xaml::DispatcherTimer^ m_timer;
    std::vector<StarState>  m_stars;
    std::vector<CloudState> m_clouds;
    float m_canvasW = 0;
    float m_canvasH = 0;
    bool  m_initialized = false;
    std::mt19937 m_rng;

    void Canvas_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
    void OnTick(Platform::Object^ sender, Platform::Object^ args);
    void InitScene();
};

}
