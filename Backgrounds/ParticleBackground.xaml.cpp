#include "pch.h"
#include "Backgrounds\ParticleBackground.xaml.h"

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Shapes;
using namespace Windows::UI::Xaml::Media;

static const int kParticleCount = 24;

ParticleBackground::ParticleBackground()
{
    m_rng = std::mt19937(std::random_device{}());
    InitializeComponent();

    TimeSpan interval;
    interval.Duration = 33 * 10000LL;
    m_timer = ref new DispatcherTimer();
    m_timer->Interval = interval;
    m_timer->Tick += ref new EventHandler<Object^>(this, &ParticleBackground::OnTick);
}

void ParticleBackground::Canvas_SizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
    m_canvasW = static_cast<float>(e->NewSize.Width);
    m_canvasH = static_cast<float>(e->NewSize.Height);

    if (!m_initialized && m_canvasW > 0 && m_canvasH > 0) {
        InitParticles();
        m_initialized = true;
    }
}

void ParticleBackground::InitParticles()
{
    std::uniform_real_distribution<float> distX(0.0f, m_canvasW);
    std::uniform_real_distribution<float> distY(0.0f, m_canvasH);
    std::uniform_real_distribution<float> distV(-0.4f, 0.4f);
    std::uniform_real_distribution<float> distOp(0.1f, 0.7f);
    std::uniform_real_distribution<float> distDelta(0.003f, 0.008f);
    std::uniform_real_distribution<float> distSize(4.0f, 18.0f);

    m_particles.clear();
    m_particles.reserve(kParticleCount);
    ParticleCanvas->Children->Clear();

    auto brush = ref new SolidColorBrush(ColorHelper::FromArgb(200, 180, 210, 255));

    for (int i = 0; i < kParticleCount; ++i) {
        ParticleState s;
        s.x = distX(m_rng);
        s.y = distY(m_rng);
        s.vx = distV(m_rng);
        s.vy = distV(m_rng);
        s.opacity = distOp(m_rng);
        s.opacityDelta = distDelta(m_rng) * (m_rng() % 2 == 0 ? 1.0f : -1.0f);
        s.size = distSize(m_rng);
        m_particles.push_back(s);

        auto ellipse = ref new Ellipse();
        ellipse->Width = s.size;
        ellipse->Height = s.size;
        ellipse->Fill = brush;
        ellipse->Opacity = s.opacity;
        Canvas::SetLeft(ellipse, s.x);
        Canvas::SetTop(ellipse, s.y);
        ParticleCanvas->Children->Append(ellipse);
    }
}

void ParticleBackground::OnTick(Object^ sender, Object^ args)
{
    if (!m_initialized) return;

    int count = static_cast<int>(m_particles.size());
    for (int i = 0; i < count; ++i) {
        auto& s = m_particles[i];

        s.x += s.vx;
        s.y += s.vy;

        if (s.x < -s.size)        s.x = m_canvasW + s.size;
        else if (s.x > m_canvasW + s.size) s.x = -s.size;
        if (s.y < -s.size)        s.y = m_canvasH + s.size;
        else if (s.y > m_canvasH + s.size) s.y = -s.size;

        s.opacity += s.opacityDelta;
        if (s.opacity > 0.75f || s.opacity < 0.05f) s.opacityDelta = -s.opacityDelta;

        auto el = safe_cast<Ellipse^>(ParticleCanvas->Children->GetAt(i));
        Canvas::SetLeft(el, s.x);
        Canvas::SetTop(el, s.y);
        el->Opacity = s.opacity;
    }
}

void ParticleBackground::StartAnimations()
{
    if (m_timer != nullptr) m_timer->Start();
}

void ParticleBackground::StopAnimations()
{
    if (m_timer != nullptr) m_timer->Stop();
}
