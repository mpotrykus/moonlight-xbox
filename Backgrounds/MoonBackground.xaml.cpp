#include "pch.h"
#include "Backgrounds\MoonBackground.xaml.h"
#include <cmath>

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Shapes;
using namespace Windows::UI::Xaml::Media;

static const int kStarCount       = 70;
static const int kCloudCount      = 6;
static const int kMoonChildIndex  = kStarCount;
static const int kCloudStartIndex = kStarCount + 1;

MoonBackground::MoonBackground()
{
    m_rng = std::mt19937(std::random_device{}());
    InitializeComponent();
    TimeSpan interval;
    interval.Duration = 16 * 10000LL;
    m_timer = ref new DispatcherTimer();
    m_timer->Interval = interval;
    m_timer->Tick += ref new EventHandler<Object^>(this, &MoonBackground::OnTick);
}

void MoonBackground::Canvas_SizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
    m_canvasW = static_cast<float>(e->NewSize.Width);
    m_canvasH = static_cast<float>(e->NewSize.Height);
    if (!m_initialized && m_canvasW > 0 && m_canvasH > 0) {
        InitScene();
        m_initialized = true;
    }
}

void MoonBackground::InitScene()
{
    m_stars.clear();
    m_clouds.clear();
    SkyCanvas->Children->Clear();

    std::uniform_real_distribution<float> distX(0.0f, m_canvasW);
    std::uniform_real_distribution<float> distY(0.0f, m_canvasH);
    std::uniform_real_distribution<float> distR(1.0f, 3.5f);
    std::uniform_real_distribution<float> distOp(0.25f, 0.85f);
    std::uniform_real_distribution<float> distPhase(0.0f, 6.28318f);
    std::uniform_real_distribution<float> distPhaseSpeed(0.005f, 0.025f);

    for (int i = 0; i < kStarCount; ++i) {
        StarState s;
        s.x          = distX(m_rng);
        s.y          = distY(m_rng);
        s.radius     = distR(m_rng);
        s.baseOpacity = distOp(m_rng);
        s.phase      = distPhase(m_rng);
        s.phaseSpeed = distPhaseSpeed(m_rng);
        m_stars.push_back(s);

        float d = s.radius * 2.0f;
        auto star = ref new Ellipse();
        star->Width   = d;
        star->Height  = d;
        star->Fill    = ref new SolidColorBrush(ColorHelper::FromArgb(255, 255, 255, 255));
        star->Opacity = s.baseOpacity;
        Canvas::SetLeft(star, s.x - s.radius);
        Canvas::SetTop(star,  s.y - s.radius);
        SkyCanvas->Children->Append(star);
    }

    // Moon: warm white circle, centered horizontally, upper third
    float moonR  = m_canvasH * 0.12f;
    float moonCx = m_canvasW * 0.5f;
    float moonCy = m_canvasH * 0.32f;
    auto moon = ref new Ellipse();
    moon->Width   = moonR * 2.0f;
    moon->Height  = moonR * 2.0f;
    moon->Fill    = ref new SolidColorBrush(ColorHelper::FromArgb(255, 245, 240, 210));
    moon->Opacity = 0.85;
    Canvas::SetLeft(moon, moonCx - moonR);
    Canvas::SetTop(moon,  moonCy - moonR);
    SkyCanvas->Children->Append(moon); // kMoonChildIndex

    // Clouds: wide, squat, semi-transparent blobs drifting left to right
    std::uniform_real_distribution<float> distCloudY(m_canvasH * 0.15f, m_canvasH * 0.75f);
    std::uniform_real_distribution<float> distCloudW(280.0f, 600.0f);
    std::uniform_real_distribution<float> distCloudH(65.0f, 140.0f);
    std::uniform_real_distribution<float> distCloudOp(0.05f, 0.14f);
    std::uniform_real_distribution<float> distCloudSpeed(0.15f, 0.55f);
    std::uniform_real_distribution<float> distCloudX(-m_canvasW * 0.5f, m_canvasW * 1.5f);

    for (int i = 0; i < kCloudCount; ++i) {
        CloudState c;
        c.width  = distCloudW(m_rng);
        c.height = distCloudH(m_rng);
        c.y      = distCloudY(m_rng);
        c.x      = distCloudX(m_rng);
        c.opacity = distCloudOp(m_rng);
        c.speed  = distCloudSpeed(m_rng);
        m_clouds.push_back(c);

        auto cloud = ref new Ellipse();
        cloud->Width   = c.width;
        cloud->Height  = c.height;
        cloud->Fill    = ref new SolidColorBrush(ColorHelper::FromArgb(255, 220, 230, 255));
        cloud->Opacity = c.opacity;
        Canvas::SetLeft(cloud, c.x - c.width * 0.5f);
        Canvas::SetTop(cloud,  c.y - c.height * 0.5f);
        SkyCanvas->Children->Append(cloud); // kCloudStartIndex + i
    }
}

void MoonBackground::OnTick(Object^ sender, Object^ args)
{
    if (!m_initialized) return;

    for (int i = 0; i < kStarCount; ++i) {
        auto& s = m_stars[i];
        s.phase += s.phaseSpeed;
        float op = s.baseOpacity * (0.55f + 0.45f * sinf(s.phase));
        auto star = safe_cast<Ellipse^>(SkyCanvas->Children->GetAt(i));
        star->Opacity = op;
    }

    for (int i = 0; i < kCloudCount; ++i) {
        auto& c = m_clouds[i];
        c.x += c.speed;
        if (c.x - c.width * 0.5f > m_canvasW)
            c.x = -c.width * 0.5f;
        auto cloud = safe_cast<Ellipse^>(SkyCanvas->Children->GetAt(kCloudStartIndex + i));
        Canvas::SetLeft(cloud, c.x - c.width * 0.5f);
    }
}

void MoonBackground::StartAnimations()
{
    if (m_timer != nullptr) m_timer->Start();
}

void MoonBackground::StopAnimations()
{
    if (m_timer != nullptr) m_timer->Stop();
}
