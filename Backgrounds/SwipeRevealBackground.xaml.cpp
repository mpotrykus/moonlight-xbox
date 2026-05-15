#include "pch.h"
#include "Backgrounds\SwipeRevealBackground.xaml.h"
#include <cmath>
#include <algorithm>
#include <ppltasks.h>

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Shapes;
using namespace concurrency;

static const int   kHoldTicks  = 240;    // 4 s at 60 fps
static const int   kWipeTicks  = 100;    // ~1.7 s
static const float kGlassWidth = 64.0f;
static const float kPanMax     = 0.04f;  // ±4% in relative coords; within 1.1× scale buffer
static const float kSlantH      = 180.0f; // horizontal span of diagonal over full screen height
// Extra distance so the glass edge and diagonal clips are fully off-screen at wipe start/end
static const float kWipeMargin  = kGlassWidth * 0.5f + kSlantH * 0.5f;
static const int   kLoadRetry  = 500;

static float EaseInOut(float t)
{
    // Cubic ease-in-out: slow start, fast middle, slow end
    return t < 0.5f ? 4.0f * t * t * t
                    : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}
static const float kPi         = 3.14159265f;
static const int   kNumSlices  = 14;     // horizontal strips used to fake the diagonal clip

SwipeRevealBackground::SwipeRevealBackground()
{
    InitializeComponent();

    // Build the shared ImageBrush + pan transform for all front slices
    m_frontPan = ref new CompositeTransform();
    m_frontPan->ScaleX   = 1.1;
    m_frontPan->ScaleY   = 1.1;
    m_frontPan->CenterX  = 0.5;
    m_frontPan->CenterY  = 0.5;

    m_frontBrush = ref new ImageBrush();
    m_frontBrush->Stretch             = Stretch::UniformToFill;
    m_frontBrush->RelativeTransform   = m_frontPan;

    // Create kNumSlices strip rectangles, each with its own RectangleGeometry clip.
    // All share m_frontBrush so they render as one coherent image.
    for (int i = 0; i < kNumSlices; ++i) {
        auto clip = ref new RectangleGeometry();
        m_sliceClips.push_back(clip);

        auto rect = ref new Rectangle();
        rect->Fill = m_frontBrush;
        rect->Clip = clip;
        FrontGrid->Children->Append(rect);
    }

    TimeSpan ts;
    ts.Duration = 16 * 10000LL;
    m_timer = ref new DispatcherTimer();
    m_timer->Interval = ts;
    m_timer->Tick += ref new EventHandler<Object^>(this, &SwipeRevealBackground::OnTick);
}

void SwipeRevealBackground::SetHosts(IVector<MoonlightHost^>^ hosts)
{
    m_hosts = hosts;
}

void SwipeRevealBackground::Grid_SizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
    m_canvasW     = static_cast<float>(e->NewSize.Width);
    m_canvasH     = static_cast<float>(e->NewSize.Height);
    m_initialized = (m_canvasW > 0 && m_canvasH > 0);
    if (m_initialized) {
        GlassEdgeSkew->CenterY = m_canvasH * 0.5f;
        UpdateGlassEdgeSkew();
    }
}

// Zero every slice clip so FrontGrid is invisible during the HOLD phase.
// UpdateDiagonalClip(0) would leak kSlantH/2 pixels on the leading side.
void SwipeRevealBackground::ZeroFrontClips()
{
    float sliceH = m_canvasH > 0 ? m_canvasH / static_cast<float>(kNumSlices) : 1.0f;
    for (int i = 0; i < kNumSlices; ++i)
        m_sliceClips[i]->Rect = Rect(0.0f, i * sliceH, 0.0f, sliceH);
}

void SwipeRevealBackground::UpdateGlassEdgeSkew()
{
    if (m_canvasH <= 0) return;
    // "/" lean for L→R (AngleX negative), "\" lean for R→L (AngleX positive)
    float angle = atanf(kSlantH / m_canvasH) * 180.0f / kPi;
    GlassEdgeSkew->AngleX = (m_wipeDir > 0) ? -angle : angle;
}

