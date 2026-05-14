#include "pch.h"
#include "Backgrounds\BubblesBackground.xaml.h"
#include <cmath>

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Shapes;
using namespace Windows::UI::Xaml::Media;

static const int   kBubbleCount = 16;
static const int   kColorCount  = 5;
static const int   kElemsPerBubble = 3;  // glow, body, specular

// Neon palette: electric blue, indigo, deep violet, violet-magenta, sky blue
// Color fields: { A, R, G, B }
static const Color kPalette[kColorCount] = {
    { 255,  30,  90, 255 },
    { 255,  90,  50, 220 },
    { 255, 140,  20, 200 },
    { 255, 215,  20, 170 },
    { 255,  20, 150, 255 },
};

BubblesBackground::BubblesBackground()
{
    m_rng = std::mt19937(std::random_device{}());
    InitializeComponent();

    auto bg  = ref new LinearGradientBrush();
    bg->StartPoint = Point(0.0f, 0.0f);
    bg->EndPoint   = Point(1.0f, 1.0f);
    auto gs0 = ref new GradientStop();
    gs0->Color = { 255, 3, 5, 28 };
    gs0->Offset = 0.0;
    auto gs1 = ref new GradientStop();
    gs1->Color = { 255, 8, 3, 40 };
    gs1->Offset = 1.0;
    bg->GradientStops->Append(gs0);
    bg->GradientStops->Append(gs1);
    BubbleCanvas->Background = bg;

    TimeSpan interval;
    interval.Duration = 16 * 10000LL;
    m_timer = ref new DispatcherTimer();
    m_timer->Interval = interval;
    m_timer->Tick += ref new EventHandler<Object^>(this, &BubblesBackground::OnTick);
}

void BubblesBackground::Canvas_SizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
    m_canvasW = static_cast<float>(e->NewSize.Width);
    m_canvasH = static_cast<float>(e->NewSize.Height);

    if (!m_initialized && m_canvasW > 0 && m_canvasH > 0) {
        InitBubbles();
        m_initialized = true;
    }
}

void BubblesBackground::InitBubbles()
{
    m_bubbles.clear();
    m_bubbles.reserve(kBubbleCount);
    BubbleCanvas->Children->Clear();

    std::uniform_real_distribution<float> distAngle(0.0f, 6.28318f);

    for (int i = 0; i < kBubbleCount; ++i) {
        BubbleState s;
        float minSpeed, maxSpeed;

        // Ensure size variety: 4 large, 6 medium, 6 small
        if (i < 4) {
            s.radius = 100.0f + static_cast<float>(m_rng() % 80);
            minSpeed = 0.18f; maxSpeed = 0.55f;
        } else if (i < 10) {
            s.radius = 45.0f + static_cast<float>(m_rng() % 55);
            minSpeed = 0.45f; maxSpeed = 1.1f;
        } else {
            s.radius = 15.0f + static_cast<float>(m_rng() % 30);
            minSpeed = 0.8f;  maxSpeed = 1.8f;
        }

        std::uniform_real_distribution<float> distX(s.radius, m_canvasW - s.radius);
        std::uniform_real_distribution<float> distY(s.radius, m_canvasH - s.radius);
        std::uniform_real_distribution<float> distSpeed(minSpeed, maxSpeed);

        s.x = distX(m_rng);
        s.y = distY(m_rng);

        float angle = distAngle(m_rng);
        float speed = distSpeed(m_rng);
        s.vx = cosf(angle) * speed;
        s.vy = sinf(angle) * speed;

        s.colorIndex    = m_rng() % kColorCount;
        s.opacityMax    = 0.50f + static_cast<float>(m_rng() % 30) / 100.0f;
        s.opacityMin    = s.opacityMax * 0.30f;
        s.opacity       = s.opacityMin + (s.opacityMax - s.opacityMin)
                          * static_cast<float>(m_rng() % 100) / 100.0f;
        s.opacityDelta  = (0.003f + static_cast<float>(m_rng() % 7) / 1000.0f)
                          * (m_rng() % 2 == 0 ? 1.0f : -1.0f);
        s.specularOpacity = 0.15f + static_cast<float>(m_rng() % 15) / 100.0f;

        m_bubbles.push_back(s);

        Color col = kPalette[s.colorIndex];
        float glowD = s.radius * 3.0f;
        float bodyD = s.radius * 2.0f;
        float specD = s.radius * 0.5f;

        // Glow halo
        auto glow = ref new Ellipse();
        glow->Width  = glowD;
        glow->Height = glowD;
        glow->Fill   = ref new SolidColorBrush(ColorHelper::FromArgb(255, col.R, col.G, col.B));
        glow->Opacity = s.opacity * 0.3f;
        Canvas::SetLeft(glow, s.x - glowD * 0.5f);
        Canvas::SetTop(glow, s.y - glowD * 0.5f);
        BubbleCanvas->Children->Append(glow);

        // Main bubble body
        auto body = ref new Ellipse();
        body->Width  = bodyD;
        body->Height = bodyD;
        body->Fill   = ref new SolidColorBrush(ColorHelper::FromArgb(255, col.R, col.G, col.B));
        body->Opacity = s.opacity;
        Canvas::SetLeft(body, s.x - bodyD * 0.5f);
        Canvas::SetTop(body, s.y - bodyD * 0.5f);
        BubbleCanvas->Children->Append(body);

        // Specular highlight (upper-left)
        auto spec = ref new Ellipse();
        spec->Width  = specD;
        spec->Height = specD;
        spec->Fill   = ref new SolidColorBrush(ColorHelper::FromArgb(255, 220, 230, 255));
        spec->Opacity = s.specularOpacity;
        Canvas::SetLeft(spec, s.x - s.radius * 0.3f - specD * 0.5f);
        Canvas::SetTop(spec, s.y - s.radius * 0.3f - specD * 0.5f);
        BubbleCanvas->Children->Append(spec);
    }
}

