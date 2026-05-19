#include "pch.h"
#include "Backgrounds\SpheresBackground.xaml.h"
#include <cmath>

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Shapes;
using namespace Windows::UI::Xaml::Media;

static const int kSphereCount    = 14;
static const int kElemsPerSphere = 1;

SpheresBackground::SpheresBackground()
{
    m_rng = std::mt19937(std::random_device{}());
    InitializeComponent();

    TimeSpan interval;
    interval.Duration = 16 * 10000LL;
    m_timer = ref new DispatcherTimer();
    m_timer->Interval = interval;
    Platform::WeakReference weakSelf(this);
    m_tickToken = m_timer->Tick += ref new EventHandler<Object^>(
        [weakSelf](Object^, Object^) {
            try {
                auto self = weakSelf.Resolve<SpheresBackground>();
                if (self) self->OnTick(nullptr, nullptr);
            } catch (Platform::DisconnectedException^) {}
              catch (...) {}
        });
}

void SpheresBackground::Canvas_SizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
    m_canvasW = static_cast<float>(e->NewSize.Width);
    m_canvasH = static_cast<float>(e->NewSize.Height);

    if (!m_initialized && m_canvasW > 0 && m_canvasH > 0) {
        InitSpheres();
        m_initialized = true;
    }
}

void SpheresBackground::InitSpheres()
{
    m_spheres.clear();
    m_spheres.reserve(kSphereCount);
    SphereCanvas->Children->Clear();

    std::uniform_real_distribution<float> distAngle(0.0f, 6.28318f);
    std::uniform_real_distribution<float> distOpacity(0.05f, 0.25f);

    for (int i = 0; i < kSphereCount; ++i) {
        SphereState s;
        float minSpeed, maxSpeed;

        if (i < 3) {
            s.radius = 110.0f + static_cast<float>(m_rng() % 70);
            minSpeed = 0.15f; maxSpeed = 0.45f;
        } else if (i < 8) {
            s.radius = 50.0f  + static_cast<float>(m_rng() % 50);
            minSpeed = 0.40f; maxSpeed = 1.0f;
        } else {
            s.radius = 18.0f  + static_cast<float>(m_rng() % 28);
            minSpeed = 0.75f; maxSpeed = 1.6f;
        }

        std::uniform_real_distribution<float> distX(s.radius, m_canvasW - s.radius);
        std::uniform_real_distribution<float> distY(s.radius, m_canvasH - s.radius);
        std::uniform_real_distribution<float> distSpeed(minSpeed, maxSpeed);

        s.x          = distX(m_rng);
        s.y          = distY(m_rng);
        s.opacity    = distOpacity(m_rng);

        float angle = distAngle(m_rng);
        float speed = distSpeed(m_rng);
        s.vx = cosf(angle) * speed;
        s.vy = sinf(angle) * speed;

        m_spheres.push_back(s);

        float bodyD = s.radius * 2.0f;

        auto sphere = ref new Ellipse();
        sphere->Width           = bodyD;
        sphere->Height          = bodyD;
        sphere->Stroke          = ref new SolidColorBrush(ColorHelper::FromArgb(255, 255, 255, 255));
        sphere->StrokeThickness = 1.0;
        sphere->Opacity         = s.opacity;
        Canvas::SetLeft(sphere, s.x - s.radius);
        Canvas::SetTop(sphere,  s.y - s.radius);
        SphereCanvas->Children->Append(sphere);
    }
}

void SpheresBackground::OnTick(Object^ sender, Object^ args)
{
    if (!m_initialized) return;

    int count = static_cast<int>(m_spheres.size());
    for (int i = 0; i < count; ++i) {
        auto& s = m_spheres[i];

        s.x += s.vx;
        s.y += s.vy;

        if (s.x - s.radius < 0.0f)          { s.x = s.radius;             s.vx =  fabsf(s.vx); }
        else if (s.x + s.radius > m_canvasW) { s.x = m_canvasW - s.radius; s.vx = -fabsf(s.vx); }

        if (s.y - s.radius < 0.0f)          { s.y = s.radius;             s.vy =  fabsf(s.vy); }
        else if (s.y + s.radius > m_canvasH) { s.y = m_canvasH - s.radius; s.vy = -fabsf(s.vy); }

        auto sphere = safe_cast<Ellipse^>(SphereCanvas->Children->GetAt(i));
        Canvas::SetLeft(sphere, s.x - s.radius);
        Canvas::SetTop(sphere,  s.y - s.radius);
    }
}

void SpheresBackground::StartAnimations()
{
    if (m_timer != nullptr) m_timer->Start();
}

void SpheresBackground::StopAnimations()
{
    if (m_timer != nullptr) {
        m_timer->Stop();
        m_timer->Tick -= m_tickToken;
    }
}
