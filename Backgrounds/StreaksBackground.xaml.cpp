#include "pch.h"
#include "Backgrounds\StreaksBackground.xaml.h"
#include <cmath>

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Shapes;
using namespace Windows::UI::Xaml::Media;

// Rect width for a streak: 2 * halfLen * sqrt(2) so the 45° diagonal spans exactly
// halfLen pixels in each screen axis (the line ends are at cx±halfLen, cy±halfLen).
static const float kSqrt2      = 1.41421356f;
static const float kGlowExtend = 5.0f;   // extra pixels each end of the glow beyond the core
static const int   kStreakCount = 24;
static const int   kStarCount  = 80;
static const int   kGlowBase   = 0;
static const int   kCoreBase   = kStreakCount;
static const int   kStarBase   = kStreakCount * 2;

static const Color kStreakPalette[] = {
    { 255, 255,   0,   0 },  // pure red
    { 255,   0,  60, 255 },  // vivid blue
    { 255, 255,   0, 220 },  // vivid magenta
    { 255, 140,   0, 255 },  // vivid violet
    { 255,   0, 220, 255 },  // vivid cyan
};
static const int kStreakColors = 5;

StreaksBackground::StreaksBackground()
{
    m_rng = std::mt19937(std::random_device{}());
    InitializeComponent();
    StreakCanvas->Background = ref new SolidColorBrush(ColorHelper::FromArgb(255, 3, 3, 15));

    TimeSpan interval;
    interval.Duration = 16 * 10000LL;
    m_timer = ref new DispatcherTimer();
    m_timer->Interval = interval;
    m_timer->Tick += ref new EventHandler<Object^>(this, &StreaksBackground::OnTick);
}

void StreaksBackground::Canvas_SizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
    m_canvasW = static_cast<float>(e->NewSize.Width);
    m_canvasH = static_cast<float>(e->NewSize.Height);

    if (!m_initialized && m_canvasW > 0 && m_canvasH > 0) {
        InitStreaks();
        m_initialized = true;
    }
}

static float ExitT(const StreakState& s, float W, float H)
{
    float txW = 2.0f * W + 2.0f * s.halfLen - s.lane;
    float txH = 2.0f * H + 2.0f * s.halfLen + s.lane;
    return txW > txH ? txW : txH;
}

static float EntryT(const StreakState& s)
{
    return fabsf(s.lane) - 2.0f * s.halfLen;
}

// Make a Rectangle that looks like a diagonal streak.
// The rect is fixed-size with a baked-in 45° RotateTransform — only Canvas.Left/Top
// needs to change each frame, avoiding measure/bounding-box recalculation.
static Rectangle^ MakeStreakRect(float rectW, float rectH, Color col, float opacity)
{
    auto rect = ref new Rectangle();
    rect->Width   = rectW;
    rect->Height  = rectH;
    rect->RadiusX = rectH * 0.5;
    rect->RadiusY = rectH * 0.5;
    rect->Fill    = ref new SolidColorBrush(ColorHelper::FromArgb(255, col.R, col.G, col.B));
    rect->Opacity = opacity;

    auto xf = ref new RotateTransform();
    xf->CenterX = rectW * 0.5;
    xf->CenterY = rectH * 0.5;
    xf->Angle   = 45.0;
    rect->RenderTransform = xf;

    return rect;
}

