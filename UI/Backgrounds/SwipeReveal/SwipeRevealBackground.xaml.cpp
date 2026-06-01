#include "pch.h"
#include "UI\Backgrounds\SwipeReveal\SwipeRevealBackground.xaml.h"
#include <cmath>
#include <algorithm>
#include <random>
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

    // FrontGrid gets a rectangular clip that grows during the wipe, plus a SkewTransform so
    // the clip's leading edge appears diagonal in screen space.  The Rectangle inside gets
    // the inverse SkewTransform so the image content appears unskewed.
    m_frontClipRect = ref new RectangleGeometry();
    FrontGrid->Clip = m_frontClipRect;

    m_frontGridSkew = ref new SkewTransform();
    FrontGrid->RenderTransform = m_frontGridSkew;

    m_frontRectInvSkew = ref new SkewTransform();

    auto frontRect = ref new Rectangle();
    frontRect->Fill            = m_frontBrush;
    frontRect->RenderTransform = m_frontRectInvSkew;
    FrontGrid->Children->Append(frontRect);

    TimeSpan ts;
    ts.Duration = 16 * 10000LL;
    m_timer = ref new DispatcherTimer();
    m_timer->Interval = ts;
    Platform::WeakReference weakSelf(this);
    m_tickToken = m_timer->Tick += ref new EventHandler<Object^>(
        [weakSelf](Object^, Object^) {
            try {
                auto self = weakSelf.Resolve<SwipeRevealBackground>();
                if (self) self->OnTick(nullptr, nullptr);
            } catch (Platform::DisconnectedException^) {}
              catch (...) {}
        });
}

void SwipeRevealBackground::SetHosts(IVector<MoonlightHost^>^ hosts)
{
    m_hosts      = hosts;
    m_appsLoaded = false;
    m_apps       = nullptr;
    m_frontAppIdx = -1;
    m_backAppIdx  = -1;
}

void SwipeRevealBackground::Grid_SizeChanged(Object^ sender, SizeChangedEventArgs^ e)
{
    m_canvasW     = static_cast<float>(e->NewSize.Width);
    m_canvasH     = static_cast<float>(e->NewSize.Height);
    m_initialized = (m_canvasW > 0 && m_canvasH > 0);
    if (m_initialized) {
        GlassEdgeSkew->CenterY    = m_canvasH * 0.5f;
        m_frontGridSkew->CenterY  = m_canvasH * 0.5f;
        m_frontRectInvSkew->CenterY = m_canvasH * 0.5f;
        // Keep the clip's height in sync with the canvas
        auto r = m_frontClipRect->Rect;
        m_frontClipRect->Rect = Rect(r.X, 0.0f, r.Width, m_canvasH);
        UpdateGlassEdgeSkew();
    }
}

// Collapse the clip to zero width so FrontGrid is invisible during HOLD.
void SwipeRevealBackground::ZeroFrontClips()
{
    m_frontClipRect->Rect = Rect(0.0f, 0.0f, 0.0f, m_canvasH > 0.0f ? m_canvasH : 1.0f);
}

void SwipeRevealBackground::UpdateGlassEdgeSkew()
{
    if (m_canvasH <= 0) return;
    // "/" lean for L→R (AngleX negative), "\" lean for R→L (AngleX positive)
    float angle = atanf(kSlantH / m_canvasH) * 180.0f / kPi;
    float gridAngle = (m_wipeDir > 0) ? -angle : angle;
    GlassEdgeSkew->AngleX    =  gridAngle;
    m_frontGridSkew->AngleX  =  gridAngle;
    m_frontRectInvSkew->AngleX = -gridAngle;
}

void SwipeRevealBackground::ShuffleAndApply(Platform::Collections::Vector<MoonlightApp^>^ collected)
{
    std::vector<MoonlightApp^> vec;
    vec.reserve(collected->Size);
    for (auto a : collected) vec.push_back(a);
    std::mt19937 rng(std::random_device{}());
    std::shuffle(vec.begin(), vec.end(), rng);
    auto shuffled = ref new Platform::Collections::Vector<MoonlightApp^>();
    for (auto a : vec) shuffled->Append(a);
    m_apps       = shuffled;
    m_appsLoaded = true;
    InitSlides();
}

