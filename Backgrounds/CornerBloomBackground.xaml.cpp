#include "pch.h"
#include "Backgrounds\CornerBloomBackground.xaml.h"
#include <cmath>
#include <ppltasks.h>

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Shapes;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace Windows::UI::Composition;
using namespace Windows::UI::Xaml::Hosting;
using namespace concurrency;

static const int   kPoolSize       = 16;
static const int   kSpawnInterval  = 75;     // ticks between spawns (~1.25s at 60fps)
static const int   kLoadRetryTicks = 500;
static const float kImageGrowMin   = 2.0f;   // slow — creates the "zoom in slowly" effect
static const float kImageGrowMax   = 3.0f;
static const float kSolidGrowMin   = 3.5f;   // faster fallback when no images loaded
static const float kSolidGrowMax   = 5.5f;
static const float kEllipseOpacity = 1.0f;

// Starts a composition Scale animation on a XAML element's backing visual,
// growing from (0,0,1) to (1,1,1) with the scale pivot at (centerX, centerY).
// Runs entirely on the compositor thread — no UI-thread work per frame.
static void StartScaleAnimation(UIElement^ elem, float centerX, float centerY, int totalTicks)
{
    auto visual = ElementCompositionPreview::GetElementVisual(elem);
    auto compositor = visual->Compositor;

    float3 cp{}; cp.x = centerX; cp.y = centerY; cp.z = 0.0f;
    visual->CenterPoint = cp;

    float3 s0{}; s0.x = 0.0f; s0.y = 0.0f; s0.z = 1.0f;
    float3 s1{}; s1.x = 1.0f; s1.y = 1.0f; s1.z = 1.0f;
    auto anim = compositor->CreateVector3KeyFrameAnimation();
    anim->Duration = TimeSpan{ (long long)totalTicks * 16LL * 10000LL };
    anim->InsertKeyFrame(0.0f, s0);
    anim->InsertKeyFrame(1.0f, s1);
    visual->StartAnimation("Scale", anim);
}

static void StopScaleAnimation(UIElement^ elem)
{
    auto visual = ElementCompositionPreview::GetElementVisual(elem);
    visual->StopAnimation("Scale");
    // Reset scale so the next spawn starts cleanly from (0,0,1)
    float3 s0{}; s0.x = 0.0f; s0.y = 0.0f; s0.z = 1.0f;
    visual->Scale = s0;
}

CornerBloomBackground::CornerBloomBackground()
{
    m_rng = std::mt19937(std::random_device{}());
    InitializeComponent();

    BloomCanvas->Background = ref new SolidColorBrush(ColorHelper::FromArgb(255, 4, 5, 22));

    // Single unified pool — all slots are Ellipses that show either an app
    // image (ImageBrush fill) or the solid fallback color (SolidColorBrush fill).
    for (int i = 0; i < kPoolSize; ++i) {
        auto solid = ref new SolidColorBrush(ColorHelper::FromArgb(255, 50, 90, 210));
        auto el = ref new Ellipse();
        el->Fill            = solid;
        el->Stroke          = ref new SolidColorBrush(ColorHelper::FromArgb(255, 255, 255, 255));
        el->StrokeThickness = 1.0f;
        el->Opacity         = 0.0;
        BloomCanvas->Children->Append(el);

        BloomBubble b{};
        b.active        = false;
        b.cachedEllipse = el;
        b.solidBrush    = solid;
        m_pool.push_back(b);
    }

    TimeSpan interval;
    interval.Duration = 16 * 10000LL;
    m_timer = ref new DispatcherTimer();
    m_timer->Interval = interval;
    m_timer->Tick += ref new EventHandler<Object^>(this, &CornerBloomBackground::OnTick);
}

void CornerBloomBackground::SetHosts(IVector<MoonlightHost^>^ hosts)
{
    m_hosts = hosts;
}

void CornerBloomBackground::Canvas_SizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
    m_canvasW   = static_cast<float>(e->NewSize.Width);
    m_canvasH   = static_cast<float>(e->NewSize.Height);
    m_initialized = m_canvasW > 0 && m_canvasH > 0;
}