void StreaksBackground::InitStreaks()
{
    m_streaks.clear();
    m_streaks.reserve(kStreakCount);
    m_stars.clear();
    m_stars.reserve(kStarCount);
    StreakCanvas->Children->Clear();

    float W = m_canvasW, H = m_canvasH;

    std::uniform_real_distribution<float> distLane(-H, W);
    std::uniform_real_distribution<float> distSpeed(3.0f, 9.5f);

    for (int i = 0; i < kStreakCount; ++i) {
        StreakState s;
        s.colorIndex = m_rng() % kStreakColors;
        s.lane       = distLane(m_rng);
        s.halfLen    = 120.0f + static_cast<float>(m_rng() % 380);
        s.speed      = distSpeed(m_rng);
        s.glowH      = 10.0f + static_cast<float>(m_rng() % 22);
        s.coreH      = 2.0f  + static_cast<float>(m_rng() % 3);

        float tMin = EntryT(s);
        float tMax = ExitT(s, W, H);
        std::uniform_real_distribution<float> distT(tMin, tMax);
        s.t = distT(m_rng);

        m_streaks.push_back(s);
    }

    // All glows first so they're below all cores in z-order
    for (int i = 0; i < kStreakCount; ++i) {
        const auto& s = m_streaks[i];
        Color col    = kStreakPalette[s.colorIndex];
        float glowRectW = 2.0f * (s.halfLen + kGlowExtend) * kSqrt2;
        float opacity = 0.55f + static_cast<float>(m_rng() % 45) / 100.0f;

        auto glow = MakeStreakRect(glowRectW, s.glowH, col, opacity);
        float cx = (s.t + s.lane) * 0.5f;
        float cy = (s.t - s.lane) * 0.5f;
        Canvas::SetLeft(glow, cx - glowRectW * 0.5f);
        Canvas::SetTop(glow,  cy - s.glowH   * 0.5f);
        StreakCanvas->Children->Append(glow);
    }

    for (int i = 0; i < kStreakCount; ++i) {
        const auto& s = m_streaks[i];
        Color col   = kStreakPalette[s.colorIndex];
        float rectW = 2.0f * s.halfLen * kSqrt2;
        float opacity = 0.90f + static_cast<float>(m_rng() % 10) / 100.0f;

        auto core = MakeStreakRect(rectW, s.coreH, col, opacity);
        float cx = (s.t + s.lane) * 0.5f;
        float cy = (s.t - s.lane) * 0.5f;
        Canvas::SetLeft(core, cx - rectW     * 0.5f);
        Canvas::SetTop(core,  cy - s.coreH   * 0.5f);
        StreakCanvas->Children->Append(core);
    }

    // Stars — appended last so they render on top of all streaks
    static const float kStarRadii[] = { 5.0f, 5.0f, 6.0f, 7.0f, 8.0f, 10.0f };
    std::uniform_real_distribution<float> distX(0.0f, W);
    std::uniform_real_distribution<float> distY(0.0f, H);
    std::uniform_real_distribution<float> distPhase(0.0f, 6.2832f);
    for (int i = 0; i < kStarCount; ++i) {
        StarState st;
        st.x           = distX(m_rng);
        st.y           = distY(m_rng);
        st.radius      = kStarRadii[m_rng() % 6];
        st.phase       = distPhase(m_rng);
        st.phaseSpeed  = 0.015f + static_cast<float>(m_rng() % 50) / 1000.0f;
        st.baseOpacity = 1.0f;
        m_stars.push_back(st);

        auto el = ref new Ellipse();
        el->Width  = st.radius * 2.0f;
        el->Height = st.radius * 2.0f;
        el->Fill   = ref new SolidColorBrush(ColorHelper::FromArgb(255, 255, 255, 255));
        el->Opacity = 0.85f + 0.15f * sinf(st.phase);
        Canvas::SetLeft(el, st.x - st.radius);
        Canvas::SetTop(el,  st.y - st.radius);
        StreakCanvas->Children->Append(el);
    }
}

void StreaksBackground::OnTick(Object^ sender, Object^ args)
{
    if (!m_initialized) return;

    // Twinkle stars
    for (int i = 0; i < kStarCount; ++i) {
        auto& st = m_stars[i];
        st.phase += st.phaseSpeed;
        auto el = safe_cast<Ellipse^>(StreakCanvas->Children->GetAt(kStarBase + i));
        el->Opacity = 0.85f + 0.15f * sinf(st.phase);
    }

    float W = m_canvasW, H = m_canvasH;
    int count = static_cast<int>(m_streaks.size());

    for (int i = 0; i < count; ++i) {
        auto& s = m_streaks[i];
        s.t += s.speed;

        if (s.t > ExitT(s, W, H)) {
            s.t = EntryT(s) - 50.0f - static_cast<float>(m_rng() % 200);
        }

        float cx       = (s.t + s.lane) * 0.5f;
        float cy       = (s.t - s.lane) * 0.5f;
        float coreRectW = 2.0f * s.halfLen                  * kSqrt2;
        float glowRectW = 2.0f * (s.halfLen + kGlowExtend)  * kSqrt2;

        auto glow = safe_cast<Rectangle^>(StreakCanvas->Children->GetAt(kGlowBase + i));
        Canvas::SetLeft(glow, cx - glowRectW * 0.5f);
        Canvas::SetTop(glow,  cy - s.glowH   * 0.5f);

        auto core = safe_cast<Rectangle^>(StreakCanvas->Children->GetAt(kCoreBase + i));
        Canvas::SetLeft(core, cx - coreRectW * 0.5f);
        Canvas::SetTop(core,  cy - s.coreH   * 0.5f);
    }
}

void StreaksBackground::StartAnimations()
{
    if (m_timer != nullptr) m_timer->Start();
}

void StreaksBackground::StopAnimations()
{
    if (m_timer != nullptr) m_timer->Stop();
}
