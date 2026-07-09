#include "pch.h"
#include "AppPage.xaml.h"
#include "UI\Utilities\ImageHelpers.h"
#include "Utils.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>

using namespace Platform;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Foundation;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Hosting;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Graphics::Display;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;
using namespace concurrency;

namespace moonlight_xbox_dx {

namespace {

static double GetSharedAnimationDurationMs(AppPage^ page) {
    if (page == nullptr) return 250.0;

    Platform::String^ durationValue = nullptr;

    try {
        auto local = page->Resources != nullptr
            ? page->Resources->Lookup(ref new Platform::String(L"SharedAnimationDuration"))
            : nullptr;
        durationValue = dynamic_cast<Platform::String^>(local);
    } catch (...) {}

    if (durationValue == nullptr) {
        try {
            auto appRes = Application::Current != nullptr ? Application::Current->Resources : nullptr;
            auto global = appRes != nullptr
                ? appRes->Lookup(ref new Platform::String(L"SharedAnimationDuration"))
                : nullptr;
            durationValue = dynamic_cast<Platform::String^>(global);
        } catch (...) {}
    }

    return Utils::DurationStringToMs(durationValue);
}

// Derives the blur cache path from the source image path.
// Input:  "...\images\{hostId}\{appId}.png"  (absolute Win32 path with backslashes)
// Output: "...\images\{hostId}\blur\{appId}_bg.png"  (or _glow.png)
// Returns nullptr for ms-appx/ms-appdata URIs or any path without backslashes.
static Platform::String^ BlurCachePath(Platform::String^ imagePath, bool isGlow) {
    if (imagePath == nullptr) return nullptr;
    const wchar_t* s = imagePath->Data();
    int len = (int)wcslen(s);
    int lastSlash = -1, lastDot = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (s[i] == L'\\' && lastSlash < 0) lastSlash = i;
        if (s[i] == L'.' && lastDot   < 0) lastDot   = i;
    }
    if (lastSlash < 0 || lastDot <= lastSlash) return nullptr;
    auto dir     = ref new Platform::String(s, lastSlash + 1);
    auto base    = ref new Platform::String(s + lastSlash + 1, lastDot - lastSlash - 1);
    auto blurDir = Platform::String::Concat(dir, ref new Platform::String(L"blur\\"));
    auto suffix  = ref new Platform::String(isGlow ? L"_glow.png" : L"_bg.png");
    return Platform::String::Concat(blurDir, Platform::String::Concat(base, suffix));
}

// Saves an IRandomAccessStream to filePath, creating the parent directory if needed.
// Reads from position 0 regardless of stream->Position; leaves stream->Position unchanged.
// Blocks — call only from a background thread.
static void SaveBlurStreamSync(IRandomAccessStream^ stream, Platform::String^ filePath) {
    if (stream == nullptr || filePath == nullptr) return;
    const wchar_t* s = filePath->Data();
    int len = (int)wcslen(s);
    int lastSlash = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (s[i] == L'\\') { lastSlash = i; break; }
    }
    if (lastSlash < 0) return;
    auto dirPath  = ref new Platform::String(s, lastSlash);
    auto fileName = ref new Platform::String(s + lastSlash + 1);
    CreateDirectory(dirPath->Data(), nullptr);
    auto folder = create_task(StorageFolder::GetFolderFromPathAsync(dirPath)).get();
    auto file   = create_task(folder->CreateFileAsync(fileName,
                      CreationCollisionOption::ReplaceExisting)).get();
    auto fs     = create_task(file->OpenAsync(FileAccessMode::ReadWrite)).get();
    create_task(RandomAccessStream::CopyAsync(
        stream->GetInputStreamAt(0), fs->GetOutputStreamAt(0))).get();
    create_task(fs->FlushAsync()).get();
}

// Opens a cached blur PNG as a stream ready for BitmapImage::SetSourceAsync.
// Returns nullptr immediately if the file does not exist.
static concurrency::task<IRandomAccessStream^> OpenBlurCacheStreamAsync(Platform::String^ filePath) {
    if (filePath == nullptr || GetFileAttributes(filePath->Data()) == INVALID_FILE_ATTRIBUTES)
        return task_from_result<IRandomAccessStream^>(nullptr);
    return create_task(StorageFile::GetFileFromPathAsync(filePath))
        .then([](StorageFile^ file) -> concurrency::task<IRandomAccessStream^> {
            if (file == nullptr) return task_from_result<IRandomAccessStream^>(nullptr);
            return create_task(file->OpenReadAsync())
                .then([](IRandomAccessStream^ s) -> IRandomAccessStream^ { return s; });
        });
}

} // namespace

