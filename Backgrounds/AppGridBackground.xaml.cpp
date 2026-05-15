#include "pch.h"
#include "Backgrounds\AppGridBackground.xaml.h"
#include <cmath>
#include <ppltasks.h>

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Shapes;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace concurrency;

static const int   kMaxBubbles     = 12;
static const int   kImageEvery     = 4;    // every 4th bubble shows a game image
static const int   kSpawnInterval  = 80;   // ticks between spawns (~1.3s at 60fps)
static const int   kLoadRetryTicks = 300;  // retry app load every ~5s
static const int   kElemsPerBubble = 2;    // glow + body
static const float kMinSpeed       = 0.35f;
static const float kMaxSpeed       = 0.85f;
static const float kFadeInStep     = 0.008f;
static const float kFadeOutStep    = 0.013f;

AppGridBackground::AppGridBackground()
{
    m_rng = std::mt19937(std::random_device{}());
    InitializeComponent();

    AppCanvas->Background = ref new SolidColorBrush(ColorHelper::FromArgb(255, 4, 5, 22));

    TimeSpan interval;
    interval.Duration = 16 * 10000LL;
    m_timer = ref new DispatcherTimer();
    m_timer->Interval = interval;
    m_timer->Tick += ref new EventHandler<Object^>(this, &AppGridBackground::OnTick);
}

void AppGridBackground::SetHosts(IVector<MoonlightHost^>^ hosts)
{
    m_hosts = hosts;
}

void AppGridBackground::Canvas_SizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
    m_canvasW = static_cast<float>(e->NewSize.Width);
    m_canvasH = static_cast<float>(e->NewSize.Height);
    m_initialized = m_canvasW > 0 && m_canvasH > 0;
}

void AppGridBackground::LoadAppsAsync()
{
    if (m_hosts == nullptr) return;

    // Snapshot connected+paired hosts on the UI thread before going async
    auto targets = ref new Vector<MoonlightHost^>();
    for (auto h : m_hosts) {
        if (h->Paired && h->Connected) targets->Append(h);
    }
    if (targets->Size == 0) return;

    Platform::WeakReference weakThis(this);
    create_task([targets]() {
        for (auto h : targets) {
            try { h->UpdateApps(); } catch (...) {}
        }
    }).then([weakThis, targets]() {
        // UpdateApps queues High-priority UI dispatches; we queue Low-priority so ours runs after.
        auto dispatcher = Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher;
        dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Low,
            ref new Windows::UI::Core::DispatchedHandler([weakThis, targets]() {
                auto that = weakThis.Resolve<AppGridBackground>();
                if (that == nullptr) return;
                auto collected = ref new Vector<MoonlightApp^>();
                for (auto h : targets) {
                    for (auto a : h->Apps) collected->Append(a);
                }
                if (collected->Size > 0) {
                    that->m_apps = collected;
                    that->m_appsLoaded = true;
                }
            }));
    });
}

