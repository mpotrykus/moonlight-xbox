#include "pch.h"
#include "Backgrounds\PaperCutBackground.xaml.h"
#include <cmath>
#include <algorithm>

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Shapes;
using namespace Windows::UI::Xaml::Media;

static const float kPiPC = 3.14159265358979f;

// Paper-cut palette (A, R, G, B)
static const Color kPCBg     = { 255,  12,  20,  60 };
static const Color kPCCyan   = { 255,  22, 180, 200 };
static const Color kPCPink   = { 255, 240,  44, 110 };
static const Color kPCOrange = { 255, 240, 130,  50 };

// Layer colors: outermost (drawn first) to innermost (drawn last on top)
static const Color kPCLayerColors[kPCLayers] = {
    kPCOrange,  // layer 0: large blob just inside the base teal circle
    kPCPink,    // layer 1
    kPCCyan,    // layer 2
    kPCOrange,  // layer 3
    kPCBg,      // layer 4: innermost, matches background — creates a depth hole
};

static const Color kPCDotColors[kPCDots] = {
    kPCPink, kPCPink, kPCOrange, kPCCyan, kPCPink, kPCOrange
};

PaperCutBackground::PaperCutBackground()
{
    m_rng = std::mt19937(std::random_device{}());
    InitializeComponent();

    PaperCanvas->Background = ref new SolidColorBrush(
        ColorHelper::FromArgb(kPCBg.A, kPCBg.R, kPCBg.G, kPCBg.B));

    TimeSpan interval;
    interval.Duration = 33 * 10000LL;  // ~30 fps
    m_timer = ref new DispatcherTimer();
    m_timer->Interval = interval;
    m_timer->Tick += ref new EventHandler<Object^>(this, &PaperCutBackground::OnTick);
}

void PaperCutBackground::Canvas_SizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
    m_canvasW = static_cast<float>(e->NewSize.Width);
    m_canvasH = static_cast<float>(e->NewSize.Height);
    if (!m_initialized && m_canvasW > 0 && m_canvasH > 0) {
        InitScene();
        m_initialized = true;
    }
}

void PaperCutBackground::InitScene()
{
    PaperCanvas->Children->Clear();
    m_planets.clear();
    m_planets.reserve(kPCPlanets);

    std::uniform_real_distribution<float> distPhase(0.0f, kPiPC * 2.0f);
    std::uniform_real_distribution<float> distRFreq(0.006f, 0.016f);
    std::uniform_real_distribution<float> distAFreq(0.004f, 0.012f);
    std::uniform_real_distribution<float> distOrbitSp(0.005f, 0.013f);
    std::uniform_real_distribution<float> distMorphSp(0.4f, 0.9f);

    // Planet definitions: {cx fraction, cy fraction, radius as fraction of min(W,H)}
    float defs[kPCPlanets][3] = {
        { 0.75f, 0.42f, 0.30f },  // large, right-center
        { 0.20f, 0.74f, 0.24f },  // medium, bottom-left
        { 0.14f, 0.12f, 0.13f },  // small, top-left
    };

    float minDim = std::min(m_canvasW, m_canvasH);

    for (int pi = 0; pi < kPCPlanets; pi++) {
        PCPlanet planet{};
        planet.cx     = defs[pi][0] * m_canvasW;
        planet.cy     = defs[pi][1] * m_canvasH;
        planet.radius = defs[pi][2] * minDim;

        // Base teal circle — provides the outer circular boundary appearance
        auto baseE = ref new Ellipse();
        baseE->Width  = planet.radius * 2.0f;
        baseE->Height = planet.radius * 2.0f;
        baseE->Fill   = ref new SolidColorBrush(
            ColorHelper::FromArgb(kPCCyan.A, kPCCyan.R, kPCCyan.G, kPCCyan.B));
        Canvas::SetLeft(baseE, planet.cx - planet.radius);
        Canvas::SetTop(baseE,  planet.cy - planet.radius);
        PaperCanvas->Children->Append(baseE);

        // Blob layers from outermost to innermost
        float layerFracs[kPCLayers] = { 0.86f, 0.70f, 0.55f, 0.40f, 0.24f };

        for (int li = 0; li < kPCLayers; li++) {
            PCLayer& layer = planet.layers[li];
            layer.baseRadius  = planet.radius * layerFracs[li];
            layer.orbitRadius = planet.radius * 0.09f;
            layer.orbitAngle  = distPhase(m_rng);
            // Alternate orbit direction per layer for more organic relative motion
            layer.orbitSpeed  = distOrbitSp(m_rng) * (li % 2 == 0 ? 1.0f : -1.1f);
            layer.morphTime   = distPhase(m_rng) * 30.0f;
            layer.morphSpeed  = distMorphSp(m_rng);

            // Inner layers get proportionally more wobble for a chaotic topographic look
            float rAmpMax = layer.baseRadius * (0.18f + li * 0.04f);

            for (int j = 0; j < kPCPts; j++) {
                std::uniform_real_distribution<float> distRAmp(rAmpMax * 0.6f, rAmpMax);
                std::uniform_real_distribution<float> distAAmp(
                    0.20f + li * 0.05f,
                    0.50f + li * 0.06f);
                layer.rAmp[j]   = distRAmp(m_rng);
                layer.rPhase[j] = distPhase(m_rng);
                layer.rFreq[j]  = distRFreq(m_rng);
                layer.aAmp[j]   = distAAmp(m_rng);
                layer.aFreq[j]  = distAFreq(m_rng);
                layer.aPhase[j] = distPhase(m_rng);
            }

            layer.s0 = ref new BezierSegment();
            layer.s1 = ref new BezierSegment();
            layer.s2 = ref new BezierSegment();
            layer.s3 = ref new BezierSegment();
            layer.s4 = ref new BezierSegment();
            layer.s5 = ref new BezierSegment();

            auto fig = ref new PathFigure();
            fig->IsClosed = true;
            fig->IsFilled = true;
            fig->Segments->Append(layer.s0);
            fig->Segments->Append(layer.s1);
            fig->Segments->Append(layer.s2);
            fig->Segments->Append(layer.s3);
            fig->Segments->Append(layer.s4);
            fig->Segments->Append(layer.s5);
            layer.figure = fig;

            auto geom = ref new PathGeometry();
            geom->Figures->Append(fig);

            auto path = ref new Path();
            path->Data = geom;
            const Color& c = kPCLayerColors[li];
            path->Fill = ref new SolidColorBrush(ColorHelper::FromArgb(c.A, c.R, c.G, c.B));
            PaperCanvas->Children->Append(path);
        }

        m_planets.push_back(planet);

        for (int li = 0; li < kPCLayers; li++) {
            UpdateLayer(m_planets.back(), m_planets.back().layers[li]);
        }
    }

    // Small solid dots scattered in the open background areas
    float dotX[kPCDots]  = { 0.06f, 0.42f, 0.35f, 0.88f, 0.62f, 0.52f };
    float dotY[kPCDots]  = { 0.20f, 0.28f, 0.52f, 0.74f, 0.92f, 0.62f };
    float dotSz[kPCDots] = { 20.0f, 34.0f, 24.0f, 30.0f, 18.0f, 26.0f };

    for (int i = 0; i < kPCDots; i++) {
        auto dot = ref new Ellipse();
        dot->Width  = dotSz[i];
        dot->Height = dotSz[i];
        const Color& c = kPCDotColors[i];
        dot->Fill = ref new SolidColorBrush(ColorHelper::FromArgb(c.A, c.R, c.G, c.B));
        Canvas::SetLeft(dot, dotX[i] * m_canvasW - dotSz[i] * 0.5f);
        Canvas::SetTop(dot,  dotY[i] * m_canvasH - dotSz[i] * 0.5f);
        PaperCanvas->Children->Append(dot);
    }
}