// ── AppPage::CenterSelectedItem ───────────────────────────────────────────────

void AppPage::CenterSelectedItem(int attempts, bool immediate) {
    auto queueRetry = [this, attempts, immediate]() {
        if (this->Dispatcher == nullptr) return;
        if (attempts <= 0) {
            // Dispatcher retries exhausted — hand off to LayoutUpdated so we retry
            // after the next layout pass instead of burning more message-queue slots.
            m_pendingCentering = true;
            return;
        }
        auto weakThis = WeakReference(this);
        try {
            this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                ref new DispatchedHandler([weakThis, attempts, immediate]() {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that == nullptr) return;
                    that->CenterSelectedItem(attempts - 1, immediate);
                }));
        } catch(...) {
            Utils::Logf("[AppPage] CenterSelectedItem: queueRetry RunAsync failed, retry chain broken\n");
        }
    };

    auto lv = this->AppsGrid;
    if (lv == nullptr || lv->SelectedIndex < 0 || lv->SelectedItem == nullptr) return;

    if (m_scrollViewer == nullptr) m_scrollViewer = FindScrollViewer(lv);
    if (m_scrollViewer == nullptr) {
        queueRetry();
        return;
    }

    auto selectedItem = lv->SelectedItem;
    auto container = dynamic_cast<ListViewItem^>(lv->ContainerFromItem(selectedItem));
    if (container == nullptr) {
        try { lv->ScrollIntoView(selectedItem); } catch(...) {}
        queueRetry();
        return;
    }

    if (container->ActualWidth <= 0.0 || container->ActualHeight <= 0.0) {
        queueRetry();
        return;
    }

    double viewport = m_isGridLayout ? m_scrollViewer->ViewportHeight : m_scrollViewer->ViewportWidth;
    if (viewport <= 0.0) {
        queueRetry();
        return;
    }

    try {
        auto padding = lv->Padding;
        double desiredEdgePadding = m_isGridLayout
            ? 0.0
            : std::max(0.0, (viewport - container->ActualWidth) * 0.5);
        if (std::fabs(padding.Left - desiredEdgePadding) > 0.5 || std::fabs(padding.Right - desiredEdgePadding) > 0.5) {
            padding.Left = desiredEdgePadding;
            padding.Right = desiredEdgePadding;
            lv->Padding = padding;
            try { lv->UpdateLayout(); } catch(...) {}
            queueRetry();
            return;
        }
    } catch(...) {}

    auto contentElement = dynamic_cast<UIElement^>(m_scrollViewer->Content);
    if (contentElement == nullptr) {
        queueRetry();
        return;
    }

    Point origin; origin.X = 0.0f; origin.Y = 0.0f;
    Point inContent;
    try {
        inContent = container->TransformToVisual(contentElement)->TransformPoint(origin);
    } catch(...) {
        queueRetry();
        return;
    }

    double itemCenter = m_isGridLayout
        ? (inContent.Y + (container->ActualHeight * 0.5))
        : (inContent.X + (container->ActualWidth  * 0.5));

    double scrollable = m_isGridLayout ? m_scrollViewer->ScrollableHeight : m_scrollViewer->ScrollableWidth;
    double current = m_isGridLayout ? m_scrollViewer->VerticalOffset : m_scrollViewer->HorizontalOffset;
    double target = itemCenter - (viewport * 0.5);
    target = std::max(0.0, std::min(target, std::max(0.0, scrollable)));

    m_pendingCentering = false; // layout is valid; centering is proceeding

    // On first navigation to this page the selected container won't have keyboard
    // focus because it was selected programmatically. Set it here exactly once,
    // at the point where we know the container is realized and measured, so that
    // gamepad A fires ItemClick immediately without requiring manual navigation.
    if (!m_initialFocusApplied) {
        m_initialFocusApplied = true;
        try {
            if (container != nullptr)
                container->Focus(Windows::UI::Xaml::FocusState::Programmatic);
            else
                lv->Focus(Windows::UI::Xaml::FocusState::Programmatic);
        } catch(...) {
            Utils::Logf("[AppPage] CenterSelectedItem: initial Focus() failed\n");
        }
    }

    if (std::fabs(target - current) < 0.5) return;

    if (immediate) {
        try {
            if (m_isGridLayout) m_scrollViewer->ChangeView(nullptr, target, nullptr, true);
            else                m_scrollViewer->ChangeView(target, nullptr, nullptr, true);
        } catch(...) {}
        return;
    }

    const double durationMs = GetSharedAnimationDurationMs(this);
    if (durationMs <= 1.0) {
        try {
            if (m_isGridLayout) m_scrollViewer->ChangeView(nullptr, target, nullptr, true);
            else                m_scrollViewer->ChangeView(target, nullptr, nullptr, true);
        } catch(...) {}
        return;
    }

    const bool isGrid = m_isGridLayout;
    const double start = current;
    const double delta = target - start;
    const unsigned int version = ++m_centeringAnimationVersion;

    auto weakThis = WeakReference(this);
    auto renderingToken = std::make_shared<Windows::Foundation::EventRegistrationToken>();
    auto startTime = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now());

    *renderingToken = Windows::UI::Xaml::Media::CompositionTarget::Rendering +=
        ref new EventHandler<Platform::Object^>(
            [weakThis, renderingToken, startTime, durationMs, start, delta, isGrid, version](Platform::Object^, Platform::Object^) {
                auto that = weakThis.Resolve<AppPage>();
                if (that == nullptr || that->m_scrollViewer == nullptr || that->m_centeringAnimationVersion != version) {
                    try { Windows::UI::Xaml::Media::CompositionTarget::Rendering -= *renderingToken; }
                    catch(...) { Utils::Logf("[AppPage] CenterSelectedItem: Rendering -= failed (stale-version path), handler leaked\n"); }
                    return;
                }

                auto now = std::chrono::steady_clock::now();
                double elapsedMs = std::chrono::duration<double, std::milli>(now - *startTime).count();
                double t = std::min(1.0, elapsedMs / durationMs);
                double eased = 1.0 - std::pow(1.0 - t, 3.0);
                double value = start + (delta * eased);

                try {
                    if (isGrid) that->m_scrollViewer->ChangeView(nullptr, value, nullptr, true);
                    else        that->m_scrollViewer->ChangeView(value, nullptr, nullptr, true);
                } catch(...) {}

                if (t >= 1.0) {
                    try { Windows::UI::Xaml::Media::CompositionTarget::Rendering -= *renderingToken; }
                    catch(...) { Utils::Logf("[AppPage] CenterSelectedItem: Rendering -= failed (completion path), handler leaked\n"); }
                }
            });
}

