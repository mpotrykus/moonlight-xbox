#pragma once
#include "Backgrounds\ParticleBackground.g.h"
#include <vector>
#include <random>

namespace moonlight_xbox_dx {

struct ParticleState {
    float t;           // stream position 0..1 (x = t * canvasW)
    float tSpeed;
    float spreadY;     // perpendicular offset from wave (pixels)
    float opacity;
    float opacityDelta;
    float opacityMin;
    float opacityMax;
    float size;
    bool isBokeh;
};

public ref class ParticleBackground sealed {
public:
    ParticleBackground();
    void StartAnimations();
    void StopAnimations();
private:
    Windows::UI::Xaml::DispatcherTimer^ m_timer;
    Windows::Foundation::EventRegistrationToken m_tickToken;
    std::vector<ParticleState> m_particles;
    float m_canvasW = 0;
    float m_canvasH = 0;
    float m_wavePhase = 0.0f;
    bool m_initialized = false;
    std::mt19937 m_rng;
    Windows::UI::Color m_colorA;  // system accent color
    Windows::UI::Color m_colorB;  // darkened accent, derived in constructor

    void Canvas_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
    void OnTick(Platform::Object^ sender, Platform::Object^ args);
    void InitParticles();
    float WaveY(float t);
};

}
