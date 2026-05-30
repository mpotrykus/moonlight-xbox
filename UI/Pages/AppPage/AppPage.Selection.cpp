#include "pch.h"
#include "AppPage.xaml.h"
#include "UI\Utilities\ImageHelpers.h"
#include "Utils.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>

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
        if (attempts <= 0 || this->Dispatcher == nullptr) return;
        auto weakThis = WeakReference(this);
        try {
            this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                ref new DispatchedHandler([weakThis, attempts, immediate]() {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that == nullptr) return;
                    that->CenterSelectedItem(attempts - 1, immediate);
                }));
        } catch(...) {}
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
        if (!m_isGridLayout) {
            double desiredEdgePadding = std::max(0.0, (viewport - container->ActualWidth) * 0.5);
            if (std::fabs(padding.Left - desiredEdgePadding) > 0.5 || std::fabs(padding.Right - desiredEdgePadding) > 0.5) {
                padding.Left = desiredEdgePadding;
                padding.Right = desiredEdgePadding;
                lv->Padding = padding;
                try { lv->UpdateLayout(); } catch(...) {}
                queueRetry();
                return;
            }
        } else {
            if (std::fabs(padding.Left) > 0.5 || std::fabs(padding.Right) > 0.5) {
                padding.Left = 0.0;
                padding.Right = 0.0;
                lv->Padding = padding;
                try { lv->UpdateLayout(); } catch(...) {}
                queueRetry();
                return;
            }
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
                    try { Windows::UI::Xaml::Media::CompositionTarget::Rendering -= *renderingToken; } catch(...) {}
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
                    try { Windows::UI::Xaml::Media::CompositionTarget::Rendering -= *renderingToken; } catch(...) {}
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

            if (!std::isnan(fe->Height)) {
                fe->Height = std::nan("");
                fe->InvalidateMeasure();
                fe->UpdateLayout();
            }
        }
    } catch(...) {}
}

// ── AppPage::AppsGrid_ContainerContentChanging ───────────────────────────────
// Fires for every container that enters or leaves the virtualization window.
// Phase 0: DataTemplate may not be fully inflated yet — only register Phase 1.
// Phase 1: visual tree is ready; set height, visual state, and selection visuals.
// InRecycleQueue: container is leaving the viewport — reset to clean state.

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
        } catch(...) {}
        return;
    }

    // ── Phase 0: DataTemplate may not be inflated yet ────────────────────────
    // Don't touch the visual tree here — just request Phase 1 where it's safe.
    if (args->Phase == 0) {
        Platform::WeakReference weakThis(this);
        args->RegisterUpdateCallback(
            ref new TypedEventHandler<ListViewBase^, ContainerContentChangingEventArgs^>(
                [weakThis](ListViewBase^ s, ContainerContentChangingEventArgs^ a) {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that) try { that->AppsGrid_ContainerContentChanging(s, a); } catch(...) {}
                }));
        return;
    }

    // ── Phase 1+: visual tree is fully inflated ───────────────────────────────
    auto lv = dynamic_cast<ListView^>(sender);

    // If this is the selected container, apply full selection visuals and blur.
    // Relevant mainly in grid mode where keyboard nav can leave the selected
    // row unrealized; in list mode the selected item is always on-screen.
    try {
        bool isSelected = false;
        if (lv != nullptr && lv->SelectedIndex >= 0) {
            int idx = (int)lv->IndexFromContainer(container);
            isSelected = (idx >= 0 && idx == (int)lv->SelectedIndex);
        }
        if (isSelected) {
            auto item = dynamic_cast<MoonlightApp^>(args->Item);
            if (item != nullptr) {
                if (item->BlurredImage == nullptr)
                    try { BlurAppImage(item); } catch(...) {}
                else
                    try { FadeInBlurIfSelected(item, item->BlurredImage); } catch(...) {}
            }
        }
    } catch(...) {}
}

// ── AppPage::AppsGrid_SelectionChanged ───────────────────────────────────────