// ── AppPage::DoGridCentering ──────────────────────────────────────────────────

void AppPage::DoGridCentering() {
    if (!m_isGridLayout) return;
    CenterSelectedItem(3, false);
}

// ── AppPage::UpdateItemHeights ────────────────────────────────────────────────
// Clears any stale explicit Height on the AspectRatioBox of each realized list
// container so XAML can size it naturally from the Row1 panel constraint.
//
// The list template's AspectRatioBox has no Height set in XAML (unlike the grid
// template's Height="250").  The panel gives each container the full viewport
// height; Row1 (4* after capped rows 0/2) then provides the correct image height
// automatically.  Setting an explicit pixel value here — e.g. ActualHeight*0.85
// — overflows Row1 and causes the artwork to be clipped.
//
// Recycled containers that went through a previous toggle may still carry a
// stale explicit Height because Phase 0 preserves the visual tree.  This method
// is called from the post-toggle RunAsync (after UpdateLayout) precisely to
// clear those stale values on containers whose visual trees are already inflated.
// Freshly created containers (Phase 0, tree not yet inflated) return null from
// find() and are skipped — they already have NaN from the template.

void AppPage::UpdateItemHeights() {
    try {
        if (this->AppsGrid == nullptr) return;

        // Grid template already has Height="250" on AspectRatioBox — leave it alone.
        if (m_isGridLayout) return;

        for (unsigned int i = 0; i < this->AppsGrid->Items->Size; ++i) {
            auto container = dynamic_cast<ListViewItem^>(this->AppsGrid->ContainerFromIndex(i));
            if (container == nullptr) continue;

            std::function<DependencyObject^(DependencyObject^)> find = [&](DependencyObject^ parent) -> DependencyObject^ {
                if (parent == nullptr) return nullptr;
                int count = VisualTreeHelper::GetChildrenCount(parent);
                for (int j = 0; j < count; ++j) {
                    auto child = VisualTreeHelper::GetChild(parent, j);
                    auto fe = dynamic_cast<FrameworkElement^>(child);
                    if (fe != nullptr && fe->GetType()->FullName == "moonlight_xbox_dx.AspectRatioBox") return child;
                    auto rec = find(child);
                    if (rec != nullptr) return rec;
                }
                return nullptr;
            };

            auto found = find(container);
            if (found == nullptr) continue;
            auto fe = dynamic_cast<FrameworkElement^>(found);
            if (fe == nullptr) continue;
            if (std::isnan(fe->Height)) continue;

            fe->Height = std::nan("");
            fe->InvalidateMeasure();
            fe->UpdateLayout();
        }
    } catch(...) {
        Utils::Logf("[AppPage] UpdateItemHeights: failed, stale Height may cause clipped artwork\n");
    }
}