void BubblesBackground::OnTick(Object^ sender, Object^ args)
{
    if (!m_initialized) return;

    int count = static_cast<int>(m_bubbles.size());
    for (int i = 0; i < count; ++i) {
        auto& s = m_bubbles[i];

        s.x += s.vx;
        s.y += s.vy;

        if (s.x - s.radius < 0.0f) {
            s.x  = s.radius;
            s.vx = fabsf(s.vx);
        } else if (s.x + s.radius > m_canvasW) {
            s.x  = m_canvasW - s.radius;
            s.vx = -fabsf(s.vx);
        }

        if (s.y - s.radius < 0.0f) {
            s.y  = s.radius;
            s.vy = fabsf(s.vy);
        } else if (s.y + s.radius > m_canvasH) {
            s.y  = m_canvasH - s.radius;
            s.vy = -fabsf(s.vy);
        }

        s.opacity += s.opacityDelta;
        if (s.opacity > s.opacityMax) { s.opacity = s.opacityMax; s.opacityDelta = -fabsf(s.opacityDelta); }
        if (s.opacity < s.opacityMin) { s.opacity = s.opacityMin; s.opacityDelta =  fabsf(s.opacityDelta); }

        float glowD = s.radius * 3.0f;
        float bodyD = s.radius * 2.0f;
        float specD = s.radius * 0.5f;

        auto glow = safe_cast<Ellipse^>(BubbleCanvas->Children->GetAt(i * kElemsPerBubble));
        Canvas::SetLeft(glow, s.x - glowD * 0.5f);
        Canvas::SetTop(glow,  s.y - glowD * 0.5f);
        glow->Opacity = s.opacity * 0.3f;

        auto body = safe_cast<Ellipse^>(BubbleCanvas->Children->GetAt(i * kElemsPerBubble + 1));
        Canvas::SetLeft(body, s.x - bodyD * 0.5f);
        Canvas::SetTop(body,  s.y - bodyD * 0.5f);
        body->Opacity = s.opacity;

        auto spec = safe_cast<Ellipse^>(BubbleCanvas->Children->GetAt(i * kElemsPerBubble + 2));
        Canvas::SetLeft(spec, s.x - s.radius * 0.3f - specD * 0.5f);
        Canvas::SetTop(spec,  s.y - s.radius * 0.3f - specD * 0.5f);
    }
}

void BubblesBackground::StartAnimations()
{
    if (m_timer != nullptr) m_timer->Start();
}

void BubblesBackground::StopAnimations()
{
    if (m_timer != nullptr) m_timer->Stop();
}