void AppPage::AppsGrid_SelectionChanged(Platform::Object^ sender, SelectionChangedEventArgs^ e) {
    auto lv = dynamic_cast<ListView^>(sender);
    if (lv == nullptr || lv->SelectedIndex < 0) return;
    auto item = lv->SelectedItem;
    if (item == nullptr) return;

    Platform::Object^ prevItem = nullptr;
    try {
        if (e != nullptr && e->RemovedItems != nullptr && e->RemovedItems->Size > 0)
            prevItem = e->RemovedItems->GetAt(0);
    } catch(...) {}

    // Realize container
    auto findOrEnsureContainer = [&](Platform::Object^ it) -> ListViewItem^ {
        if (it == nullptr) return nullptr;
        ListViewItem^ c = nullptr;
        try { c = dynamic_cast<ListViewItem^>(lv->ContainerFromItem(it)); } catch(...) {}
        if (c == nullptr) {
            try { lv->ScrollIntoView(it); } catch(...) {}
            try { c = dynamic_cast<ListViewItem^>(lv->ContainerFromItem(it)); } catch(...) {}
        }
        return c;
    };

    ListViewItem^ container = findOrEnsureContainer(item);

    ListViewItem^ prevContainer = nullptr;
    if (prevItem != nullptr) {
        try { prevContainer = dynamic_cast<ListViewItem^>(lv->ContainerFromItem(prevItem)); } catch(...) {}
    }

    // Blur
    try {
        auto selApp = dynamic_cast<MoonlightApp^>(item);
        if (selApp != nullptr) {
            if (selApp->BlurredImage == nullptr) BlurAppImage(selApp);
            else FadeInBlurIfSelected(selApp, selApp->BlurredImage);
        }
    } catch(...) {}

    // Update SelectedApp text overlay (fade out → update text → fade in on change)
    try {
        auto selApp = dynamic_cast<MoonlightApp^>(lv->SelectedItem);
        auto res = this->Resources;
        if (selApp != nullptr && this->SelectedAppText != nullptr && this->SelectedAppBox != nullptr) {
            Platform::String^ newText = (selApp->Name != nullptr) ? selApp->Name : ref new Platform::String(L"");
            this->SelectedAppBox->Visibility = Windows::UI::Xaml::Visibility::Visible;
            this->SelectedAppText->Visibility = Windows::UI::Xaml::Visibility::Visible;
            this->SelectedAppText->Foreground = ref new SolidColorBrush(Windows::UI::Colors::White);

            Windows::UI::Xaml::Media::Animation::Storyboard^ showSb = nullptr;
            Windows::UI::Xaml::Media::Animation::Storyboard^ hideSb = nullptr;
            if (res != nullptr) {
                try { showSb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(res->Lookup(ref new Platform::String(L"ShowSelectedAppStoryboard"))); } catch(...) {}
                try { hideSb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(res->Lookup(ref new Platform::String(L"HideSelectedAppStoryboard"))); } catch(...) {}
            }

            bool alreadyVisible = this->SelectedAppBox->Opacity > 0.01;
            if (prevItem != nullptr && alreadyVisible && hideSb != nullptr && showSb != nullptr) {
                const unsigned int animVer = ++m_appTextAnimVersion;
                auto weakThis = WeakReference(this);
                auto capturedText = newText;
                auto capturedShow = showSb;
                auto token = std::make_shared<Windows::Foundation::EventRegistrationToken>();
                *token = hideSb->Completed += ref new Windows::Foundation::EventHandler<Platform::Object^>(
                    [weakThis, capturedText, hideSb, capturedShow, token, animVer](Platform::Object^, Platform::Object^) mutable {
                        try { hideSb->Completed -= *token; } catch(...) {}
                        auto that = weakThis.Resolve<AppPage>();
                        if (that == nullptr || that->m_appTextAnimVersion != animVer) return;
                        try {
                            if (that->SelectedAppText != nullptr) that->SelectedAppText->Text = capturedText;
                            if (capturedShow != nullptr) capturedShow->Begin();
                        } catch(...) {}
                    });
                hideSb->Begin();
            } else {
                this->SelectedAppText->Text = newText;
                if (showSb != nullptr) showSb->Begin();
            }
        }
    } catch(...) {}

    try { CenterSelectedItem(4, false); } catch(...) {}
}

// ── Local helper: capture a XAML element as a SoftwareBitmap ────────────────