// ── AppPage::AppsGrid_ContainerContentChanging ───────────────────────────────
// Fires for every container that enters or leaves the virtualization window.
// Phase 0: DataTemplate may not be fully inflated yet — only register Phase 1.
// Phase 1: recover page-level blur for the selected item if it was computed
//          before this container was realized. Visual state is driven entirely
//          by the StateTrigger on IsSelected — no GoToState needed here.
// InRecycleQueue: container is leaving the viewport — reset margin to clean state.

void AppPage::AppsGrid_ContainerContentChanging(
    Windows::UI::Xaml::Controls::ListViewBase^ sender,
    Windows::UI::Xaml::Controls::ContainerContentChangingEventArgs^ args)
{
    auto container = dynamic_cast<ListViewItem^>(args->ItemContainer);
    if (container == nullptr) return;

    // ── Recycled: leaving the viewport ───────────────────────────────────────
    if (args->InRecycleQueue) {
        try {
            Windows::UI::Xaml::Thickness zero;
            zero.Left = zero.Top = zero.Right = zero.Bottom = 0.0;
            container->Margin = zero;
        } catch(...) {
            Utils::Logf("[AppPage] AppsGrid_ContainerContentChanging: Margin reset failed on recycle, stale margin may persist into reuse\n");
        }
        return;
    }

    // ── Phase 0: DataTemplate may not be inflated yet ────────────────────────
    if (args->Phase == 0) {
        Platform::WeakReference weakThis(this);
        args->RegisterUpdateCallback(
            ref new TypedEventHandler<ListViewBase^, ContainerContentChangingEventArgs^>(
                [weakThis](ListViewBase^ s, ContainerContentChangingEventArgs^ a) {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that == nullptr) return;
                    try { that->AppsGrid_ContainerContentChanging(s, a); }
                    catch(...) { Utils::Logf("[AppPage] AppsGrid_ContainerContentChanging: Phase1 recursive dispatch failed\n"); }
                }));
        return;
    }

    // ── Phase 1+: visual tree is fully inflated ───────────────────────────────
    // Recover the page-level blur for the selected item if blur completed before
    // this container entered the viewport.
    try {
        auto app = dynamic_cast<MoonlightApp^>(args->Item);
        if (app != nullptr && app->IsSelected && app->BlurredImage != nullptr)
            FadeInBlurIfSelected(app, app->BlurredImage);
    } catch(...) {
        Utils::Logf("[AppPage] AppsGrid_ContainerContentChanging: Phase1 blur recovery failed\n");
    }
}

// ── AppPage::ApplySelectionVisuals ────────────────────────────────────────────
// Drives all selection-dependent side effects. Visual state on the item
// containers is handled declaratively by the StateTrigger on IsSelected in
// the item templates — no container lookup or GoToState call needed here.