void SwipeRevealBackground::LoadAppsAsync()
{
    if (m_hosts == nullptr) return;
    auto targets = ref new Vector<MoonlightHost^>();
    for (auto h : m_hosts)
        if (h->Paired && h->Connected) targets->Append(h);
    if (targets->Size == 0) return;

    Platform::WeakReference weakThis(this);
    create_task([targets]() {
        for (auto h : targets)
            try { h->UpdateApps(); } catch (...) {}
    }).then([weakThis, targets]() {
        auto dispatcher = Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher;
        dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Low,
            ref new Windows::UI::Core::DispatchedHandler([weakThis, targets]() {
                auto that = weakThis.Resolve<SwipeRevealBackground>();
                if (that == nullptr) return;
                auto collected = ref new Vector<MoonlightApp^>();
                for (auto h : targets)
                    for (auto a : h->Apps) collected->Append(a);
                if (collected->Size > 0) {
                    that->m_apps       = collected;
                    that->m_appsLoaded = true;
                    that->InitSlides();
                }
            }));
    });
}

int SwipeRevealBackground::FindNextAppWithImage(int startIdx)
{
    if (!m_appsLoaded || m_apps == nullptr || m_apps->Size == 0) return -1;
    int sz = static_cast<int>(m_apps->Size);
    startIdx = ((startIdx % sz) + sz) % sz;
    for (int i = 0; i < sz; i++) {
        int idx = (startIdx + i) % sz;
        if (m_apps->GetAt(idx)->Image != nullptr) return idx;
    }
    return -1;
}

void SwipeRevealBackground::InitPanForLayer(float& px, float& py, float& vx, float& vy)
{
    static const float dirX[] = {  1, -1,  1, -1 };
    static const float dirY[] = {  1, -1, -1,  1 };
    int d   = (m_panDirIdx++) % 4;
    float sx = dirX[d], sy = dirY[d];
    float speed = 2.0f * kPanMax / static_cast<float>(kHoldTicks + kWipeTicks);
    px = -sx * kPanMax;
    py = -sy * kPanMax;
    vx = sx * speed;
    vy = sy * speed;
}

void SwipeRevealBackground::AdvancePan(float& px, float& py, float vx, float vy, CompositeTransform^ xf)
{
    px += vx;
    py += vy;
    xf->TranslateX = px;
    xf->TranslateY = py;
}

// Update the RectangleGeometry clip for each horizontal slice to produce a diagonal edge.
// For "/" (L→R): top strips are most revealed. For "/" (R→L): top strips are also most revealed
// from the right side, so the same formula applies in both directions.
void SwipeRevealBackground::UpdateDiagonalClip(float swept)
{
    float sliceH = m_canvasH / static_cast<float>(kNumSlices);

    for (int i = 0; i < kNumSlices; ++i) {
        float sliceY = i * sliceH;
        // Top strip (i=0) gets +kSlantH/2 offset; bottom strip gets -kSlantH/2
        float offset = kSlantH * (0.5f - i * 1.0f / kNumSlices);

        if (m_wipeDir > 0) {
            // L→R: clip from x=0, width grows left-to-right per strip
            float w = std::max(0.0f, std::min(swept + offset, m_canvasW));
            m_sliceClips[i]->Rect = Rect(0.0f, sliceY, w, sliceH);
        } else {
            // R→L: clip from x=edge, extends to canvasW
            float x = std::max(0.0f, std::min(m_canvasW - swept - offset, m_canvasW));
            float w = std::max(0.0f, m_canvasW - x);
            m_sliceClips[i]->Rect = Rect(x, sliceY, w, sliceH);
        }
    }
}