static concurrency::task<SoftwareBitmap^> CaptureXamlElementAsync(FrameworkElement^ element) {
    concurrency::task_completion_event<SoftwareBitmap^> tce;
    if (element == nullptr) { tce.set(nullptr); return concurrency::create_task(tce); }

    auto dispatched = ref new DispatchedHandler([element, tce]() mutable {
        try {
            try {
                if (element->Visibility != Visibility::Visible) {
                    Utils::Log("CaptureXamlElementAsync: skipping non-visible element\n");
                    tce.set(nullptr); return;
                }
            } catch(...) {}

            double aw = 0.0, ah = 0.0;
            try { aw = element->ActualWidth;  } catch(...) {}
            try { ah = element->ActualHeight; } catch(...) {}
            if (aw <= 0.0 || ah <= 0.0) {
                Utils::Log("CaptureXamlElementAsync: skipping zero-sized element\n");
                tce.set(nullptr); return;
            }

            auto rtb = ref new RenderTargetBitmap();
            create_task(rtb->RenderAsync(element)).then([rtb]() {
                return create_task(rtb->GetPixelsAsync());
            }).then([rtb, tce](concurrency::task<IBuffer^> prev) {
                try {
                    auto pixels = prev.get();
                    if (pixels == nullptr) { tce.set(nullptr); return; }
                    unsigned int w = rtb->PixelWidth, h = rtb->PixelHeight;
                    if (w == 0 || h == 0) { tce.set(nullptr); return; }
                    auto sb = SoftwareBitmap::CreateCopyFromBuffer(pixels,
                        BitmapPixelFormat::Bgra8, w, h, BitmapAlphaMode::Premultiplied);
                    tce.set(sb);
                } catch(...) { tce.set(nullptr); }
            });
        } catch(...) { tce.set(nullptr); }
    });

    try {
        CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
            CoreDispatcherPriority::Normal, dispatched);
    } catch(...) { tce.set(nullptr); }
    return concurrency::create_task(tce);
}

// ── AppPage::ApplyBlur ────────────────────────────────────────────────────────

concurrency::task<IRandomAccessStream^> AppPage::ApplyBlur(MoonlightApp^ app, float blurDip, float padDip) {
    if (app == nullptr) return concurrency::task_from_result<IRandomAccessStream^>(nullptr);

    Platform::String^ path = nullptr;
    try { path = app->ImagePath; } catch(...) {}

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

        // Capture the image element for masking
        FrameworkElement^ imageFe = dynamic_cast<FrameworkElement^>(this->FindName("AppImageRect"));
        if (imageFe == nullptr && this->AppsGrid != nullptr) {
            auto container = dynamic_cast<ListViewItem^>(this->AppsGrid->ContainerFromItem(app));
            if (container != nullptr) {
                auto found = FindChildByName(container, ref new Platform::String(L"AppImageRect"));
                if (found == nullptr) found = FindChildByName(container, ref new Platform::String(L"AppImageBlurRect"));
                imageFe = dynamic_cast<FrameworkElement^>(found);
            }
        }

        concurrency::task<SoftwareBitmap^> imageCaptureTask = imageFe != nullptr
            ? CaptureXamlElementAsync(imageFe)
            : concurrency::task_from_result<SoftwareBitmap^>(nullptr);

        // Capture the mask element
        SoftwareBitmap^ nullMask = nullptr;
        FrameworkElement^ maskFe = dynamic_cast<FrameworkElement^>(this->FindName("AppImageBlurRect"));
        if (maskFe == nullptr && this->AppsGrid != nullptr) {
            auto container = dynamic_cast<ListViewItem^>(this->AppsGrid->ContainerFromItem(app));
            if (container != nullptr) {
                auto found = FindChildByName(container, ref new Platform::String(L"AppImageBlurRect"));
                maskFe = dynamic_cast<FrameworkElement^>(found);
            }
        }
        concurrency::task<SoftwareBitmap^> maskCaptureTask = maskFe != nullptr
            ? CaptureXamlElementAsync(maskFe)
            : concurrency::task_from_result<SoftwareBitmap^>(nullMask);

        return imageCaptureTask.then([this, app, softwareBitmap, maskCaptureTask,
                                      ui_targetW, ui_targetH, ui_dpi, blurDip, padDip]
            (SoftwareBitmap^ capturedImage) mutable -> concurrency::task<IRandomAccessStream^>
        {
            return maskCaptureTask.then([this, app, softwareBitmap,
                                         ui_targetW, ui_targetH, ui_dpi, blurDip, padDip, capturedImage]
                (SoftwareBitmap^ maskFromXaml) mutable -> concurrency::task<IRandomAccessStream^>
            {
                unsigned int targetW = ui_targetW != 0 ? ui_targetW : softwareBitmap->PixelWidth;
                unsigned int targetH = ui_targetH != 0 ? ui_targetH : softwareBitmap->PixelHeight;
                unsigned int cornerRadiusPx = 0;
                try { cornerRadiusPx = (unsigned int)std::round(8.0 * ui_dpi / 96.0); } catch(...) { cornerRadiusPx = 16; }

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
                    softwareBitmap, maskFromXaml, glowTargetW, glowTargetH, ui_dpi, blurDip);
            });
        });
    });
}