void AppPage::ApplySelectionVisuals(MoonlightApp^ app, bool animate) {
    if (app == nullptr) return;

    // Update IsSelected on the data objects. The StateTrigger in the item template
    // binds to IsSelected and drives the visual state automatically, with no
    // dependency on container realization timing.
    MoonlightApp^ prev = m_selectedApp;
    if (prev != nullptr && prev->Id != app->Id) {
        try { prev->IsSelected = false; }
        catch(...) { Utils::Logf("[AppPage] ApplySelectionVisuals: prev->IsSelected=false failed for app id=%d, stale selected visual may persist\n", prev->Id); }
    }
    m_selectedApp = app;
    try { app->IsSelected = true; }
    catch(...) { Utils::Logf("[AppPage] ApplySelectionVisuals: app->IsSelected=true failed for app id=%d, selection visual desynced\n", app->Id); }

    // Blur (page-level background, outside the item containers)
    if (app->BlurredImage == nullptr) BlurAppImage(app);
    else FadeInBlurIfSelected(app, app->BlurredImage);

    try {
        if (this->SelectedAppText == nullptr || this->SelectedAppBox == nullptr) return;
        Platform::String^ newText = app->Name != nullptr ? app->Name : ref new Platform::String(L"");
        this->SelectedAppBox->Visibility = Windows::UI::Xaml::Visibility::Visible;
        this->SelectedAppText->Visibility = Windows::UI::Xaml::Visibility::Visible;
        this->SelectedAppText->Foreground = ref new SolidColorBrush(Windows::UI::Colors::White);

        Windows::UI::Xaml::Media::Animation::Storyboard^ showSb = nullptr;
        Windows::UI::Xaml::Media::Animation::Storyboard^ hideSb = nullptr;
        try {
            auto res = this->Resources;
            if (res != nullptr) {
                try { showSb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(res->Lookup(ref new Platform::String(L"ShowSelectedAppStoryboard"))); } catch(...) {}
                try { hideSb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(res->Lookup(ref new Platform::String(L"HideSelectedAppStoryboard"))); } catch(...) {}
            }
        } catch(...) {}

        bool alreadyVisible = this->SelectedAppBox->Opacity > 0.01;
        if (animate && prev != nullptr && alreadyVisible && hideSb != nullptr && showSb != nullptr) {
            const unsigned int animVer = ++m_appTextAnimVersion;
            auto weakThis = WeakReference(this);
            auto capturedText = newText;
            auto capturedShow = showSb;
            auto token = std::make_shared<Windows::Foundation::EventRegistrationToken>();
            auto capturedAppId = app->Id;
            *token = hideSb->Completed += ref new Windows::Foundation::EventHandler<Platform::Object^>(
                [weakThis, capturedText, hideSb, capturedShow, token, animVer, capturedAppId](Platform::Object^, Platform::Object^) mutable {
                    try { hideSb->Completed -= *token; }
                    catch(...) { Utils::Logf("[AppPage] ApplySelectionVisuals: hideSb->Completed -= failed, handler leaked\n"); }
                    auto that = weakThis.Resolve<AppPage>();
                    if (that == nullptr || that->m_appTextAnimVersion != animVer) return;
                    try {
                        if (that->SelectedAppText != nullptr) that->SelectedAppText->Text = capturedText;
                        if (capturedShow != nullptr) capturedShow->Begin();
                    } catch(...) {
                        Utils::Logf("[AppPage] ApplySelectionVisuals: post-hide text/show update failed for app id=%d\n", capturedAppId);
                    }
                });
            hideSb->Begin();
        } else {
            this->SelectedAppText->Text = newText;
            if (showSb != nullptr) showSb->Begin();
        }
    } catch(...) {
        Utils::Logf("[AppPage] ApplySelectionVisuals: text overlay update failed for app id=%d\n", app->Id);
    }

    CenterSelectedItem(4, false);
}

// ── AppPage::AppsGrid_SelectionChanged ───────────────────────────────────────

void AppPage::AppsGrid_SelectionChanged(Platform::Object^ sender, SelectionChangedEventArgs^ e) {
    auto lv = dynamic_cast<ListView^>(sender);
    if (lv == nullptr || lv->SelectedIndex < 0) return;
    auto app = dynamic_cast<MoonlightApp^>(lv->SelectedItem);
    if (app == nullptr) return;
    ApplySelectionVisuals(app, true);
}

// ── AppPage::ApplyBlur ────────────────────────────────────────────────────────

