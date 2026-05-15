#include "pch.h"
#include "Backgrounds\WavyLinesBackground.xaml.h"
#include <cmath>

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Shapes;
using namespace Windows::UI::Xaml::Media;

static const float kPi    = 3.14159265f;
static const float kTwoPi = 6.28318530f;

static const WavyRibbon kRibbonDefs[] = {
    // Top-left: sweeps right-down, crimson → violet
    { 0.23f, 0.18f,  0.55f, 1.30f,
      95.f, 1.70f, 0.0075f,   35.f, 3.30f, -0.0055f,
      25.f, 0.00320f,
      7.0f,
      255, 18, 50,    210, 45, 255,
      0, 0, 0, 0 },
    // Top-right: sweeps left-down, blue → hot-pink
    { 0.80f, 0.20f,  kPi - 0.50f, 1.20f,
      88.f, 1.90f, -0.0070f,   30.f, 3.80f, 0.0050f,
      22.f, 0.00280f,
      7.0f,
      45, 55, 255,    255, 45, 195,
      0, 0, 0, 0 },
    // Bottom-left: sweeps right-up, deep-red → pink-magenta
    { 0.20f, 0.82f, -0.35f, 1.05f,
      80.f, 1.60f,  0.0085f,   28.f, 3.10f, -0.0042f,
      20.f, 0.00350f,
      7.0f,
      255, 22, 75,    255, 75, 225,
      0, 0, 0, 0 },
    // Bottom-right: sweeps left-up, violet → magenta
    { 0.80f, 0.80f,  kPi + 0.30f, 1.00f,
      82.f, 1.85f, -0.0078f,   26.f, 3.60f, 0.0058f,
      20.f, 0.00300f,
      7.0f,
      70, 35, 255,    215, 28, 195,
      0, 0, 0, 0 },
};
static const int kRibbonCount = 4;

static inline float lerpF(float a, float b, float t) { return a + (b - a) * t; }

WavyLinesBackground::WavyLinesBackground()
{
    InitializeComponent();
    WavyCanvas->Background = ref new SolidColorBrush(ColorHelper::FromArgb(255, 5, 5, 18));

    TimeSpan interval;
    interval.Duration = 33 * 10000LL;  // ~30 fps
    m_timer = ref new DispatcherTimer();
    m_timer->Interval = interval;
    m_timer->Tick += ref new EventHandler<Object^>(this, &WavyLinesBackground::OnTick);
}

void WavyLinesBackground::Canvas_SizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
    m_canvasW = static_cast<float>(e->NewSize.Width);
    m_canvasH = static_cast<float>(e->NewSize.Height);

    if (!m_initialized && m_canvasW > 0 && m_canvasH > 0) {
        InitRibbons();
        m_initialized = true;
    }
}

static Path^ MakeBezierPath(Color col, float strokeW, float opacity)
{
    auto seg = ref new PolyBezierSegment();
    for (int i = 0; i < kBezierPts; ++i)
        seg->Points->Append(Point(0.0f, 0.0f));

    auto figure = ref new PathFigure();
    figure->IsClosed = false;
    figure->Segments->Append(seg);

    auto geom = ref new PathGeometry();
    geom->Figures->Append(figure);

    auto path = ref new Path();
    path->Data            = geom;
    path->Stroke          = ref new SolidColorBrush(col);
    path->StrokeThickness = strokeW;
    path->Opacity         = opacity;
    path->StrokeLineJoin  = PenLineJoin::Round;
    path->StrokeStartLineCap = PenLineCap::Round;
    path->StrokeEndLineCap   = PenLineCap::Round;
    return path;
}

void WavyLinesBackground::InitRibbons()
{
    m_ribbons.clear();
    m_figures.clear();
    m_points.clear();
    WavyCanvas->Children->Clear();

    int nextPath = 0;
    for (int ri = 0; ri < kRibbonCount; ++ri) {
        WavyRibbon r = kRibbonDefs[ri];
        r.phase1     = 0.0f;
        r.phase2     = static_cast<float>(ri) * 1.1f;
        r.driftPhase = static_cast<float>(ri) * 0.8f;
        r.pathStart  = nextPath;

        float ribbonW = (kVisibleLines - 1) * r.lineSpacing;
        float midLine = (kVisibleLines - 1) * 0.5f;
        Color centerCol;
        centerCol.A = 255;
        centerCol.R = static_cast<uint8_t>(lerpF(r.r0, r.r1, 0.5f));
        centerCol.G = static_cast<uint8_t>(lerpF(r.g0, r.g1, 0.5f));
        centerCol.B = static_cast<uint8_t>(lerpF(r.b0, r.b1, 0.5f));

        // Glow layer 0: wide diffuse halo
        auto g0 = MakeBezierPath(centerCol, ribbonW * 1.4f, 0.07f);
        WavyCanvas->Children->Append(g0);
        auto gf0 = safe_cast<PathFigure^>(safe_cast<PathGeometry^>(g0->Data)->Figures->GetAt(0));
        m_figures.push_back(gf0);
        m_points.push_back(safe_cast<PolyBezierSegment^>(gf0->Segments->GetAt(0))->Points);

        // Glow layer 1: tighter inner glow
        auto g1 = MakeBezierPath(centerCol, ribbonW * 0.65f, 0.13f);
        WavyCanvas->Children->Append(g1);
        auto gf1 = safe_cast<PathFigure^>(safe_cast<PathGeometry^>(g1->Data)->Figures->GetAt(0));
        m_figures.push_back(gf1);
        m_points.push_back(safe_cast<PolyBezierSegment^>(gf1->Segments->GetAt(0))->Points);

        // Core lines
        for (int li = 0; li < kVisibleLines; ++li) {
            float t      = (kVisibleLines > 1) ? (float)li / (kVisibleLines - 1) : 0.5f;
            float norm   = (li - midLine) / (midLine > 0 ? midLine : 1.0f);  // -1..+1
            float bell   = 1.0f - norm * norm;

            Color col;
            col.A = 255;
            col.R = static_cast<uint8_t>(lerpF(r.r0, r.r1, t));
            col.G = static_cast<uint8_t>(lerpF(r.g0, r.g1, t));
            col.B = static_cast<uint8_t>(lerpF(r.b0, r.b1, t));

            float strokeW = lerpF(0.9f, 1.6f, bell);
            float opacity = lerpF(0.20f, 0.80f, bell);

            auto path = MakeBezierPath(col, strokeW, opacity);
            WavyCanvas->Children->Append(path);
            auto fig = safe_cast<PathFigure^>(safe_cast<PathGeometry^>(path->Data)->Figures->GetAt(0));
            m_figures.push_back(fig);
            m_points.push_back(safe_cast<PolyBezierSegment^>(fig->Segments->GetAt(0))->Points);
        }

        nextPath += kPathsPerRibbon;
        m_ribbons.push_back(r);
    }
}