void SwipeRevealBackground::LoadAppsAsync()
{
    if (m_hosts == nullptr) return;

    // Phase 1: apps already loaded in memory for any paired host (works when host is offline)
    {
        auto inMemory = ref new Vector<MoonlightApp^>();
        for (auto h : m_hosts)
            if (h->Paired)
                for (auto a : h->Apps) inMemory->Append(a);
        if (inMemory->Size > 0) {
            ShuffleAndApply(inMemory);
            // Best-effort background refresh so h->Apps stays current
            auto targets = ref new Vector<MoonlightHost^>();
            for (auto h : m_hosts)
                if (h->Paired && h->Connected) targets->Append(h);
            if (targets->Size > 0)
                create_task([targets]() {
                    for (auto h : targets)
                        try { h->UpdateApps(); } catch (...) {}
                });
            return;
        }
    }

    // Phase 2: scan per-host disk cache using InstanceId (available from state.json even offline)
    Platform::WeakReference weakThis(this);
    Platform::String^ baseImages = Windows::Storage::ApplicationData::Current->LocalFolder->Path;
    baseImages = Platform::String::Concat(baseImages, L"\\images\\");

    // Collect per-host subdirectory paths for any host with a known InstanceId.
    // Do NOT gate on h->Paired — Paired is only set after a live network handshake,
    // so it is always false on a fresh launch with the host offline.
    // InstanceId comes from state.json and is available regardless of connectivity.
    auto hostDirs = std::make_shared<std::vector<std::wstring>>();
    for (auto h : m_hosts) {
        if (h->InstanceId != nullptr && !h->InstanceId->IsEmpty())
            hostDirs->push_back(std::wstring(Platform::String::Concat(baseImages,
                Platform::String::Concat(h->InstanceId, L"\\"))->Data()));
    }

    if (hostDirs->empty()) {
        LoadFromNetworkAsync();
        return;
    }

    auto imagePaths = std::make_shared<std::vector<std::wstring>>();
    create_task([hostDirs, imagePaths]() {
        for (auto& dir : *hostDirs) {
            std::wstring search = dir + L"*.png";
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(search.c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) continue;
            do {
                imagePaths->push_back(dir + fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }).then([weakThis, imagePaths]() {
        auto dispatcher = Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher;
        dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Low,
            ref new Windows::UI::Core::DispatchedHandler([weakThis, imagePaths]() {
                auto that = weakThis.Resolve<SwipeRevealBackground>();
                if (that == nullptr) return;

                if (!imagePaths->empty()) {
                    auto fromDisk = ref new Platform::Collections::Vector<MoonlightApp^>();
                    for (auto& path : *imagePaths) {
                        auto app = ref new MoonlightApp();
                        app->ImagePath = ref new Platform::String(path.c_str());
                        fromDisk->Append(app);
                    }
                    that->ShuffleAndApply(fromDisk);
                    // Best-effort background refresh for connected hosts
                    if (that->m_hosts != nullptr) {
                        auto targets = ref new Platform::Collections::Vector<MoonlightHost^>();
                        for (auto h : that->m_hosts)
                            if (h->Paired && h->Connected) targets->Append(h);
                        if (targets->Size > 0)
                            create_task([targets]() {
                                for (auto h : targets)
                                    try { h->UpdateApps(); } catch (...) {}
                            });
                    }
                    return;
                }

                // No cached images found — fall back to network
                that->LoadFromNetworkAsync();
            }));
    });
}

void SwipeRevealBackground::LoadFromNetworkAsync()
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
                if (collected->Size > 0)
                    that->ShuffleAndApply(collected);
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

// Grow the rectangular clip on the skewed FrontGrid.
// FrontGrid's SkewTransform(CenterY=canvasH/2) turns the clip's wipe-side edge into the
// "/" or "\" diagonal seen on screen. The opposite edge must be extended by kSlantH/2 so
// that after the skew it lands at (or beyond) the canvas boundary — without this, the
// skew would leave a triangular gap where the back image bleeds through.
void SwipeRevealBackground::UpdateDiagonalClip(float swept)
{
    if (m_wipeDir > 0) {
        // L→R: right edge = diagonal wipe front; extend left past x=0 to cover the gap
        float clipW = swept + kSlantH * 0.5f;
        if (clipW <= 0.0f)
            m_frontClipRect->Rect = Rect(0.0f, 0.0f, 0.0f, m_canvasH);
        else
            m_frontClipRect->Rect = Rect(-kSlantH * 0.5f, 0.0f, clipW, m_canvasH);
    } else {
        // R→L: left edge = diagonal wipe front; extend right past canvasW to cover the gap
        float clipW = swept + kSlantH * 0.5f;
        if (clipW <= 0.0f)
            m_frontClipRect->Rect = Rect(m_canvasW, 0.0f, 0.0f, m_canvasH);
        else
            m_frontClipRect->Rect = Rect(m_canvasW - swept, 0.0f, clipW, m_canvasH);
    }
}

void SwipeRevealBackground::InitSlides()
{
    // First image appears immediately in the back layer; second image is preloaded
    // into the front layer ready to wipe in after the first hold period.
    m_backAppIdx = FindNextAppWithImage(0);
    if (m_backAppIdx < 0) return;

    BackBrush->ImageSource = m_apps->GetAt(m_backAppIdx)->Image;

    m_frontAppIdx = FindNextAppWithImage(m_backAppIdx + 1);
    m_frontBrush->ImageSource = (m_frontAppIdx >= 0)
        ? m_apps->GetAt(m_frontAppIdx)->Image : nullptr;

    InitPanForLayer(m_backPanX,  m_backPanY,  m_backVX,  m_backVY);
    InitPanForLayer(m_frontPanX, m_frontPanY, m_frontVX, m_frontVY);
    BackPan->TranslateX      = m_backPanX;  BackPan->TranslateY  = m_backPanY;
    m_frontPan->TranslateX   = m_frontPanX; m_frontPan->TranslateY = m_frontPanY;

    m_wipeDir  = 1;
    m_wipeTick = 0;
    m_phase    = 0;  // hold first, then wipe
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

    if (m_frontAppIdx < 0) {
        if (++m_imageRetryTick >= 60) {
            m_imageRetryTick = 0;
            InitSlides();
        }
        return;
    }

    AdvancePan(m_backPanX, m_backPanY, m_backVX, m_backVY, BackPan);

    if (m_phase == 0) {
        if (++m_holdTick >= kHoldTicks) {
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
    if (m_timer) {
        m_timer->Stop();
        m_timer->Tick -= m_tickToken;
    }
    GlassEdge->Opacity = 0.0;
}