void CornerBloomBackground::LoadAppsAsync()
{
    if (m_hosts == nullptr) return;

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
        auto dispatcher = Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher;
        dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Low,
            ref new Windows::UI::Core::DispatchedHandler([weakThis, targets]() {
                auto that = weakThis.Resolve<CornerBloomBackground>();
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

void CornerBloomBackground::SpawnBubble()
{
    if (!m_initialized) return;

    // Strictly alternate corners: even = left, odd = right
    bool leftCorner = (m_spawnCount % 2 == 0);
    m_spawnCount++;

    float cx   = leftCorner ? 0.0f : m_canvasW;
    float cy   = m_canvasH;
    float maxR = sqrtf(m_canvasW * m_canvasW + m_canvasH * m_canvasH) + 20.0f;

    // Try a random app image (a few attempts to avoid always landing on art-less apps)
    int appIdx = -1;
    if (m_appsLoaded && m_apps != nullptr && m_apps->Size > 0) {
        unsigned int sz = m_apps->Size;
        std::uniform_int_distribution<int> dist(0, static_cast<int>(sz) - 1);
        for (int attempt = 0; attempt < 5; ++attempt) {
            int idx = dist(m_rng);
            if (m_apps->GetAt(idx)->Image != nullptr) {
                appIdx = idx;
                break;
            }
        }
    }
    bool useImage = (appIdx >= 0);

    // Find a free pool slot
    BloomBubble* slot = nullptr;
    for (auto& b : m_pool) {
        if (!b.active) { slot = &b; break; }
    }
    if (slot == nullptr) return;

    float grow = useImage
        ? std::uniform_real_distribution<float>(kImageGrowMin, kImageGrowMax)(m_rng)
        : std::uniform_real_distribution<float>(kSolidGrowMin, kSolidGrowMax)(m_rng);

    slot->active       = true;
    slot->cornerX      = cx;
    slot->cornerY      = cy;
    slot->maxRadius    = maxR;
    slot->growSpeed    = grow;
    slot->totalTicks   = (int)ceilf(maxR / grow);
    slot->elapsedTicks = 0;
    slot->hasImage     = useImage;

    float d  = maxR * 2.0f;
    auto  el = slot->cachedEllipse;
    el->Width  = d;
    el->Height = d;
    Canvas::SetLeft(el, cx - maxR);
    Canvas::SetTop(el,  cy - maxR);

    if (useImage) {
        auto brush = ref new ImageBrush();
        brush->ImageSource = m_apps->GetAt(appIdx)->Image;
        brush->Stretch     = Stretch::UniformToFill;
        el->Fill = brush;
    } else {
        el->Fill = slot->solidBrush;
    }

    el->Opacity = kEllipseOpacity;
    StartScaleAnimation(el, maxR, maxR, slot->totalTicks);
}

void CornerBloomBackground::DeactivateBubble(BloomBubble& b)
{
    b.active = false;
    StopScaleAnimation(b.cachedEllipse);
    if (b.hasImage) {
        b.cachedEllipse->Fill = b.solidBrush;
        b.hasImage = false;
    }
    b.cachedEllipse->Opacity = 0.0;
}

void CornerBloomBackground::OnTick(Object^ sender, Object^ args)
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

    // Composition animations handle all per-frame rendering — OnTick only
    // tracks elapsed ticks to know when to retire each bubble.
    for (auto& b : m_pool) {
        if (!b.active) continue;
        if (++b.elapsedTicks >= b.totalTicks) {
            DeactivateBubble(b);
        }
    }
}

void CornerBloomBackground::ResetAndReload()
{
    // Deactivate all running bubbles immediately
    for (auto& b : m_pool) {
        if (b.active) DeactivateBubble(b);
    }

    // Clear app cache and force an immediate reload next tick
    m_apps        = nullptr;
    m_appsLoaded  = false;
    m_nextAppIdx  = 0;
    m_loadRetryTick = kLoadRetryTicks - 1;

    LoadAppsAsync();
}

void CornerBloomBackground::StartAnimations()
{
    LoadAppsAsync();
    if (m_timer != nullptr) m_timer->Start();
}

void CornerBloomBackground::StopAnimations()
{
    if (m_timer != nullptr) m_timer->Stop();
    for (auto& b : m_pool) {
        if (b.active) DeactivateBubble(b);
    }
}
