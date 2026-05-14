#pragma once
#include "Backgrounds\ParticleBackground.g.h"
#include <vector>
#include <random>

namespace moonlight_xbox_dx {

struct ParticleState {
    float x, y;
    float vx, vy;
    float opacity;
    float opacityDelta;
    float size;
};

public ref class ParticleBackground sealed {
public:
    ParticleBackground();
    void StartAnimations();
    void StopAnimations();
private:
    Windows::UI::Xaml::DispatcherTimer^ m_timer;
    std::vector<ParticleState> m_particles;
    float m_canvasW = 0;
    float m_canvasH = 0;
    bool m_initialized = false;
    std::mt19937 m_rng;

    void Canvas_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
    void OnTick(Platform::Object^ sender, Platform::Object^ args);
    void InitParticles();
};

}
