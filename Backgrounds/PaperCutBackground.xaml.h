#pragma once
#include "Backgrounds\PaperCutBackground.g.h"
#include <vector>
#include <random>

namespace moonlight_xbox_dx {

static const int kPCPts    = 6;
static const int kPCLayers = 5;
static const int kPCPlanets = 3;
static const int kPCDots   = 6;

struct PCLayer {
    float baseRadius;
    float orbitRadius;
    float orbitAngle;
    float orbitSpeed;
    float morphTime;
    float morphSpeed;
    float rAmp[kPCPts];
    float rPhase[kPCPts];
    float rFreq[kPCPts];
    float aAmp[kPCPts];
    float aFreq[kPCPts];
    float aPhase[kPCPts];
    Windows::UI::Xaml::Media::PathFigure^    figure;
    Windows::UI::Xaml::Media::BezierSegment^ s0;
    Windows::UI::Xaml::Media::BezierSegment^ s1;
    Windows::UI::Xaml::Media::BezierSegment^ s2;
    Windows::UI::Xaml::Media::BezierSegment^ s3;
    Windows::UI::Xaml::Media::BezierSegment^ s4;
    Windows::UI::Xaml::Media::BezierSegment^ s5;
};

struct PCPlanet {
    float cx, cy, radius;
    PCLayer layers[kPCLayers];
};

public ref class PaperCutBackground sealed {
public:
    PaperCutBackground();
    void StartAnimations();
    void StopAnimations();
private:
    Windows::UI::Xaml::DispatcherTimer^ m_timer;
    std::vector<PCPlanet>               m_planets;
    float m_canvasW     = 0;
    float m_canvasH     = 0;
    bool  m_initialized = false;
    std::mt19937 m_rng;

    void Canvas_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
    void OnTick(Platform::Object^ sender, Platform::Object^ args);
    void InitScene();
    void UpdateLayer(PCPlanet& planet, PCLayer& layer);
};

} // namespace moonlight_xbox_dx