concurrency::task<IRandomAccessStream^> AppPage::ApplyBlur(MoonlightApp^ app, float blurDip, float padDip) {
    if (app == nullptr) return concurrency::task_from_result<IRandomAccessStream^>(nullptr);

    Platform::String^ path = app->ImagePath;

    return concurrency::create_task(ImageHelpers::LoadSoftwareBitmapFromUriOrPathAsync(path))
        .then([this, app, blurDip, padDip](SoftwareBitmap^ softwareBitmap) -> concurrency::task<IRandomAccessStream^> {
        if (softwareBitmap == nullptr) return concurrency::task_from_result<IRandomAccessStream^>(nullptr);

        unsigned int ui_targetW = 0, ui_targetH = 0;
        double ui_dpi = 96.0;
        try {
            // Try to find AppImageRect to determine the render size
            FrameworkElement^ fe_for_size = dynamic_cast<FrameworkElement^>(this->FindName("AppImageRect"));
            if (fe_for_size == nullptr && this->AppsGrid != nullptr) {
                auto container = dynamic_cast<ListViewItem^>(this->AppsGrid->ContainerFromItem(app));
                if (container != nullptr) {
                    auto found = FindChildByName(container, ref new Platform::String(L"AppImageRect"));
                    if (found == nullptr) found = FindChildByName(container, ref new Platform::String(L"AppImageBlurRect"));
                    fe_for_size = dynamic_cast<FrameworkElement^>(found);
                }
            }

            if (fe_for_size != nullptr) {
                double aw = 0.0, ah = 0.0;
                try { aw = fe_for_size->ActualWidth;  } catch(...) {}
                try { ah = fe_for_size->ActualHeight; } catch(...) {}

                if ((aw <= 0 || ah <= 0) && this->AppsGrid != nullptr) {
                    try {
                        auto container = dynamic_cast<ListViewItem^>(this->AppsGrid->ContainerFromItem(app));
                        if (container != nullptr) {
                            auto tryFE = [&](const wchar_t* name) {
                                auto fe = dynamic_cast<FrameworkElement^>(FindChildByName(container, ref new Platform::String(name)));
                                if (fe != nullptr) {
                                    try { if (aw <= 0) aw = fe->ActualWidth;  } catch(...) {}
                                    try { if (ah <= 0) ah = fe->ActualHeight; } catch(...) {}
                                }
                            };
                            tryFE(L"AppAspectRatioBox");
                            tryFE(L"ItemGrid");
                            tryFE(L"AppImageBlurRect");
                            if (aw <= 0) { try { aw = container->ActualWidth; } catch(...) {} }
                        }
                        if (aw <= 0) { try { aw = this->AppsGrid->ActualWidth; } catch(...) {} }
                    } catch(...) {}
                }

                if (aw > 0 && ah > 0) {
                    try {
                        auto di = DisplayInformation::GetForCurrentView();
                        double dpi = di != nullptr ? di->LogicalDpi : 96.0;
                        ui_dpi = dpi;
                        ui_targetW = (unsigned int)std::max(1u, (unsigned int)std::round(aw * dpi / 96.0));
                        ui_targetH = (unsigned int)std::max(1u, (unsigned int)std::round(ah * dpi / 96.0));
                    } catch(...) { ui_targetW = ui_targetH = 0; ui_dpi = 96.0; }
                }
            }
        } catch(...) { ui_targetW = ui_targetH = 0; }

        unsigned int targetW = ui_targetW != 0 ? ui_targetW : softwareBitmap->PixelWidth;
        unsigned int targetH = ui_targetH != 0 ? ui_targetH : softwareBitmap->PixelHeight;

        // Expand target dimensions by padDip on each side so the blurred image
        // fills the blur rect (which is larger than the main image by BlurAmount)
        unsigned int glowTargetW = targetW, glowTargetH = targetH;
        if (padDip > 0.0f && targetW > 0 && targetH > 0) {
            unsigned int padPx = (unsigned int)std::round((double)padDip * ui_dpi / 96.0);
            if (padPx > 0) {
                glowTargetW = targetW + 2 * padPx;
                glowTargetH = targetH + 2 * padPx;
            }
        }

        try { ImageHelpers::AdjustSaturation(softwareBitmap, kBackgroundSaturation); } catch(...) {}
        return ImageHelpers::CreateMaskedBlurredPngStreamAsync(
            softwareBitmap, glowTargetW, glowTargetH, ui_dpi, blurDip);
    });
}