void SwipeRevealBackground::InitSlides()
{
    m_backAppIdx = FindNextAppWithImage(0);
    if (m_backAppIdx < 0) return;

    BackBrush->ImageSource    = m_apps->GetAt(m_backAppIdx)->Image;
    m_frontAppIdx = FindNextAppWithImage(m_backAppIdx + 1);
    if (m_frontAppIdx >= 0)
        m_frontBrush->ImageSource = m_apps->GetAt(m_frontAppIdx)->Image;

    InitPanForLayer(m_backPanX,  m_backPanY,  m_backVX,  m_backVY);
    InitPanForLayer(m_frontPanX, m_frontPanY, m_frontVX, m_frontVY);
    BackPan->TranslateX      = m_backPanX;  BackPan->TranslateY  = m_backPanY;
    m_frontPan->TranslateX   = m_frontPanX; m_frontPan->TranslateY = m_frontPanY;

    m_wipeDir  = 1;
    m_wipeTick = 0;
    m_phase    = 0;
    m_holdTick = 0;
    GlassEdge->Opacity = 0.0;
    ZeroFrontClips();
    UpdateGlassEdgeSkew();
}

void SwipeRevealBackground::AdvanceSlide()
{
    // Promote front to back — copy image and pan state to avoid a visible pop
    BackBrush->ImageSource  = m_frontBrush->ImageSource;
    BackPan->TranslateX     = m_frontPan->TranslateX;
    BackPan->TranslateY     = m_frontPan->TranslateY;
    m_backPanX = m_frontPanX; m_backPanY = m_frontPanY;
    m_backVX   = m_frontVX;   m_backVY   = m_frontVY;
    m_backAppIdx = m_frontAppIdx;

    // Load next image into front
    m_frontAppIdx = FindNextAppWithImage(m_backAppIdx + 1);
    if (m_frontAppIdx >= 0)
        m_frontBrush->ImageSource = m_apps->GetAt(m_frontAppIdx)->Image;

    InitPanForLayer(m_frontPanX, m_frontPanY, m_frontVX, m_frontVY);
    m_frontPan->TranslateX = m_frontPanX;
    m_frontPan->TranslateY = m_frontPanY;

    m_wipeDir  = -m_wipeDir;
    m_wipeTick = 0;
    m_phase    = 0;
    m_holdTick = 0;
    GlassEdge->Opacity = 0.0;
    ZeroFrontClips();
    UpdateGlassEdgeSkew();
}

void SwipeRevealBackground::OnTick(Object^ sender, Object^ args)
{
    if (!m_initialized) return;

    if (!m_appsLoaded) {
        if (++m_loadRetryTick >= kLoadRetry) {
            m_loadRetryTick = 0;
            LoadAppsAsync();
        }
        return;
    }

    if (m_backAppIdx < 0) {
        if (++m_imageRetryTick >= 60) {
            m_imageRetryTick = 0;
            InitSlides();
        }
        return;
    }

    AdvancePan(m_backPanX, m_backPanY, m_backVX, m_backVY, BackPan);

    if (m_phase == 0) {
        if (++m_holdTick >= kHoldTicks && m_frontAppIdx >= 0) {
            m_phase    = 1;
            m_wipeTick = 0;
            GlassEdge->Opacity = 1.0;
        }
    } else {
        AdvancePan(m_frontPanX, m_frontPanY, m_frontVX, m_frontVY, m_frontPan);

        m_wipeTick++;
        float t     = std::min(static_cast<float>(m_wipeTick) / static_cast<float>(kWipeTicks), 1.0f);
        // Map eased t across (canvasW + 2*margin), offset by -margin so the edge
        // starts and ends fully off-screen on both sides.
        float swept = EaseInOut(t) * (m_canvasW + 2.0f * kWipeMargin) - kWipeMargin;

        UpdateDiagonalClip(swept);

        float cx = (m_wipeDir > 0) ? swept : (m_canvasW - swept);
        GlassEdgeTranslate->X = cx - kGlassWidth * 0.5f;

        if (m_wipeTick >= kWipeTicks)
            AdvanceSlide();
    }
}

void SwipeRevealBackground::StartAnimations()
{
    LoadAppsAsync();
    if (m_timer) m_timer->Start();
}

void SwipeRevealBackground::StopAnimations()
{
    if (m_timer) m_timer->Stop();
    GlassEdge->Opacity = 0.0;
}