void PaperCutBackground::UpdateLayer(PCPlanet& planet, PCLayer& layer)
{
    layer.morphTime  += layer.morphSpeed;
    layer.orbitAngle += layer.orbitSpeed;

    // Blob center slowly orbits the planet center for organic relative motion
    float ocx = planet.cx + layer.orbitRadius * cosf(layer.orbitAngle);
    float ocy = planet.cy + layer.orbitRadius * sinf(layer.orbitAngle);

    float px[kPCPts], py[kPCPts];
    for (int i = 0; i < kPCPts; i++) {
        float baseAngle = i * (2.0f * kPiPC / kPCPts);
        float angle = baseAngle + layer.aAmp[i] * sinf(layer.morphTime * layer.aFreq[i] + layer.aPhase[i]);
        float r     = layer.baseRadius + layer.rAmp[i] * sinf(layer.morphTime * layer.rFreq[i] + layer.rPhase[i]);
        px[i] = ocx + r * cosf(angle);
        py[i] = ocy + r * sinf(angle);
    }

    // Catmull-Rom to cubic Bezier (tension 1/6)
    auto cp1x = [&](int i) { return px[i] + (px[(i+1)%kPCPts] - px[(i-1+kPCPts)%kPCPts]) / 6.0f; };
    auto cp1y = [&](int i) { return py[i] + (py[(i+1)%kPCPts] - py[(i-1+kPCPts)%kPCPts]) / 6.0f; };
    auto cp2x = [&](int i) { return px[(i+1)%kPCPts] - (px[(i+2)%kPCPts] - px[i]) / 6.0f; };
    auto cp2y = [&](int i) { return py[(i+1)%kPCPts] - (py[(i+2)%kPCPts] - py[i]) / 6.0f; };

    layer.figure->StartPoint = Point(px[0], py[0]);

    BezierSegment^ segs[kPCPts] = { layer.s0, layer.s1, layer.s2, layer.s3, layer.s4, layer.s5 };
    for (int i = 0; i < kPCPts; i++) {
        int ni = (i + 1) % kPCPts;
        segs[i]->Point1 = Point(cp1x(i), cp1y(i));
        segs[i]->Point2 = Point(cp2x(i), cp2y(i));
        segs[i]->Point3 = Point(px[ni],   py[ni]);
    }
}

void PaperCutBackground::OnTick(Object^ sender, Object^ args)
{
    if (!m_initialized) return;
    for (auto& planet : m_planets) {
        for (int li = 0; li < kPCLayers; li++) {
            UpdateLayer(planet, planet.layers[li]);
        }
    }
}

void PaperCutBackground::StartAnimations()  { if (m_timer) m_timer->Start(); }
void PaperCutBackground::StopAnimations()   { if (m_timer) m_timer->Stop();  }