// ── AppPage::BlurAppImage ─────────────────────────────────────────────────────

void AppPage::BlurAppImage(MoonlightApp^ selApp) {
    try {
        if (selApp->BlurredImage != nullptr) return;
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
                        try { SaveBlurStreamSync(stream, cachePath); } catch(...) {}
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
                    if (stream == nullptr) {
                        Utils::Logf("[AppPage] BlurAppImage: background stream null for app id=%d\n", selApp->Id);
                        return;
                    }
                    auto that = weakThis.Resolve<AppPage>();
                    if (that == nullptr) return;

                    that->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                        ref new DispatchedHandler([selApp, stream, weakThis]() {
                        try {
                            auto thatInner = weakThis.Resolve<AppPage>();
                            if (thatInner == nullptr || selApp == nullptr) return;
                            auto img = ref new BitmapImage();
                            create_task(img->SetSourceAsync(stream))
                                .then([weakThis, selApp, img, stream]() {
                                try {
                                    Utils::Logf("[AppPage] BlurAppImage: background SetSourceAsync completed for app id=%d\n", selApp->Id);
                                    auto thatCont = weakThis.Resolve<AppPage>();
                                    if (thatCont == nullptr) return;

                                    BitmapImage^ imgCapture = img;
                                    thatCont->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                                        ref new DispatchedHandler([thatCont, selApp, imgCapture]() {
                                        try {
                                            selApp->BlurredImage = imgCapture;
                                            Utils::Logf("[AppPage] BlurAppImage: assigned BlurredImage for app id=%d\n", selApp->Id);
                                            try { thatCont->FadeInBlurIfSelected(selApp, imgCapture); } catch(...) {}
                                        } catch(...) {}
                                    }));
                                } catch(...) {}
                            }, concurrency::task_continuation_context::use_arbitrary());
                        } catch(...) {}
                    }));
                } catch(...) {}
            }, concurrency::task_continuation_context::use_current());
        } catch(...) {}

        // Glow blur — only in list mode, used for the per-item glow rect
        if (!isGrid) {
            auto glowCachePath = BlurCachePath(selApp->ImagePath, true);
            try {
                getOrComputeStream(glowBlurAmount, kBlurGlowPaddingDip, glowCachePath)
                    .then([selApp, weakThis](IRandomAccessStream^ stream) {
                    try {
                        if (stream == nullptr) return;
                        auto that = weakThis.Resolve<AppPage>();
                        if (that == nullptr) return;

                        that->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                            ref new DispatchedHandler([selApp, stream, weakThis]() {
                            try {
                                auto thatInner = weakThis.Resolve<AppPage>();
                                if (thatInner == nullptr || selApp == nullptr) return;
                                auto img = ref new BitmapImage();
                                create_task(img->SetSourceAsync(stream))
                                    .then([selApp, img]() {
                                    try {
                                        Utils::Logf("[AppPage] BlurAppImage: assigned GlowImage for app id=%d\n", selApp->Id);
                                        selApp->GlowImage = img;
                                    } catch(...) {}
                                }, concurrency::task_continuation_context::use_current());
                            } catch(...) {}
                        }));
                    } catch(...) {}
                }, concurrency::task_continuation_context::use_current());
            } catch(...) {}
        }
    } catch(...) {}
}

// ── AppPage::FadeInBlurIfSelected ────────────────────────────────────────────

void AppPage::FadeInBlurIfSelected(MoonlightApp^ app, BitmapImage^ img) {
    try {
        if (app == nullptr || img == nullptr) return;
        try {
            if (this->AppsGrid == nullptr) return;
            auto selApp = dynamic_cast<MoonlightApp^>(this->AppsGrid->SelectedItem);
            if (selApp == nullptr || selApp != app) return;
        } catch(...) { return; }

        try {
            auto vm = this->ViewModel;
            if (vm != nullptr) {
                vm->TransitionToBlurredImage(img);
            }
        } catch(...) {}
    } catch(...) {}
}

} // namespace moonlight_xbox_dx