// ── AppPage::HandleBlurStreamReady ───────────────────────────────────────────
// Assigns a completed blur stream to selApp on the UI thread. isBackground selects
// whether this sets BlurredImage (clearing m_blurInProgressIds and triggering the
// page-level fade-in) or GlowImage (the per-item glow, no retry gate of its own —
// BlurAppImage's BlurredImage-null check gates both, so a failure here leaves
// GlowImage permanently unset for this app).

void AppPage::HandleBlurStreamReady(MoonlightApp^ selApp, Platform::WeakReference weakThis,
                                     IRandomAccessStream^ stream, bool isBackground) {
    if (stream == nullptr) {
        if (!isBackground) return;
        Utils::Logf("[AppPage] BlurAppImage: background stream null for app id=%d\n", selApp->Id);
        // Erase so the next selection can retry rather than being blocked forever.
        this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
            ref new DispatchedHandler([weakThis, selApp]() {
                auto ui = weakThis.Resolve<AppPage>();
                if (ui) ui->m_blurInProgressIds.erase(selApp->Id);
            }));
        return;
    }

    this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
        ref new DispatchedHandler([selApp, stream, weakThis, isBackground]() {
        try {
            auto thatInner = weakThis.Resolve<AppPage>();
            if (thatInner == nullptr || selApp == nullptr) return;
            auto img = ref new BitmapImage();
            create_task(img->SetSourceAsync(stream))
                .then([weakThis, selApp, img, isBackground]() {
                try {
                    auto thatCont = weakThis.Resolve<AppPage>();
                    if (thatCont == nullptr) return;
                    if (isBackground) {
                        thatCont->m_blurInProgressIds.erase(selApp->Id);
                        selApp->BlurredImage = img;
                        // Mirror to the live binding target if SetAt swapped the reference.
                        if (thatCont->m_selectedApp != nullptr &&
                            thatCont->m_selectedApp != selApp &&
                            thatCont->m_selectedApp->Id == selApp->Id)
                            thatCont->m_selectedApp->BlurredImage = img;
                        try { thatCont->FadeInBlurIfSelected(selApp, img); } catch(...) {}
                    } else {
                        selApp->GlowImage = img;
                        // Mirror to the live binding target if SetAt swapped the reference.
                        if (thatCont->m_selectedApp != nullptr &&
                            thatCont->m_selectedApp != selApp &&
                            thatCont->m_selectedApp->Id == selApp->Id)
                            thatCont->m_selectedApp->GlowImage = img;
                    }
                } catch(...) {
                    if (isBackground)
                        Utils::Logf("[AppPage] BlurAppImage: assign-BlurredImage step failed for app id=%d, m_blurInProgressIds entry stuck\n", selApp->Id);
                    else
                        Utils::Logf("[AppPage] BlurAppImage: assign-GlowImage step failed for app id=%d, GlowImage permanently null (BlurredImage gate blocks retry)\n", selApp->Id);
                }
            }, concurrency::task_continuation_context::use_current());
        } catch(...) {
            if (isBackground)
                Utils::Logf("[AppPage] BlurAppImage: SetSourceAsync setup failed for app id=%d, m_blurInProgressIds entry stuck\n", selApp->Id);
            else
                Utils::Logf("[AppPage] BlurAppImage: glow SetSourceAsync setup failed for app id=%d, GlowImage permanently null\n", selApp->Id);
        }
    }));
}

// ── AppPage::BlurAppImage ─────────────────────────────────────────────────────

