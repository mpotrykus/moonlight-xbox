#pragma once
#include "Backgrounds\WavyLinesBackground.g.h"
#include <vector>

namespace moonlight_xbox_dx {

static const int kBezierSegs     = 6;
static const int kKnots          = kBezierSegs + 1;   // 7 evaluation points
static const int kBezierPts      = kBezierSegs * 3;   // 18 control pts per PolyBezierSegment
static const int kVisibleLines   = 6;
static const int kPathsPerRibbon = 2 + kVisibleLines; // 2 glow + 6 lines = 8

struct WavyRibbon {
    float anchorFX, anchorFY;
    float angle, flowLenMult;
    float amp1, freq1, spd1;
    float amp2, freq2, spd2;
    float driftR, driftSpd;
    float lineSpacing;
    float r0, g0, b0;
    float r1, g1, b1;
    // live state
    float phase1, phase2, driftPhase;
    int   pathStart;
};

public ref class WavyLinesBackground sealed {
public:
    WavyLinesBackground();
    void StartAnimations();
    void StopAnimations();
private:
    Windows::UI::Xaml::DispatcherTimer^                     m_timer;
    std::vector<WavyRibbon>                                 m_ribbons;
    std::vector<Windows::UI::Xaml::Media::PathFigure^>      m_figures;
    std::vector<Windows::UI::Xaml::Media::PointCollection^> m_points;
    float m_canvasW  = 0;
    float m_canvasH  = 0;
    bool  m_initialized = false;

    void Canvas_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
    void OnTick(Platform::Object^ sender, Platform::Object^ args);
    void InitRibbons();
    void UpdateRibbon(WavyRibbon& r);
};

}