void AppGridBackground::SpawnBubble()
{
    if (!m_initialized) return;
    if (static_cast<int>(m_bubbles.size()) >= kMaxBubbles) return;

    AppBubble b;
    std::uniform_real_distribution<float> distSpeed(kMinSpeed, kMaxSpeed);
    std::uniform_real_distribution<float> distDrift(-0.25f, 0.25f);

    b.radius = 55.0f + static_cast<float>(m_rng() % 75);

    // Spawn from bottom-left or bottom-right corner (~35% of width each)
    float span = m_canvasW * 0.35f;
    std::uniform_real_distribution<float> distCorner(b.radius, std::max(b.radius + 1.0f, span));
    bool leftCorner = (m_rng() % 2 == 0);
    float localX = distCorner(m_rng);
    b.x = leftCorner ? localX : (m_canvasW - localX);
    b.y = m_canvasH + b.radius + 10.0f;

    b.vy            = -distSpeed(m_rng);
    b.vx            = distDrift(m_rng);
    b.opacity       = 0.0f;
    b.opacityTarget = 0.45f + static_cast<float>(m_rng() % 30) / 100.0f;

    // Decide image vs solid color
    m_bubbleSinceImg++;
    b.isImageBubble = false;
    b.appIndex      = -1;

    bool tryImage = (m_bubbleSinceImg >= kImageEvery) && m_appsLoaded
                    && m_apps != nullptr && m_apps->Size > 0;
    if (tryImage) {
        unsigned int sz = m_apps->Size;
        for (unsigned int i = 0; i < sz; ++i) {
            int idx = m_nextAppIdx % static_cast<int>(sz);
            m_nextAppIdx++;
            if (m_apps->GetAt(idx)->Image != nullptr) {
                b.appIndex      = idx;
                b.isImageBubble = true;
                m_bubbleSinceImg = 0;
                break;
            }
        }
    }

    b.elemBase = static_cast<int>(AppCanvas->Children->Size);
    m_bubbles.push_back(b);

    float glowD = b.radius * 2.6f;
    float bodyD = b.radius * 2.0f;

    auto glow = ref new Ellipse();
    glow->Width   = glowD;
    glow->Height  = glowD;
    glow->Fill    = ref new SolidColorBrush(ColorHelper::FromArgb(255, 60, 100, 200));
    glow->Opacity = 0.0;
    Canvas::SetLeft(glow, b.x - glowD * 0.5f);
    Canvas::SetTop(glow,  b.y - glowD * 0.5f);
    AppCanvas->Children->Append(glow);

    auto body = ref new Ellipse();
    body->Width   = bodyD;
    body->Height  = bodyD;
    body->Opacity = 0.0;
    Canvas::SetLeft(body, b.x - bodyD * 0.5f);
    Canvas::SetTop(body,  b.y - bodyD * 0.5f);

    if (b.isImageBubble) {
        auto brush = ref new ImageBrush();
        brush->ImageSource = m_apps->GetAt(b.appIndex)->Image;
        brush->Stretch     = Stretch::UniformToFill;
        body->Fill = brush;
    } else {
        body->Fill = ref new SolidColorBrush(ColorHelper::FromArgb(255, 35, 65, 170));
    }
    AppCanvas->Children->Append(body);
}

void AppGridBackground::OnTick(Object^ sender, Object^ args)
{
    if (!m_initialized) return;

    if (!m_appsLoaded) {
        if (++m_loadRetryTick >= kLoadRetryTicks) {
            m_loadRetryTick = 0;
            LoadAppsAsync();
        }
    }

    if (++m_spawnTick >= kSpawnInterval) {
        m_spawnTick = 0;
        SpawnBubble();
    }

    float fadeOutY = m_canvasH * 0.3f;
    std::vector<int> toRemove;
    int count = static_cast<int>(m_bubbles.size());

    for (int i = 0; i < count; ++i) {
        auto& b = m_bubbles[i];
        b.x += b.vx;
        b.y += b.vy;

        b.opacity = (b.y < fadeOutY)
            ? std::max(0.0f, b.opacity - kFadeOutStep)
            : std::min(b.opacityTarget, b.opacity + kFadeInStep);

        if (b.y + b.radius < -30.0f) {
            toRemove.push_back(i);
            continue;
        }

        float glowD = b.radius * 2.6f;
        float bodyD = b.radius * 2.0f;

        auto glow = safe_cast<Ellipse^>(AppCanvas->Children->GetAt(b.elemBase));
        Canvas::SetLeft(glow, b.x - glowD * 0.5f);
        Canvas::SetTop(glow,  b.y - glowD * 0.5f);
        glow->Opacity = b.opacity * 0.22;

        auto body = safe_cast<Ellipse^>(AppCanvas->Children->GetAt(b.elemBase + 1));
        Canvas::SetLeft(body, b.x - bodyD * 0.5f);
        Canvas::SetTop(body,  b.y - bodyD * 0.5f);
        body->Opacity = b.opacity;
    }

    // Remove off-screen bubbles back-to-front to keep canvas indices stable
    for (int i = static_cast<int>(toRemove.size()) - 1; i >= 0; --i) {
        int bi   = toRemove[i];
        int base = m_bubbles[bi].elemBase;
        AppCanvas->Children->RemoveAt(base + 1);
        AppCanvas->Children->RemoveAt(base);
        m_bubbles.erase(m_bubbles.begin() + bi);
        for (int j = bi; j < static_cast<int>(m_bubbles.size()); ++j)
            m_bubbles[j].elemBase -= kElemsPerBubble;
    }
}

void AppGridBackground::StartAnimations()
{
    LoadAppsAsync();
    if (m_timer != nullptr) m_timer->Start();
}

void AppGridBackground::StopAnimations()
{
    if (m_timer != nullptr) m_timer->Stop();
}
