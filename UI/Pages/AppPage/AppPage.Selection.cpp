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

void AppPage::UpdateItemHeights() {
    try {
        if (this->AppsGrid == nullptr) return;
        double listTarget = this->AppsGrid->ActualHeight * 0.85;
        bool isGrid = m_isGridLayout;
        double perItemWidth = 0.0;

        if (isGrid) {
            try {
                auto panel = dynamic_cast<ItemsWrapGrid^>(this->AppsGrid->ItemsPanelRoot);
                if (panel != nullptr) perItemWidth = panel->ItemWidth;
            } catch(...) {}
        }

        struct ItemSize { FrameworkElement^ fe; double desiredH; };
        std::vector<ItemSize> items;

        for (unsigned int i = 0; i < this->AppsGrid->Items->Size; ++i) {
            auto container = dynamic_cast<ListViewItem^>(this->AppsGrid->ContainerFromIndex(i));
            if (container == nullptr) continue;

            double containerHeight = container->ActualHeight;
            double availableH = containerHeight - 50.0;
            if (availableH <= 0.0) availableH = listTarget;

            double containerWidth = container->ActualWidth;
            double availableW = containerWidth > 0.0 ? containerWidth : this->AppsGrid->ActualWidth;
            if (isGrid && perItemWidth > 0.0) availableW = perItemWidth;

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

            double desiredH = listTarget;
            try {
                constexpr double ratio = 0.65;
                if (availableW > 0.0 && ratio > 0.0) {
                    double h = availableW / ratio;
                    if (h > listTarget) h = listTarget;
                    if (h > 0.0) desiredH = h;
                }
            } catch(...) {}
            if (desiredH < 0.0) desiredH = 0.0;
            items.push_back({ fe, desiredH });
        }

        for (auto& it : items) {
            if (it.fe == nullptr) continue;
            double prevH = it.fe->Height;
            if (std::isnan(prevH) || std::fabs(prevH - it.desiredH) > 1.0) {
                it.fe->Height = it.desiredH;
                it.fe->InvalidateMeasure();
                it.fe->UpdateLayout();
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

    // Update SelectedApp text overlay
    try {
        auto selApp = dynamic_cast<MoonlightApp^>(lv->SelectedItem);
        auto res = this->Resources;
        if (selApp != nullptr && this->SelectedAppText != nullptr && this->SelectedAppBox != nullptr) {
            try {
                this->SelectedAppText->Text = selApp->Name != nullptr ? selApp->Name : ref new Platform::String(L"");
            } catch(...) { this->SelectedAppText->Text = selApp->Name; }
            this->SelectedAppBox->Visibility = Windows::UI::Xaml::Visibility::Visible;
            this->SelectedAppText->Visibility = Windows::UI::Xaml::Visibility::Visible;
            this->SelectedAppText->Foreground  = ref new SolidColorBrush(Windows::UI::Colors::White);
            if (res != nullptr) {
                auto sb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(
                    res->Lookup(ref new Platform::String(L"ShowSelectedAppStoryboard")));
                if (sb != nullptr) sb->Begin();
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

        // Background blur — always generated, used for page background
        try {
            ApplyBlur(selApp, kBlurAmountBackground)
                .then([selApp, weakThis](IRandomAccessStream^ stream) {
                try {
                    if (stream == nullptr) {
                        Utils::Logf("[AppPage] BlurAppImage: ApplyBlur (background) returned null stream for app id=%d\n", selApp->Id);
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
            try {
                ApplyBlur(selApp, glowBlurAmount, kBlurGlowPaddingDip)
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