void AppPage::BlurAppImage(MoonlightApp^ selApp) {
    try {
        if (selApp->BlurredImage != nullptr) return;
        if (m_blurInProgressIds.count(selApp->Id)) return; // async already running
        m_blurInProgressIds.insert(selApp->Id);
        bool isGrid = this->m_isGridLayout;
        Platform::WeakReference weakThis(this);

        // Read glow blur amount from the shared XAML resource so it stays in sync
        // with the padding/margin converters driven by the same value.
        float glowBlurAmount = 16.0f; // fallback if resource lookup fails
        try {
            auto boxed = Resources->Lookup("BlurAmount");
            auto pv = dynamic_cast<Windows::Foundation::IPropertyValue^>(boxed);
            if (pv != nullptr) glowBlurAmount = (float)pv->GetDouble();
        } catch(...) {}

        // Returns a stream from the blur cache if it exists, otherwise runs ApplyBlur
        // and saves the result so future calls skip the GPU work.
        auto getOrComputeStream = [this, selApp](float blurDip, float padDip,
                                                  Platform::String^ cachePath)
            -> concurrency::task<IRandomAccessStream^>
        {
            if (cachePath != nullptr && GetFileAttributes(cachePath->Data()) != INVALID_FILE_ATTRIBUTES)
                return OpenBlurCacheStreamAsync(cachePath);
            return ApplyBlur(selApp, blurDip, padDip)
                .then([cachePath](IRandomAccessStream^ stream) -> IRandomAccessStream^ {
                    if (stream != nullptr && cachePath != nullptr) {
                        try { SaveBlurStreamSync(stream, cachePath); }
                        catch(...) { Utils::Logf("[AppPage] BlurAppImage: cache write failed, blur will be recomputed every launch\n"); }
                        stream->Seek(0);
                    }
                    return stream;
                }, concurrency::task_continuation_context::use_arbitrary());
        };

        // Background blur — always generated, used for page background
        auto bgCachePath = BlurCachePath(selApp->ImagePath, false);
        try {
            getOrComputeStream(kBlurAmountBackground, 0.0f, bgCachePath)
                .then([selApp, weakThis](IRandomAccessStream^ stream) {
                try {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that == nullptr) return;
                    that->HandleBlurStreamReady(selApp, weakThis, stream, /*isBackground*/ true);
                } catch(...) {
                    Utils::Logf("[AppPage] BlurAppImage: background RunAsync dispatch failed for app id=%d, m_blurInProgressIds entry stuck\n", selApp->Id);
                }
            }, concurrency::task_continuation_context::use_current());
        } catch(...) {
            Utils::Logf("[AppPage] BlurAppImage: background getOrComputeStream setup failed for app id=%d, m_blurInProgressIds entry stuck\n", selApp->Id);
        }

        // Glow blur — only in list mode, used for the per-item glow rect
        if (!isGrid) {
            auto glowCachePath = BlurCachePath(selApp->ImagePath, true);
            try {
                getOrComputeStream(glowBlurAmount, kBlurGlowPaddingDip, glowCachePath)
                    .then([selApp, weakThis](IRandomAccessStream^ stream) {
                    try {
                        auto that = weakThis.Resolve<AppPage>();
                        if (that == nullptr) return;
                        that->HandleBlurStreamReady(selApp, weakThis, stream, /*isBackground*/ false);
                    } catch(...) {
                        Utils::Logf("[AppPage] BlurAppImage: glow RunAsync dispatch failed for app id=%d, GlowImage permanently null\n", selApp->Id);
                    }
                }, concurrency::task_continuation_context::use_current());
            } catch(...) {
                Utils::Logf("[AppPage] BlurAppImage: glow getOrComputeStream setup failed for app id=%d, GlowImage permanently null\n", selApp->Id);
            }
        }
    } catch(...) {
        Utils::Logf("[AppPage] BlurAppImage: unhandled failure for app id=%d, m_blurInProgressIds entry may be stuck\n", selApp->Id);
    }
}

// ── AppPage::FadeInBlurIfSelected ────────────────────────────────────────────

void AppPage::FadeInBlurIfSelected(MoonlightApp^ app, BitmapImage^ img) {
    try {
        if (app == nullptr || img == nullptr) return;

        // Determine whether this app is the current selection. SelectedItem is the
        // primary check, but it can be transiently null while ApplyAppFilter is
        // clearing and repopulating the collection. m_selectedApp is the reliable
        // fallback — it is set by ApplySelectionVisuals and is never cleared by
        // collection churn.
        bool isSelected = false;
        try {
            if (this->AppsGrid != nullptr) {
                auto selItem = dynamic_cast<MoonlightApp^>(this->AppsGrid->SelectedItem);
                if (selItem != nullptr && selItem->Id == app->Id) isSelected = true;
            }
            if (!isSelected && m_selectedApp != nullptr && m_selectedApp->Id == app->Id)
                isSelected = true;
        } catch(...) {}

        if (!isSelected) return;

        try {
            auto vm = this->ViewModel;
            if (vm != nullptr) vm->TransitionToBlurredImage(img);
        } catch(...) {
            Utils::Logf("[AppPage] FadeInBlurIfSelected: TransitionToBlurredImage failed for app id=%d\n", app->Id);
        }
    } catch(...) {}
}

} // namespace moonlight_xbox_dx