void WavyLinesBackground::UpdateRibbon(WavyRibbon& r)
{
    r.phase1     += r.spd1;
    r.phase2     += r.spd2;
    r.driftPhase += r.driftSpd;

    float cx    = r.anchorFX * m_canvasW + r.driftR * cosf(r.driftPhase);
    float cy    = r.anchorFY * m_canvasH + r.driftR * sinf(r.driftPhase + 1.1f);
    float cosA  = cosf(r.angle);
    float sinA  = sinf(r.angle);
    float perpX = -sinA;
    float perpY =  cosA;
    float flowLen  = r.flowLenMult * m_canvasW;
    float dt       = 1.0f / kBezierSegs;
    float dtOver3  = dt / 3.0f;
    float lineMid  = (kVisibleLines - 1) * 0.5f;

    // Precompute knots once — all paths share the same wave, just shifted perpendicularly
    float baseX[kKnots], baseY[kKnots];
    float waveV[kKnots], tangX[kKnots], tangY[kKnots];
    for (int k = 0; k < kKnots; ++k) {
        float t   = k * dt;
        float w   = r.amp1 * sinf(r.freq1 * t * kTwoPi + r.phase1)
                  + r.amp2 * sinf(r.freq2 * t * kTwoPi + r.phase2);
        float wd  = r.amp1 * r.freq1 * kTwoPi * cosf(r.freq1 * t * kTwoPi + r.phase1)
                  + r.amp2 * r.freq2 * kTwoPi * cosf(r.freq2 * t * kTwoPi + r.phase2);
        baseX[k]  = cx + (t - 0.5f) * flowLen * cosA;
        baseY[k]  = cy + (t - 0.5f) * flowLen * sinA;
        waveV[k]  = w;
        tangX[k]  = flowLen * cosA + wd * perpX;
        tangY[k]  = flowLen * sinA + wd * perpY;
    }

    for (int j = 0; j < kPathsPerRibbon; ++j) {
        float perpOff = (j < 2) ? 0.0f : ((j - 2 - lineMid) * r.lineSpacing);

        auto& fig = m_figures[r.pathStart + j];
        auto& pts = m_points[r.pathStart + j];

        float d0 = waveV[0] + perpOff;
        fig->StartPoint = Point(baseX[0] + d0 * perpX, baseY[0] + d0 * perpY);

        for (int seg = 0; seg < kBezierSegs; ++seg) {
            float d_k0 = waveV[seg]     + perpOff;
            float d_k1 = waveV[seg + 1] + perpOff;
            int   base = seg * 3;
            pts->SetAt(base + 0, Point(baseX[seg]     + d_k0 * perpX + tangX[seg]     * dtOver3,
                                       baseY[seg]     + d_k0 * perpY + tangY[seg]     * dtOver3));
            pts->SetAt(base + 1, Point(baseX[seg + 1] + d_k1 * perpX - tangX[seg + 1] * dtOver3,
                                       baseY[seg + 1] + d_k1 * perpY - tangY[seg + 1] * dtOver3));
            pts->SetAt(base + 2, Point(baseX[seg + 1] + d_k1 * perpX,
                                       baseY[seg + 1] + d_k1 * perpY));
        }
    }
}

void WavyLinesBackground::OnTick(Object^ sender, Object^ args)
{
    if (!m_initialized) return;
    try {
        for (auto& r : m_ribbons)
            UpdateRibbon(r);
    } catch (...) {}
}

void WavyLinesBackground::StartAnimations()
{
    if (m_timer != nullptr) m_timer->Start();
}

void WavyLinesBackground::StopAnimations()
{
    if (m_timer != nullptr) m_timer->Stop();
}
