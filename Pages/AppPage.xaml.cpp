#include "pch.h"
#include "AppPage.Xaml.h"
#include "Common\ModalDialog.xaml.h"
#include "HostSettingsPage.xaml.h"
#include "State\MoonlightClient.h"
#include "StreamPage.xaml.h"
#include "Utils.hpp"
#include "Common\XamlHelper.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl.h>
#include <ppltasks.h>
#include <robuffer.h>
#include <experimental/coroutine>
#include <windows.storage.streams.h>
#include <windows.graphics.directx.direct3d11.h>
#include <windows.ui.xaml.media.imaging.h>
#include <windows.ui.core.h>
#include <windows.ui.xaml.input.h>
#include <windows.ui.xaml.media.h>
#include <windows.ui.xaml.navigation.h>
#include <windows.ui.xaml.hosting.h>
#include <windows.ui.composition.h>
#include <windows.graphics.imaging.h>
#include <windows.ui.xaml.h>
#include "Common/DeviceResources.h"
#include "../Common/EffectsLibrary.h"
#include "Common/ImageHelpers.h"

using namespace Microsoft::WRL;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace concurrency;
using namespace Windows::UI::Xaml::Hosting;
using namespace Windows::UI::Composition;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Storage::Streams;
using namespace Windows::Storage;

namespace moonlight_xbox_dx {

// Helper to access buffer bytes for SoftwareBitmap
struct DECLSPEC_UUID("5B0D3235-4DBA-4D44-865E-8F1D0ED9F3E4") IMemoryBufferByteAccess : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(BYTE** value, UINT32* capacity) = 0;
};

// Use CPU fallback in EffectsLibrary for SoftwareBitmap blurs

// (Removed) XAML capture helper: UI-capture approach deferred — keep file-mask flow.

// Capture a XAML element (UI thread) into a BGRA8 Premultiplied SoftwareBitmap.
// Forward-declare visual-tree search helper so it can be used before its definition
static FrameworkElement^ FindChildByName(DependencyObject^ parent, Platform::String^ name);
static concurrency::task<Windows::Graphics::Imaging::SoftwareBitmap^> CaptureXamlElementAsync(Windows::UI::Xaml::FrameworkElement^ element) {
    concurrency::task_completion_event<Windows::Graphics::Imaging::SoftwareBitmap^> tce;
    if (element == nullptr) {
        tce.set(nullptr);
        return concurrency::create_task(tce);
    }

    auto dispatcher = Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher;
    auto dispatched = ref new Windows::UI::Core::DispatchedHandler([element, tce]() mutable {
        try {
            moonlight_xbox_dx::Utils::Log("CaptureXamlElementAsync: dispatched handler running\n");
            auto rtb = ref new Windows::UI::Xaml::Media::Imaging::RenderTargetBitmap();
            create_task(rtb->RenderAsync(element)).then([rtb]() {
                moonlight_xbox_dx::Utils::Log("CaptureXamlElementAsync: RenderAsync completed\n");
                return create_task(rtb->GetPixelsAsync());
            }).then([rtb, tce](Windows::Storage::Streams::IBuffer^ pixels) {
                if (pixels == nullptr) {
                    moonlight_xbox_dx::Utils::Log("CaptureXamlElementAsync: GetPixelsAsync returned null pixels\n");
                    tce.set(nullptr);
                    return;
                }
                try {
                    unsigned int w = rtb->PixelWidth;
                    unsigned int h = rtb->PixelHeight;
                    moonlight_xbox_dx::Utils::Logf("CaptureXamlElementAsync: pixels received width=%u height=%u\n", w, h);
                    if (w == 0 || h == 0) {
                        moonlight_xbox_dx::Utils::Log("CaptureXamlElementAsync: RenderTargetBitmap reported zero width/height\n");
                        tce.set(nullptr);
                        return;
                    }
                    auto sb = Windows::Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(pixels, Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8, w, h, Windows::Graphics::Imaging::BitmapAlphaMode::Premultiplied);
                    moonlight_xbox_dx::Utils::Log("CaptureXamlElementAsync: SoftwareBitmap created from pixels\n");
                    tce.set(sb);
                    return;
                } catch(...) {
                    moonlight_xbox_dx::Utils::Log("CaptureXamlElementAsync: exception creating SoftwareBitmap\n");
                    tce.set(nullptr);
                    return;
                }
            });
        } catch(...) {
            moonlight_xbox_dx::Utils::Log("CaptureXamlElementAsync: dispatched handler exception\n");
            tce.set(nullptr);
        }
    });

    try {
        dispatcher->RunAsync(CoreDispatcherPriority::Normal, dispatched);
    } catch(...) {
        // If dispatcher invocation fails, return null
        tce.set(nullptr);
    }

    return concurrency::create_task(tce);
}

concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> AppPage::ApplyBlur(MoonlightApp^ app) {
    if (app == nullptr) return concurrency::task_from_result<Windows::Storage::Streams::IRandomAccessStream^>(nullptr);

    Platform::String^ path = nullptr;
    try { path = app->ImagePath; } catch(...) { path = nullptr; }

    return concurrency::create_task(ImageHelpers::LoadSoftwareBitmapFromUriOrPathAsync(path)).then([this, app](Windows::Graphics::Imaging::SoftwareBitmap^ softwareBitmap) -> concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> {
        if (softwareBitmap == nullptr) return concurrency::task_from_result<Windows::Storage::Streams::IRandomAccessStream^>(nullptr);

        // Attempt to capture optional XAML mask element asynchronously
        SoftwareBitmap^ nullMask = nullptr;
        Windows::UI::Xaml::FrameworkElement^ fe = nullptr;
        // Try page namescope first
        try { fe = dynamic_cast<Windows::UI::Xaml::FrameworkElement^>(this->FindName("AppImageBlurRect")); } catch(...) { fe = nullptr; }
        // If not found, try the realized container for this app (useful if element is inside an ItemTemplate)
        if (fe == nullptr) {
            try {
                if (this->AppsGrid != nullptr) {
                    auto container = dynamic_cast<ListViewItem^>(this->AppsGrid->ContainerFromItem(app));
                    if (container != nullptr) {
                        try { auto found = FindChildByName(container, ref new Platform::String(L"AppImageBlurRect")); fe = dynamic_cast<Windows::UI::Xaml::FrameworkElement^>(found); } catch(...) { fe = nullptr; }
                    }
                }
            } catch(...) { fe = nullptr; }
        }
        concurrency::task<Windows::Graphics::Imaging::SoftwareBitmap^> captureTask = fe != nullptr ? CaptureXamlElementAsync(fe) : concurrency::task_from_result<Windows::Graphics::Imaging::SoftwareBitmap^>(nullMask);

        return captureTask.then([this, softwareBitmap](Windows::Graphics::Imaging::SoftwareBitmap^ maskFromXaml) mutable -> concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> {
            try {
                if (maskFromXaml == nullptr) {
                    moonlight_xbox_dx::Utils::Log("ApplyBlur: no XAML mask captured (maskFromXaml == nullptr)\n");
                } else {
                    try {
                        unsigned int mw = maskFromXaml->PixelWidth;
                        unsigned int mh = maskFromXaml->PixelHeight;
                        moonlight_xbox_dx::Utils::Logf("ApplyBlur: XAML mask captured size=%u x %u\n", mw, mh);
                    } catch(...) {
                        moonlight_xbox_dx::Utils::Log("ApplyBlur: XAML mask captured but failed to read dimensions\n");
                    }
                }
            } catch(...) {}
            // Composite mask if present
            try {
                if (maskFromXaml != nullptr) {
                    try { softwareBitmap = ImageHelpers::CompositeWithMask(softwareBitmap, maskFromXaml, 8); } catch(...) { }
                }
            } catch(...) {}

            // Try GPU blur first
            try {
                auto gpuResult = ::EffectsLibrary::GpuBoxBlurSoftwareBitmap(softwareBitmap, 6);
                if (gpuResult != nullptr) {
                    return concurrency::create_task(ImageHelpers::EncodeSoftwareBitmapToPngStreamAsync(gpuResult)).then([](Windows::Storage::Streams::IRandomAccessStream^ newStream) -> Windows::Storage::Streams::IRandomAccessStream^ {
                        if (newStream != nullptr) {
                            try { newStream->Seek(0); } catch(...) {}
                        }
                        return newStream;
                    });
                }
            } catch(...) {}

            // CPU fallback
            try { ::EffectsLibrary::BoxBlurSoftwareBitmap(softwareBitmap, 6); } catch(...) {}
            return concurrency::create_task(ImageHelpers::EncodeSoftwareBitmapToPngStreamAsync(softwareBitmap)).then([](Windows::Storage::Streams::IRandomAccessStream^ newStream) -> Windows::Storage::Streams::IRandomAccessStream^ {
                if (newStream != nullptr) {
                    try { newStream->Seek(0); } catch(...) {}
                }
                return newStream;
            });
        });
    });
}

static bool allowScaleTransitions = true;
static bool allowOpacityTransitions = true;

static constexpr float kDesaturatorOpacityUnselected = 0.7f;
static constexpr float kSelectedScale = 1.0f;
static constexpr float kUnselectedScale = 0.8f;
static constexpr int kAnimationDurationMs = 150;
// Fraction of the AppsGrid height used to size the app image area. Change this to adjust layout globally.
static constexpr double kAppsGridHeightFactor = 0.75;

// Forward declare helper used below
static ScrollViewer^ FindScrollViewer(DependencyObject^ parent);
static FrameworkElement^ FindChildByName(DependencyObject^ parent, Platform::String^ name);
// Forward declare animation helpers used by ApplySelectionVisuals
static void AnimateElementOpacity(UIElement^ element, float targetOpacity, int durationMs);
static void SetElementOpacityImmediate(UIElement^ element, float value);
static void SetElementScaleImmediate(UIElement^ element, float scale);
static void AnimateElementScale(UIElement^ element, float targetScale, int durationMs);

void AppPage::CenterSelectedItem(int attempts, bool immediate) {
    auto lv = this->AppsGrid;
    if (lv == nullptr) return;
    if (lv->SelectedIndex < 0) return;

    // If we're in grid layout, perform *vertical* centering only (Y axis).
    if (m_isGridLayout) {
        Utils::Logf("CenterSelectedItem: m_isGridLayout=true, performing vertical centering attempts=%d immediate=%d\n", attempts, immediate ? 1 : 0);
        auto item = lv->SelectedItem;
        if (item == nullptr) return;
        try {
            // Ensure item is realized so we can obtain its container
            try { lv->ScrollIntoView(item); } catch(...) {}
            auto container = dynamic_cast<ListViewItem^>(lv->ContainerFromItem(item));
            auto sv = m_scrollViewer == nullptr ? FindScrollViewer(lv) : m_scrollViewer;
            if (container != nullptr && sv != nullptr) {
                int retries = attempts > 0 ? attempts : 1;
                TryVerticalCentering(sv, container, retries, this);
            } else {
                Utils::Log("CenterSelectedItem: vertical centering - container or ScrollViewer null\n");
            }
        } catch(...) {}
        return;
    }

    // Diagnostic: log selected index and item id/name if available
    try {
        int idx = lv->SelectedIndex;
        auto sel = lv->SelectedItem;
        if (sel != nullptr) {
            auto app = dynamic_cast<::moonlight_xbox_dx::MoonlightApp^>(sel);
            if (app != nullptr) {
                Utils::Logf("CenterSelectedItem: centering index=%d attempts=%d immediate=%d id=%d name=%S\n", idx, attempts, immediate ? 1 : 0, app->Id, app->Name->Data());
            } else {
                Utils::Logf("CenterSelectedItem: centering index=%d attempts=%d immediate=%d (selected item not MoonlightApp)\n", idx, attempts, immediate ? 1 : 0);
            }
        } else {
            Utils::Logf("CenterSelectedItem: centering index=%d attempts=%d immediate=%d (selected item null)\n", idx, attempts, immediate ? 1 : 0);
        }
    } catch(...) {}

    // Simplified centering: prefer built-in ScrollIntoView with Center alignment.
    auto item = lv->SelectedItem;
    if (item == nullptr) return;

    try {
        // Attempt to center the selected item horizontally

        // Try to compute the desired centered X based on the realized container and use ChangeView.
        auto container = dynamic_cast<ListViewItem^>(lv->ContainerFromItem(item));
        auto sv = m_scrollViewer == nullptr ? FindScrollViewer(lv) : m_scrollViewer;

        if (container != nullptr && sv != nullptr) {
            try {
                Windows::Foundation::Point pt{0,0};
                try {
                    auto trans = container->TransformToVisual(sv);
                    if (trans != nullptr) pt = trans->TransformPoint(Windows::Foundation::Point{ 0, 0 });
                    else Utils::Log("CenterSelectedItem: TransformToVisual returned null\n");
                } catch(...) { Utils::Log("CenterSelectedItem: TransformToVisual/TransformPoint threw\n"); }
                double containerCenter = pt.X + container->ActualWidth / 2.0;
                double desired = containerCenter - (sv->ViewportWidth / 2.0);
                if (desired < 0) desired = 0;
                if (desired > sv->ScrollableWidth) desired = sv->ScrollableWidth;
                // computed desired centered offset

                double extentW = -1.0;
                try { extentW = sv->ExtentWidth; } catch(...) { extentW = -1.0; }
                Utils::Logf("CenterSelectedItem: sv ExtentWidth=%.2f ViewportWidth=%.2f ScrollableWidth=%.2f HorizontalOffset=%.2f VerticalOffset=%.2f\n", extentW, sv->ViewportWidth, sv->ScrollableWidth, sv->HorizontalOffset, sv->VerticalOffset);
                Utils::Logf("CenterSelectedItem: container ActualWidth=%.2f pt.X=%.2f containerCenter=%.2f\n", container->ActualWidth, pt.X, containerCenter);
                Utils::Logf("CenterSelectedItem: desired=%.2f (clamped to [0, %.2f])\n", desired, sv->ScrollableWidth);

                // Additional diagnostics: items count, items panel type and measured widths
                int itemsCount = -1;
                try { itemsCount = (int)lv->Items->Size; } catch(...) { itemsCount = -1; }
                double itemsPanelActualW = -1.0;
                double itemsPanelRootActualW = -1.0;
                try { if (this->m_itemsPanel != nullptr) itemsPanelActualW = this->m_itemsPanel->ActualWidth; } catch(...) { itemsPanelActualW = -2.0; }
                try { if (lv->ItemsPanelRoot != nullptr) itemsPanelRootActualW = lv->ItemsPanelRoot->ActualWidth; } catch(...) { itemsPanelRootActualW = -2.0; }
                bool isStack = false; bool isWrap = false;
                try { isStack = (dynamic_cast<ItemsStackPanel^>(this->m_itemsPanel) != nullptr); } catch(...) { isStack = false; }
                try { isWrap = (dynamic_cast<ItemsWrapGrid^>(this->m_itemsPanel) != nullptr); } catch(...) { isWrap = false; }
                if (isStack) Utils::Log("CenterSelectedItem: ItemsPanel type = ItemsStackPanel\n");
                else if (isWrap) Utils::Log("CenterSelectedItem: ItemsPanel type = ItemsWrapGrid\n");
                else Utils::Log("CenterSelectedItem: ItemsPanel type = Other or null\n");
                double estimatedContentWidth = -1.0;
                try { estimatedContentWidth = container->ActualWidth * (double)itemsCount; } catch(...) { estimatedContentWidth = -1.0; }
                Utils::Logf("CenterSelectedItem: ItemsCount=%d itemsPanelActualW=%.2f itemsPanelRootActualW=%.2f estimatedContentWidth=%.2f containerWidth=%.2f\n", itemsCount, itemsPanelActualW, itemsPanelRootActualW, estimatedContentWidth, container->ActualWidth);

                if (sv->ScrollableWidth > 1.0) {
                    // perform ChangeView to center
                    try { sv->ChangeView(desired, nullptr, nullptr, immediate); } catch(...) { }
                    Utils::Log("CenterSelectedItem: used ChangeView to desired offset\n");
                    try { if (!this->m_suppressSelectionFocus) container->Focus(Windows::UI::Xaml::FocusState::Programmatic); } catch(...) { }
                    // mark that initial centering has been scheduled if this was requested
                    if (immediate) this->m_initialCenteringScheduled = true;
                    return;
                }

                // If ScrollableWidth is not available (<= 1.0), attempt immediate transform-based centering
                // as a reliable fallback so the selected item appears centered even when the ScrollViewer
                // has not yet computed scroll extents.
                // If ScrollableWidth is not available, apply the transform fallback immediately
                this->ApplyCenteringTransformIfNeeded(container);

                // One-shot LayoutUpdated fallback: if layout updates later produce a valid ScrollableWidth,
                // center the item then and remove the handler. This is more responsive than waiting on
                // the timer polling loop in many scenarios where layout finishes quickly.
                try {
                    auto weakThisLU = WeakReference(this);
                    // Use a heap-allocated token so the lambda can safely capture it by value
                    auto luTokenPtr = std::make_shared<Windows::Foundation::EventRegistrationToken>();
                    EventHandler<Object^>^ layoutHandler = nullptr;
                    layoutHandler = ref new EventHandler<Object^>([weakThisLU, sv, container, immediate, luTokenPtr](Object^, Object^) {
                        try {
                            auto that = weakThisLU.Resolve<AppPage>();
                            if (that == nullptr) return;
                            double swLU = 0.0;
                            try { swLU = sv->ScrollableWidth; } catch(...) { swLU = 0.0; }
                            if (swLU > 1.0) {
                                try {
                                    Windows::Foundation::Point ptLU{0,0};
                                    try {
                                        auto transLU = container->TransformToVisual(sv);
                                        if (transLU != nullptr) ptLU = transLU->TransformPoint(Windows::Foundation::Point{ 0, 0 });
                                        else Utils::Log("CenterSelectedItem (LayoutUpdated): TransformToVisual returned null\n");
                                    } catch(...) { Utils::Log("CenterSelectedItem (LayoutUpdated): TransformToVisual/TransformPoint threw\n"); }
                                    double containerCenterLU = ptLU.X + container->ActualWidth / 2.0;
                                    double desiredLU = containerCenterLU - (sv->ViewportWidth / 2.0);
                                    if (desiredLU < 0) desiredLU = 0;
                                    if (desiredLU > sv->ScrollableWidth) desiredLU = sv->ScrollableWidth;
                                    try { sv->ChangeView(desiredLU, nullptr, nullptr, false); Utils::Logf("CenterSelectedItem: LayoutUpdated applied ChangeView desired=%.2f\n", desiredLU); } catch(...) {}
                                    try { if (!that->m_suppressSelectionFocus) container->Focus(Windows::UI::Xaml::FocusState::Programmatic); } catch(...) {}
                                } catch(...) {}
                                try { sv->LayoutUpdated -= *luTokenPtr; } catch(...) {}
                            }
                        } catch(...) {}
                    });
                    try { *luTokenPtr = sv->LayoutUpdated += layoutHandler; } catch(...) {}
                } catch(...) {}

                // If we reach here, ScrollableWidth is not yet available. Start a short-lived DispatcherTimer to poll
                // and attempt ChangeView when the ScrollableWidth becomes valid. This avoids relying solely on composition
                // transform fallback when real scrolling becomes available shortly after layout.
                try {
                    auto weakThis = WeakReference(this);
                    auto itemRef = item;
                    auto svRef = sv;
                    auto containerRef = container;
                    auto lvRef = lv;
                    try {
                        auto that = weakThis.Resolve<AppPage>();
                        if (that == nullptr) { return; }
                        double sw = 0.0;
                        try { sw = svRef->ScrollableWidth; } catch(...) { sw = 0.0; }
                        Utils::Logf("CenterSelectedItem: polling sv.ScrollableWidth=%.2f\n", sw);
                        try {
                            double itemsPanelActualW = -1.0;
                            double itemsPanelRootActualW = -1.0;
                            bool hasItemsPanel = false;
                            try { hasItemsPanel = (that->m_itemsPanel != nullptr); } catch(...) { hasItemsPanel = false; }
                            try { if (that->m_itemsPanel != nullptr) itemsPanelActualW = that->m_itemsPanel->ActualWidth; } catch(...) { itemsPanelActualW = -2.0; }
                            try { if (lvRef != nullptr && lvRef->ItemsPanelRoot != nullptr) itemsPanelRootActualW = lvRef->ItemsPanelRoot->ActualWidth; } catch(...) { itemsPanelRootActualW = -2.0; }
                            try {
                                if (that->m_itemsPanel != nullptr) {
                                    auto vis = ElementCompositionPreview::GetElementVisual(that->m_itemsPanel);
                                    (void)vis;
                                    auto tt = dynamic_cast<Windows::UI::Xaml::Media::TranslateTransform^>(that->m_itemsPanel->RenderTransform);
                                    (void)tt;
                                }
                            } catch(...) {}
                            // compute an estimated content width using realized container width and total items
                            int totalItems = -1;
                            try { totalItems = (int)lvRef->Items->Size; } catch(...) { totalItems = -1; }
                            double estContent = -1.0;
                            try { estContent = containerRef->ActualWidth * (double)totalItems; } catch(...) { estContent = -1.0; }
                            Utils::Logf("CenterSelectedItem: polling sv.ScrollableWidth=%.2f itemsPanelActualW=%.2f itemsPanelRootActualW=%.2f estimatedContentWidth=%.2f totalItems=%d\n", sw, itemsPanelActualW, itemsPanelRootActualW, estContent, totalItems);
                        } catch(...) {}
                        if (sw > 1.0) {
                            // recompute desired in case sizes changed
                            try {
                                Windows::Foundation::Point pt2{0,0};
                                try {
                                    auto trans2 = containerRef->TransformToVisual(svRef);
                                    if (trans2 != nullptr) pt2 = trans2->TransformPoint(Windows::Foundation::Point{ 0, 0 });
                                    else Utils::Log("CenterSelectedItem (polling): TransformToVisual returned null\n");
                                } catch(...) { Utils::Log("CenterSelectedItem (polling): TransformToVisual/TransformPoint threw\n"); }
                                double containerCenter2 = pt2.X + containerRef->ActualWidth / 2.0;
                                double desired2 = containerCenter2 - (svRef->ViewportWidth / 2.0);
                                if (desired2 < 0) desired2 = 0;
                                if (desired2 > svRef->ScrollableWidth) desired2 = svRef->ScrollableWidth;
                                // apply computed ChangeView when available
                                try { svRef->ChangeView(desired2, nullptr, nullptr, false); Utils::Logf("CenterSelectedItem: polling applied ChangeView desired2=%.2f\n", desired2); } catch(...) { }
                                try { if (!that->m_suppressSelectionFocus) containerRef->Focus(Windows::UI::Xaml::FocusState::Programmatic); } catch(...) {}
                            } catch(...) { }
                        }
                    } catch(...) {}
                } catch(...) { }
            } catch(...) { }
        } else {
            // container or sv is null; nothing to do
        }

        // If container is not yet realized, retry shortly up to `attempts` times.
        if (attempts > 0) {
            // schedule a retry on the UI thread without blocking it
            // If caller asked for immediate centering, ask the ListView to realize the item now to speed centering
            try { if (immediate) lv->ScrollIntoView(item); } catch(...) {}
            auto weakThis = WeakReference(this);
            auto itemRef = item;
            this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([weakThis, itemRef, attempts, immediate]() {
                try {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that == nullptr) return;
                    that->CenterSelectedItem(attempts - 1, immediate);
                } catch(...) {}
            }));
            return;
        }

        // Fallback: ask the ListView to bring the item into view if we couldn't center programmatically.
        try { lv->ScrollIntoView(item); } catch(...) {}
    } catch (...) {}
}

// Recursive find child by name
static FrameworkElement^ FindChildByName(DependencyObject^ parent, Platform::String^ name) {
    if (parent == nullptr) return nullptr;
    int count = VisualTreeHelper::GetChildrenCount(parent);
    for (int i = 0; i < count; ++i) {
        auto child = VisualTreeHelper::GetChild(parent, i);
        auto fe = dynamic_cast<FrameworkElement^>(child);
        if (fe != nullptr && fe->Name == name) return fe;
        auto rec = FindChildByName(child, name);
        if (rec != nullptr) return rec;
    }
    return nullptr;
}

// Helper: find multiple named children (Desaturator, AppImageRect, AppName) inside a container
static void FindElementChildren(DependencyObject^ container, UIElement^& outDesaturator, UIElement^& outImage, UIElement^& outName) {
    outDesaturator = nullptr; outImage = nullptr; outName = nullptr;
    if (container == nullptr) return;
    try {
        outDesaturator = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"Desaturator")));
        outImage = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppImageRect")));
        outName = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppName")));
    } catch(...) { outDesaturator = nullptr; outImage = nullptr; outName = nullptr; }
}

// Helper: apply visuals for selected/unselected state to a realized container
static void ApplySelectionVisuals(UIElement^ des, UIElement^ img, UIElement^ nameTxt, bool selected) {
    try {
        // Idempotency: if the container has a Tag storing the last-applied selected state,
        // avoid reapplying visuals unnecessarily to reduce flicker.
        try {
            ListViewItem^ container = nullptr;
            try {
                // Walk up the visual tree from nameTxt to find the enclosing ListViewItem
                DependencyObject^ current = dynamic_cast<DependencyObject^>(nameTxt);
                while (current != nullptr && container == nullptr) {
                    container = dynamic_cast<ListViewItem^>(current);
                    if (container != nullptr) break;
                    try { current = VisualTreeHelper::GetParent(current); } catch(...) { current = nullptr; }
                }
            } catch(...) { container = nullptr; }

            if (container != nullptr) {
                bool prev = false; bool hasPrev = false;
                try {
                    auto tag = container->Tag;
                    if (tag != nullptr) {
                        try { prev = safe_cast<Platform::IBox<bool>^>(tag)->Value; hasPrev = true; } catch(...) { hasPrev = false; }
                    }
                } catch(...) { hasPrev = false; }
                if (hasPrev && prev == selected) {
                    // already in desired state, skip
                    return;
                }
                try { container->Tag = safe_cast<Platform::Object^>(ref new Platform::Box<bool>(selected)); } catch(...) {}
            }
        } catch(...) {}
        if (img != nullptr) {
            if (allowScaleTransitions) AnimateElementScale(img, selected ? kSelectedScale : kUnselectedScale, kAnimationDurationMs);
            else SetElementScaleImmediate(img, selected ? kSelectedScale : kUnselectedScale);
        }
        if (des != nullptr) {
            if (allowScaleTransitions) AnimateElementScale(des, selected ? kSelectedScale : kUnselectedScale, kAnimationDurationMs);
            else SetElementScaleImmediate(des, selected ? kSelectedScale : kUnselectedScale);

            if (allowOpacityTransitions) AnimateElementOpacity(des, selected ? 0.0f : kDesaturatorOpacityUnselected, kAnimationDurationMs);
            else SetElementOpacityImmediate(des, selected ? 0.0f : kDesaturatorOpacityUnselected);
        }
        if (nameTxt != nullptr) {
            if (allowOpacityTransitions) AnimateElementOpacity(nameTxt, selected ? 1.0f : 0.0f, kAnimationDurationMs);
            else SetElementOpacityImmediate(nameTxt, selected ? 1.0f : 0.0f);
        }
    } catch(...) {}
}

// Apply visuals to a ListViewItem container without modifying ListView selection.
void AppPage::ApplyVisualsToContainer(ListViewItem^ container, bool selected) {
    if (container == nullptr) return;
    try {
        UIElement^ des = nullptr; UIElement^ img = nullptr; UIElement^ nameTxt = nullptr;
        FindElementChildren(container, des, img, nameTxt);
        ApplySelectionVisuals(des, img, nameTxt, selected);
    } catch(...) {}
}

// Animate a UIElement's opacity using the Composition Visual for smooth fades
static void AnimateElementOpacity(UIElement^ element, float targetOpacity, int durationMs = kAnimationDurationMs) {
    if (element == nullptr) return;
    try {
        // Try using Composition visual animation first for smoothness
        bool compositionAttempt = false;
        try {
                auto vis = ElementCompositionPreview::GetElementVisual(element);
                if (vis != nullptr) {
                    compositionAttempt = true;
                    auto compositor = vis->Compositor;
                    auto anim = compositor->CreateScalarKeyFrameAnimation();
                    TimeSpan ts; ts.Duration = (int64_t)durationMs * 10000LL;
                    anim->Duration = ts;
                    anim->InsertKeyFrame(1.0f, targetOpacity);
                    try { vis->StopAnimation("Opacity"); } catch (...) {}
                    vis->StartAnimation("Opacity", anim);
            }
        } catch (...) {
            try { Utils::Logf("AnimateElementOpacity: composition animation threw for element=%p\n", element); } catch(...) {}
        }

        // Also start a XAML Storyboard DoubleAnimation as a reliable fallback
        try {
            using namespace Windows::UI::Xaml::Media::Animation;
            auto dbl = ref new DoubleAnimation();
            dbl->To = ref new Platform::Box<double>((double)targetOpacity);
            TimeSpan ts2; ts2.Duration = (int64_t)durationMs * 10000LL;
            dbl->Duration = Windows::UI::Xaml::DurationHelper::FromTimeSpan(ts2);
            auto sb = ref new Storyboard();
            sb->Children->Append(dbl);
            Storyboard::SetTarget(dbl, element);
            Storyboard::SetTargetProperty(dbl, ref new Platform::String(L"(UIElement.Opacity)"));
            try { 
                sb->Begin(); 
                // try { Utils::Logf("AnimateElementOpacity: xaml storyboard begun for element=%p\n", element); } catch(...) {} 
            } catch (...) { 
                // try { Utils::Logf("AnimateElementOpacity: xaml storyboard begin failed for element=%p\n", element); } catch(...) {} 
            }
        } catch (const std::exception &e) {
            try { Utils::Logf("AnimateElementOpacity: xaml animation exception: %s\n", e.what()); } catch(...) {}
        } catch (...) {
            try { Utils::Logf("AnimateElementOpacity: xaml animation unknown exception for element=%p\n", element); } catch(...) {}
        }
    } catch (...) {}
}

// Set opacity immediately without animation
static void SetElementOpacityImmediate(UIElement^ element, float value) {
    if (element == nullptr) return;
    try {
            auto vis = ElementCompositionPreview::GetElementVisual(element);
            if (vis != nullptr) {
                try { vis->StopAnimation("Opacity"); } catch (...) {}
                vis->Opacity = value;
            }
            // also set XAML opacity to keep properties in sync
            element->Opacity = value;
    } catch (...) {}
}

// Set scale immediately without animation
static void SetElementScaleImmediate(UIElement^ element, float scale) {
    if (element == nullptr) return;
    try {
        auto vis = ElementCompositionPreview::GetElementVisual(element);
        if (vis != nullptr) {
            try { vis->StopAnimation("Scale.X"); vis->StopAnimation("Scale.Y"); } catch (...) {}
            Windows::Foundation::Numerics::float3 s; s.x = scale; s.y = scale; s.z = 0.0f;
            vis->Scale = s;
        }
    } catch (...) {}
}

// Animate a UIElement's uniform scale using the Composition Visual for smooth scaling
static void AnimateElementScale(UIElement^ element, float targetScale, int durationMs = kAnimationDurationMs) {
    if (element == nullptr) return;
    try {
        auto vis = ElementCompositionPreview::GetElementVisual(element);
        if (vis == nullptr) return;
        auto compositor = vis->Compositor;
        auto animX = compositor->CreateScalarKeyFrameAnimation();
        auto animY = compositor->CreateScalarKeyFrameAnimation();

        TimeSpan ts;
        ts.Duration = (int64_t)durationMs * 10000LL;

        animX->Duration = ts; animY->Duration = ts;
        animX->InsertKeyFrame(1.0f, targetScale);
        animY->InsertKeyFrame(1.0f, targetScale);

        try { vis->StopAnimation("Scale.X"); vis->StopAnimation("Scale.Y"); } catch (...) {}
        try { vis->StartAnimation("Scale.X", animX); vis->StartAnimation("Scale.Y", animY); } catch (...) {}
    } catch (...) {}
}

AppPage::AppPage() {
	InitializeComponent();
	Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->SetDesiredBoundsMode(Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow);

    this->Loaded += ref new Windows::UI::Xaml::RoutedEventHandler(this, &AppPage::OnLoaded);
    this->Unloaded += ref new Windows::UI::Xaml::RoutedEventHandler(this, &AppPage::OnUnloaded);
	
	this->SizeChanged += ref new Windows::UI::Xaml::SizeChangedEventHandler(this, &AppPage::PageRoot_SizeChanged);

	m_compositionReady = false;
    m_filteredApps = ref new Platform::Collections::Vector<MoonlightApp^>();
}

// Apply current search filter to Host->Apps and populate m_filteredApps
bool AppPage::ApplyAppFilter(Platform::String^ filter) {
    auto host = this->Host;
    auto vec = this->m_filteredApps;
    bool identical = false;

    if (vec == nullptr) return identical;

    if (host == nullptr || host->Apps == nullptr) {
        vec->Clear();
        return identical;
    }

    // Build new filtered results into a temporary vector so we can compare before updating the bound collection.
    auto newResults = ref new Platform::Collections::Vector<moonlight_xbox_dx::MoonlightApp^>();

    bool empty = filter == nullptr || filter->Length() == 0;
    std::wstring fw;
    if (!empty) {
        std::string fstr = Utils::PlatformStringToStdString(filter);
        fw = Utils::NarrowToWideString(fstr);
        // tolower
        std::transform(fw.begin(), fw.end(), fw.begin(), ::towlower);
    }

    for (unsigned int i = 0; i < host->Apps->Size; ++i) {
        auto app = host->Apps->GetAt(i);
        if (app == nullptr) continue;
        if (empty) {
            newResults->Append(app);
        } else {
            auto namePS = app->Name != nullptr ? app->Name : ref new Platform::String(L"");
            std::string nstr = Utils::PlatformStringToStdString(namePS);
            std::wstring namew = Utils::NarrowToWideString(nstr);
            std::transform(namew.begin(), namew.end(), namew.begin(), ::towlower);
            if (namew.find(fw) != std::wstring::npos) newResults->Append(app);
        }
    }

    // If the new results have the same IDs in the same order as the current filtered list, skip updating to avoid flashing.
    try {
        if (vec != nullptr && vec->Size == newResults->Size) {
            identical = true;
            for (unsigned int i = 0; i < vec->Size; ++i) {
                auto a = vec->GetAt(i);
                auto b = newResults->GetAt(i);
                int aid = a != nullptr ? a->Id : -1;
                int bid = b != nullptr ? b->Id : -1;
                if (aid != bid) { identical = false; break; }
            }
        }
    } catch(...) { identical = false; }

    if (identical) {
        // Nothing changed; skip UI updates
        return identical;
    }

    // Commit new results to the bound collection
    vec->Clear();
    for (unsigned int i = 0; i < newResults->Size; ++i) vec->Append(newResults->GetAt(i));

    // Toggle a centered placeholder message when no apps match the filter
    try {
        bool emptyResults = (vec->Size == 0);
        this->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([this, emptyResults]() {
            try {
                if (this->NoAppsMessage != nullptr) {
                    this->NoAppsMessage->Visibility = emptyResults ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
                }

                // If there are no results, animate out the shared SelectedAppBox/SelectedAppText
                try {
                    if (emptyResults && this->SelectedAppText != nullptr && this->SelectedAppBox != nullptr) {
                        auto res = this->Resources;
                        if (res != nullptr) {
                            auto sbObj = res->Lookup(ref new Platform::String(L"HideSelectedAppStoryboard"));
                            auto sb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(sbObj);
                            if (sb != nullptr) {
                                sb->Begin();
                            } else {
                                if (allowOpacityTransitions && m_compositionReady) {
                                    AnimateElementOpacity(this->SelectedAppBox, 0.0f, kAnimationDurationMs);
                                    AnimateElementOpacity(this->SelectedAppText, 0.0f, kAnimationDurationMs);
                                } else {
                                    SetElementOpacityImmediate(this->SelectedAppBox, 0.0f);
                                    SetElementOpacityImmediate(this->SelectedAppText, 0.0f);
                                }
                            }
                        }
                    }
                } catch(...) {}

            } catch(...) {}
        }));
    } catch(...) {}

    // If we have results, visually promote the first item and center it (no focus changes)
    try {
        if (vec->Size > 0 && this->AppsGrid != nullptr) {
            // Promote first item on the UI thread so visuals/layout are updated
            this->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([this]() {
                try {
                    if (this->AppsGrid == nullptr) return;
                    if (this->AppsGrid->Items != nullptr && this->AppsGrid->Items->Size > 0) {
                        try {
                            // Clear visuals on other realized containers so only the promoted item appears selected
                            try {
                                for (unsigned int i = 0; i < this->AppsGrid->Items->Size; ++i) {
                                    auto c = dynamic_cast<ListViewItem^>(this->AppsGrid->ContainerFromIndex(i));
                                    if (c != nullptr) ApplyVisualsToContainer(c, false);
                                }
                            } catch(...) {}

                            auto firstItem = this->AppsGrid->Items->GetAt(0);
                            if (firstItem != nullptr) {
                                // Ensure it's realized
                                this->AppsGrid->ScrollIntoView(firstItem);
                                auto container = dynamic_cast<ListViewItem^>(this->AppsGrid->ContainerFromItem(firstItem));
                                if (container != nullptr) {
                                    ApplyVisualsToContainer(container, true);

                                    // Update the shared SelectedAppText/SelectedAppBox to mirror selection visuals
                                    try {
                                        if (this->SelectedAppText != nullptr && this->SelectedAppBox != nullptr) {
                                            auto selApp = dynamic_cast<moonlight_xbox_dx::MoonlightApp^>(firstItem);
                                            if (selApp != nullptr) {
                                                this->SelectedAppText->Text = selApp->Name;
                                                this->SelectedAppBox->Visibility = Windows::UI::Xaml::Visibility::Visible;
                                                this->SelectedAppText->Visibility = Windows::UI::Xaml::Visibility::Visible;
                                                this->SelectedAppBox->Background = ref new SolidColorBrush(Windows::UI::Colors::Transparent);
                                                this->SelectedAppText->Foreground = ref new SolidColorBrush(Windows::UI::Colors::White);
                                                SetElementOpacityImmediate(this->SelectedAppBox, 0.0f);
                                                SetElementOpacityImmediate(this->SelectedAppText, 0.0f);
                                                auto res = this->Resources;
                                                if (res != nullptr) {
                                                    auto sbObj = res->Lookup(ref new Platform::String(L"ShowSelectedAppStoryboard"));
                                                    auto sb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(sbObj);
                                                    if (sb != nullptr) sb->Begin();
                                                }
                                            }
                                        }
                                    } catch(...) {}

                                    // Center the container in the horizontal view without changing selection
                                    try {
                                        if (!m_isGridLayout) {
                                            auto sv = m_scrollViewer == nullptr ? FindScrollViewer(this->AppsGrid) : m_scrollViewer;
                                            if (sv != nullptr) {
                                                Windows::Foundation::Point pt{0,0};
                                                try {
                                                    auto trans2 = container->TransformToVisual(sv);
                                                    if (trans2 != nullptr) pt = trans2->TransformPoint(Windows::Foundation::Point{ 0, 0 });
                                                    else Utils::Log("OnNavigatedTo center: TransformToVisual returned null\n");
                                                } catch(...) { Utils::Log("OnNavigatedTo center: TransformToVisual/TransformPoint threw\n"); }
                                                double containerCenter = pt.X + container->ActualWidth / 2.0;
                                                double desired = containerCenter - (sv->ViewportWidth / 2.0);
                                                if (desired < 0) desired = 0;
                                                if (desired > sv->ScrollableWidth) desired = sv->ScrollableWidth;
                                                if (sv->ScrollableWidth > 1.0) sv->ChangeView(desired, nullptr, nullptr, false);
                                            }
                                        }
                                    } catch(...) {}
                                }
                            }
                        } catch(...) {}
                    }
                } catch(...) {}
            }));
        }
    } catch(...) {}
	return identical;
}

void AppPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) {
	MoonlightHost^ mhost = dynamic_cast<MoonlightHost^>(e->Parameter);
	if (mhost == nullptr) {
		return;
	}
    host = mhost;
    host->UpdateHostInfo(true);
    host->UpdateApps();

    // Attach to host Apps collection changed so we reapply filter when apps arrive/refresh
    try {
        if (host->Apps != nullptr) {
            auto obs = dynamic_cast<Windows::Foundation::Collections::IObservableVector<MoonlightApp^>^>(host->Apps);
            if (obs != nullptr) {
                m_apps_changed_token = obs->VectorChanged += ref new Windows::Foundation::Collections::VectorChangedEventHandler<MoonlightApp^>(this, &AppPage::OnHostAppsChanged);
            }
        }
    } catch(...) {}

    // Populate filtered list initially (in case Apps were already present)
    ApplyAppFilter(nullptr);

    // Blur all app images (defensive: guard nulls and log aggressively)
    try {
        if (host == nullptr) {
            Utils::Log("OnNavigatedTo: host is null, skipping blur loop\n");
        } else if (host->Apps == nullptr) {
            Utils::Log("OnNavigatedTo: host->Apps is null, skipping blur loop\n");
        } else {
            for (unsigned int i = 0; i < host->Apps->Size; ++i) {
                auto app = host->Apps->GetAt(i);
                if (app == nullptr) {
                    Utils::Logf("OnNavigatedTo: app at index %u is null, skipping\n", i);
                    continue;
                }
                Platform::WeakReference weakThis(this);
                try {
                    ApplyBlur(app).then([app, weakThis](Windows::Storage::Streams::IRandomAccessStream^ stream) {
                        try {
                            if (stream == nullptr) {
                                Utils::Logf("[AppPage] ApplyBlur returned null stream for app id=%d\n", app->Id);
                                return;
                            }
                            auto that = weakThis.Resolve<AppPage>();
                            if (that == nullptr) return;
                            // Dispatch BitmapImage creation and SetSourceAsync to the page Dispatcher
                            try {
                                that->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([app, stream, weakThis]() {
                                    try {
                                        auto thatInner = weakThis.Resolve<AppPage>();
                                        if (thatInner == nullptr) return;
                                        if (app == nullptr) return;
                                        try { Utils::Logf("[AppPage] Dispatcher: creating BitmapImage for app id=%d\n", app->Id); } catch(...) {}
                                        auto img = ref new Windows::UI::Xaml::Media::Imaging::BitmapImage();
                                        try { Utils::Logf("[AppPage] Dispatcher: calling SetSourceAsync for app id=%d\n", app->Id); } catch(...) {}
                                        concurrency::create_task(img->SetSourceAsync(stream)).then([weakThis, app, img, stream]() {
                                            try {
                                                try { Utils::Logf("[AppPage] SetSourceAsync continuation for app id=%d (app=%p img=%p stream=%p)\n", app->Id, app, img, stream); } catch(...) {}
                                                if (app == nullptr || img == nullptr) {
                                                    Utils::Log("[AppPage] SetSourceAsync continuation: app or img null, skipping assignment\n");
                                                    return;
                                                }
                                                auto thatCont = weakThis.Resolve<AppPage>();
                                                if (thatCont == nullptr) {
                                                    Utils::Log("[AppPage] SetSourceAsync continuation: page is gone, skipping UI assignment\n");
                                                    return;
                                                }
                                                try {
                                                    thatCont->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([app, img]() {
                                                        try {
                                                            try { Utils::Logf("[AppPage] Dispatcher (final): assigning app->BlurredImage (app=%p img=%p)\n", app, img); } catch(...) {}
                                                            app->BlurredImage = img;
                                                            try { Utils::Logf("[AppPage] assigned app->BlurredImage for id=%d\n", app->Id); } catch(...) {}
                                                        } catch(...) { Utils::Log("[AppPage] assign app->BlurredImage threw in final dispatcher\n"); }
                                                    }));
                                                } catch(...) {
                                                    Utils::Log("[AppPage] failed to RunAsync final assignment, attempting direct assign\n");
                                                    try { app->Image = img; } catch(...) { Utils::Log("[AppPage] direct assign app->Image threw\n"); }
                                                }
                                            } catch(...) { Utils::Log("[AppPage] unexpected exception in SetSourceAsync continuation\n"); }
                                        }, concurrency::task_continuation_context::use_arbitrary());
                                    } catch(...) {}
                                }));
                            } catch(...) {}
                        } catch(...) {}
                    }, concurrency::task_continuation_context::use_current());
                } catch(...) {}
            }
        }
    } catch(...) {}

    // Start background polling for app running state and connectivity
    // Disabled temporarily to isolate native access-violation crashes during debugging
    static const bool kDisableBackgroundAppFetch = true;
    if (!kDisableBackgroundAppFetch) {
        continueAppFetch.store(true);
        wasConnected.store(host->Connected);
        Platform::WeakReference weakThis(this);
        create_task([weakThis]() {
            while (true) {
                auto that = weakThis.Resolve<AppPage>();
                if (that == nullptr) break;
                try { Utils::Logf("AppPage: background loop iteration that=%p host=%p\n", that, that->host); } catch(...) {}
                try {
                    if (that->host != nullptr) {
                        try {
                            Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
                                Windows::UI::Core::CoreDispatcherPriority::Normal,
                                ref new Windows::UI::Core::DispatchedHandler([weakThis]() {
                                    auto thatInner = weakThis.Resolve<AppPage>();
                                    try {
                                        if (thatInner != nullptr && thatInner->host != nullptr) thatInner->host->UpdateAppRunningStates();
                                    } catch(...) {}
                                })
                            );
                        } catch(...) {
                            try { if (that->host != nullptr) that->host->UpdateAppRunningStates(); } catch(...) {}
                        }
                        bool connected = false;
                        try { connected = (that->host->Connect() == 0); } catch (...) { connected = false; }
                        if (that->wasConnected.load() && !connected) {
                            that->wasConnected.store(false);
                            Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([weakThis]() {
                                auto thatInner = weakThis.Resolve<AppPage>();
                                try {
                                    if (thatInner == nullptr) return;
                                    auto dialog = ref new Windows::UI::Xaml::Controls::ContentDialog();
                                    dialog->Title = Utils::StringFromStdString("Disconnected");
                                    dialog->Content = Utils::StringFromStdString("Connection to host was lost.");
                                    dialog->PrimaryButtonText = Utils::StringFromStdString("OK");
                                    concurrency::create_task(::moonlight_xbox_dx::ModalDialog::ShowOnceAsync(dialog)).then([weakThis](Windows::UI::Xaml::Controls::ContentDialogResult result) {
                                        auto thatFinal = weakThis.Resolve<AppPage>();
                                        if (thatFinal == nullptr) return;
                                        thatFinal->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([weakThis]() {
                                            auto thatNavigate = weakThis.Resolve<AppPage>();
                                            try {
                                                if (thatNavigate != nullptr) thatNavigate->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSelectorPage::typeid));
                                            } catch (...) {}
                                        }));
                                    });
                                } catch (...) {}
                            }));
                        }
                        else if (!that->wasConnected.load() && connected) {
                            that->wasConnected.store(true);
                        }
                    }
                } catch (...) {}
                // sleep but check if page still exists every 300ms to allow quick exit
                for (int i = 0; i < 10; ++i) {
                    auto thatChk = weakThis.Resolve<AppPage>();
                    if (thatChk == nullptr) return;
                    Sleep(300);
                }
            }
        });
    } else {
        Utils::Log("AppPage: background app-fetch disabled for debugging\n");
    }

	if (host->AutostartID >= 0 && GetApplicationState()->shouldAutoConnect) {
		GetApplicationState()->shouldAutoConnect = false;
		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
			Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([this]() {
				this->Connect(host->AutostartID);
			}));
	}
	GetApplicationState()->shouldAutoConnect = false;
}

void AppPage::AppsGrid_ItemClick(Platform::Object ^ sender, Windows::UI::Xaml::Controls::ItemClickEventArgs ^ e) {
	MoonlightApp ^ app = (MoonlightApp ^) e->ClickedItem;
    this->currentApp = app;

	if (this->host != nullptr) {
		for (unsigned int i = 0; i < this->host->Apps->Size; ++i) {
			auto candidate = this->host->Apps->GetAt(i);
			if (candidate != nullptr && candidate->CurrentlyRunning && candidate->Id != app->Id) {
				this->closeAndStartButton_Click(nullptr, nullptr);
				return;
			}
		}
	}

	this->Connect(app->Id);
}

void AppPage::Connect(int appId) {
	StreamConfiguration ^ config = ref new StreamConfiguration();
	config->hostname = host->LastHostname;
	config->appID = appId;
	config->width = host->Resolution->Width;
	config->height = host->Resolution->Height;
	config->bitrate = host->Bitrate;
	config->FPS = host->FPS;
	config->audioConfig = host->AudioConfig;
	config->videoCodec = host->VideoCodec;
	config->playAudioOnPC = host->PlayAudioOnPC;
	config->enableHDR = host->EnableHDR;
	config->enableSOPS = host->EnableSOPS;
	config->enableStats = host->EnableStats;
	config->enableGraphs = host->EnableGraphs;
	if (config->enableHDR) {
		host->VideoCodec = "HEVC (H.265)";
	}
 bool result = this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(StreamPage::typeid), config);
	if (!result) {
		printf("C");
	}
}

void AppPage::AppsGrid_RightTapped(Platform::Object ^ sender, Windows::UI::Xaml::Input::RightTappedRoutedEventArgs ^ e) {
	Utils::Log("AppPage::AppsGrid_RightTapped invoked\n");
	FrameworkElement ^ senderElement = (FrameworkElement ^) e->OriginalSource;
	FrameworkElement ^ anchor = senderElement;

    if (senderElement != nullptr && senderElement->GetType()->FullName->Equals(ListViewItem::typeid->FullName)) {
        auto gi = (ListViewItem ^) senderElement;
        currentApp = (MoonlightApp ^)(gi->Content);
        anchor = gi;
	} else {
		if (senderElement != nullptr) currentApp = (MoonlightApp ^)(senderElement->DataContext);

		if (currentApp == nullptr && this->AppsGrid != nullptr && this->AppsGrid->SelectedIndex >= 0) {
			currentApp = (MoonlightApp ^) this->AppsGrid->SelectedItem;
            auto container = (ListViewItem ^) this->AppsGrid->ContainerFromIndex(this->AppsGrid->SelectedIndex);
            if (container != nullptr)
                anchor = container;
			else
				anchor = this->AppsGrid;
		}
	}

	bool anyRunning = false;
	MoonlightApp ^ runningApp = nullptr;
	if (this->host != nullptr) {
		for (unsigned int i = 0; i < this->host->Apps->Size; ++i) {
			auto candidate = this->host->Apps->GetAt(i);
			if (candidate != nullptr && candidate->CurrentlyRunning) {
				anyRunning = true;
				runningApp = candidate;
				break;
			}
		}
	}

	this->resumeAppButton->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	this->closeAppButton->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	this->closeAndStartButton->Visibility = Windows::UI::Xaml::Visibility::Collapsed;

	if (!anyRunning) {
		this->resumeAppButton->Text = "Open App";
		this->resumeAppButton->Visibility = Windows::UI::Xaml::Visibility::Visible;
	} else {
		if (currentApp != nullptr && currentApp->CurrentlyRunning) {
			this->resumeAppButton->Text = "Resume App";
			this->resumeAppButton->Visibility = Windows::UI::Xaml::Visibility::Visible;
			this->closeAppButton->Visibility = Windows::UI::Xaml::Visibility::Visible;
		} else {
			if (currentApp != nullptr) {
				this->closeAndStartButton->Visibility = Windows::UI::Xaml::Visibility::Visible;
			}
		}
	}

	if (anchor != nullptr) {
		this->ActionsFlyout->ShowAt(anchor);
	} else {
		this->ActionsFlyout->ShowAt(this->AppsGrid);
	}
}

void AppPage::SearchBox_TextChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::TextChangedEventArgs^ e) {
    try {
        auto tb = dynamic_cast<TextBox^>(sender);
        if (tb == nullptr) return;
        bool collectionChanged = !ApplyAppFilter(tb->Text);
		if (collectionChanged) {
			this->EnsureRealizedContainersInitialized(this->AppsGrid);
			this->AppsGrid->SelectedIndex = this->AppsGrid->SelectedIndex > -1 ? this->AppsGrid->SelectedIndex : 0;
			this->AppsGrid_SelectionChanged(this->AppsGrid, nullptr);
			this->CenterSelectedItem(1, true);
        }
    } catch (...) {}
}

void AppPage::resumeAppButton_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	this->Connect(this->currentApp->Id);
}

void AppPage::closeAndStartButton_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	if (this->currentApp == nullptr) {
		return;
	}

	if (sender != nullptr) {
		this->ExecuteCloseAndStart();
		return;
	}

	auto dialog = ref new Windows::UI::Xaml::Controls::ContentDialog();
	dialog->Title = Utils::StringFromStdString("Confirm");
	dialog->Content = Utils::StringFromStdString("Close currently running app and connect?");
	dialog->PrimaryButtonText = Utils::StringFromStdString("Yes");
	dialog->CloseButtonText = Utils::StringFromStdString("Cancel");

	Platform::WeakReference weakThis(this);

	concurrency::create_task(::moonlight_xbox_dx::ModalDialog::ShowOnceAsync(dialog)).then([weakThis](Windows::UI::Xaml::Controls::ContentDialogResult result) {
		try {
			if (result != Windows::UI::Xaml::Controls::ContentDialogResult::Primary) {
				return;
			}

			auto that = weakThis.Resolve<AppPage>();
			if (that == nullptr) return;
			that->ExecuteCloseAndStart();
		} catch (const std::exception &e) {
			Utils::Logf("closeAndStartButton_Click dialog task exception: %s\n", e.what());
		} catch (...) {
			Utils::Log("closeAndStartButton_Click dialog task unknown exception\n");
		}
	});
}

void AppPage::ExecuteCloseAndStart() {
    Platform::WeakReference weakThis(this);
    auto progressToken = ::moonlight_xbox_dx::ModalDialog::ShowProgressDialogToken(nullptr, Utils::StringFromStdString("Closing app..."));
    moonlight_xbox_dx::Utils::Logf("AppPage::ExecuteCloseAndStart: progressToken=%llu\n", (unsigned long long)progressToken);

    concurrency::create_task(concurrency::create_async([weakThis, progressToken]() {
		try {
            auto thatLocal = weakThis.Resolve<AppPage>();
            if (thatLocal == nullptr) return;
            MoonlightClient client;
            auto ipAddr = Utils::PlatformStringToStdString(thatLocal->host->LastHostname);
            int status = client.Connect(ipAddr.c_str());
            if (status == 0) {
                client.StopApp();
                Sleep(1000);
            }
		} catch (...) {
		}
        auto thatLocal2 = weakThis.Resolve<AppPage>();
        if (thatLocal2 == nullptr) return;
        thatLocal2->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([weakThis, progressToken]() {
                auto thatUI = weakThis.Resolve<AppPage>();
                try {
                    if (thatUI != nullptr && thatUI->currentApp != nullptr) {
                        thatUI->Connect(thatUI->currentApp->Id);
                    }
                    ::moonlight_xbox_dx::ModalDialog::HideDialogByToken(progressToken);
                } catch (const std::exception &e) {
                    Utils::Logf("ExecuteCloseAndStart UI exception: %s\n", e.what());
                } catch (...) {
                    Utils::Log("ExecuteCloseAndStart UI unknown exception\n");
                }
            }));
	})).then([](concurrency::task<void> t) {
		try {
			t.get();
		} catch (const std::exception &e) {
			Utils::Logf("ExecuteCloseAndStart task exception: %s\n", e.what());
		} catch (...) {
			Utils::Log("ExecuteCloseAndStart unknown task exception\n");
		}
	});
}

void AppPage::closeAppButton_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
    Platform::WeakReference weakThis(this);

    auto progressToken = ::moonlight_xbox_dx::ModalDialog::ShowProgressDialogToken(Utils::StringFromStdString("Closing"), Utils::StringFromStdString("Closing app..."));
    moonlight_xbox_dx::Utils::Logf("AppPage::closeAppButton_Click: progressToken=%llu\n", (unsigned long long)progressToken);

    concurrency::create_task(concurrency::create_async([weakThis, progressToken]() {
        try {
            auto thatLocal = weakThis.Resolve<AppPage>();
            if (thatLocal == nullptr) return;
            MoonlightClient client;
            auto ipAddr = Utils::PlatformStringToStdString(thatLocal->host->LastHostname);
            int status = client.Connect(ipAddr.c_str());
            if (status == 0) {
                client.StopApp();
                Sleep(1000);
            }
        } catch (...) {
        }

        auto thatLocal2 = weakThis.Resolve<AppPage>();
        if (thatLocal2 == nullptr) return;
        thatLocal2->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([weakThis, progressToken]() {
                auto thatUI = weakThis.Resolve<AppPage>();
                try {
                    if (thatUI != nullptr) {
                        thatUI->host->UpdateHostInfo(true);
                        thatUI->host->UpdateAppRunningStates();
                    }
                } catch (...) {
                }
                ::moonlight_xbox_dx::ModalDialog::HideDialogByToken(progressToken);
            }));
    }));
}

void AppPage::backButton_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	this->Frame->GoBack();
}

void AppPage::settingsButton_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	bool result = this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSettingsPage::typeid), Host);
}

void AppPage::helpButton_Click(Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^)
{
    auto path = ref new Platform::String(L"/Pages/HelpDialog.xaml");
    concurrency::create_task(::moonlight_xbox_dx::XamlHelper::LoadXamlFileAsStringAsync(path)).then([this](Platform::String^ xaml) {
        try {
            ::moonlight_xbox_dx::ModalDialog::ShowOnceAsyncWithXaml(xaml, nullptr, Utils::StringFromStdString("OK"));
        } catch(...) {}
    });
}

void AppPage::OnBackRequested(Platform::Object ^ e, Windows::UI::Core::BackRequestedEventArgs ^ args) {
	// UWP on Xbox One triggers a back request whenever the B
	// button is pressed which can result in the app being
	// suspended if unhandled
	if (this->Frame->CanGoBack) {
		this->Frame->GoBack();
		args->Handled = true;
	}
}

void AppPage::AppsGrid_SizeChanged(Platform::Object ^ sender, Windows::UI::Xaml::SizeChangedEventArgs ^ e) {
    // No-op: disable adjusting ItemsPanel margin which previously caused unexpected clipping/blank-first-item.
    (void)sender; (void)e;
}

void AppPage::AppsGrid_Unloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
    // Intentionally empty: lifecycle hook for AppsGrid Unloaded
    (void)sender; (void)e;
}

void AppPage::AppsGrid_StatusChanged(Platform::Object^ sender, Platform::Object^ args) {
    // Intentionally empty: lifecycle hook for AppsGrid status changes
    (void)sender; (void)args;
}

void AppPage::AppsGrid_ItemsChanged(Platform::Object^ sender, Platform::Object^ args) {
    // Intentionally empty: lifecycle hook for AppsGrid items changed
    (void)sender; (void)args;
}

void AppPage::PageRoot_SizeChanged(Platform::Object ^ sender, Windows::UI::Xaml::SizeChangedEventArgs ^ e) {
    // Trigger an immediate update of item heights so AspectRatioBox.Height bindings re-evaluate.
    UpdateItemHeights();
}

void AppPage::UpdateItemHeights() {
    try {
        if (this->AppsGrid == nullptr) return;
        double listTarget = this->AppsGrid->ActualHeight * 0.85;
        bool isGrid = m_isGridLayout;
        unsigned int gridColumns = 0;
        double perItemWidth = 0.0;
        if (isGrid) {
            // try to determine number of columns from the ItemsPanel (ItemsWrapGrid.MaximumRowsOrColumns)
            try {
                auto panel = dynamic_cast<ItemsWrapGrid^>(this->AppsGrid->ItemsPanelRoot);
                if (panel != nullptr) {
                    gridColumns = (unsigned int)panel->MaximumRowsOrColumns;
                }
            } catch(...) { gridColumns = 0; }
            if (gridColumns == 0) {
                // fallback to 3 columns
                gridColumns = 3;
            }
            // compute per-item width by dividing available AppsGrid width by columns, accounting for margins/padding roughly
            double totalWidth = this->AppsGrid->ActualWidth;
            if (totalWidth > 0.0) perItemWidth = std::floor(totalWidth / (double)gridColumns);
        }

        // First pass: gather realized AspectRatioBox elements and compute their desired heights
        struct ItemSize { FrameworkElement^ fe; double desiredH; };
        std::vector<ItemSize> items;

        for (unsigned int i = 0; i < this->AppsGrid->Items->Size; ++i) {
            auto container = dynamic_cast<ListViewItem^>(this->AppsGrid->ContainerFromIndex(i));
            if (container == nullptr) continue;

            // Determine available height for the image inside this container.
            const double reserved = 50.0; // tuned: border padding + title area + margins
            double containerHeight = container->ActualHeight;
            double availableH = containerHeight - reserved;
            // Fallback to list-based target if container height is not yet realized
            if (availableH <= 0.0) availableH = listTarget;

            // Determine available width from container or compute from grid columns
            double containerWidth = container->ActualWidth;
            double availableW = containerWidth;
            if (availableW <= 0.0) availableW = this->AppsGrid->ActualWidth;
            if (isGrid && perItemWidth > 0.0) availableW = perItemWidth;

            // recursive search for AspectRatioBox by runtime type name inside container
            std::function<DependencyObject^(DependencyObject^)> find = [&](DependencyObject^ parent)->DependencyObject^ {
                if (parent == nullptr) return nullptr;
                int count = VisualTreeHelper::GetChildrenCount(parent);
                for (int j = 0; j < count; ++j) {
                    DependencyObject^ child = VisualTreeHelper::GetChild(parent, j);
                    auto fe = dynamic_cast<FrameworkElement^>(child);
                    if (fe != nullptr) {
                        if (fe->GetType()->FullName == "moonlight_xbox_dx.AspectRatioBox") return child;
                    }
                    auto rec = find(child);
                    if (rec != nullptr) return rec;
                }
                return nullptr;
            };

            auto found = find(container);
            if (found != nullptr) {
                auto fe = dynamic_cast<FrameworkElement^>(found);
                    if (fe != nullptr) {
                        // Compute desired height based on available width and the AspectRatioBox ratio
                        double desiredH = listTarget;
                        try {
                            // AspectRatioBox Ratio is 0.65 in XAML; compute height = width / ratio
                            double ratio = 0.65;
                            if (availableW > 0.0 && ratio > 0.0) {
                                double h = availableW / ratio;
                                // Limit height to not exceed listTarget to avoid overly large items
                                if (h > listTarget) h = listTarget;
                                if (h > 0.0) desiredH = h;
                            }
                        } catch(...) {}
                        if (desiredH < 0.0) desiredH = 0.0;
                        items.push_back({ fe, desiredH });
                    }
            }
        }

        if (items.empty()) return;

        // Apply the uniform target height to all realized items
        for (auto &it : items) {
            auto fe = it.fe;
            if (fe == nullptr) continue;
            double prevH = fe->Height;
            if (std::isnan(prevH) || std::fabs(prevH - it.desiredH) > 1.0) {
                fe->Height = it.desiredH;
                fe->InvalidateMeasure();
                fe->UpdateLayout();
            }
        }
    } catch (...) {}
}

void AppPage::OnLoaded(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	m_back_cookie = navigation->BackRequested += ref new EventHandler<BackRequestedEventArgs ^>(this, &AppPage::OnBackRequested);

    // No diagnostics attached in OnLoaded (clean build)

    try {
        auto window = Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow;
        if (window != nullptr) {
            m_keydown_cookie = window->KeyDown += ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &AppPage::OnGamepadKeyDown);
        }
    } catch(...) {}

    // Subscribe to a one-shot Rendering event so we can run logic after first visual render.
    try {
        // Save token so we can unsubscribe correctly later
        m_rendering_token = Windows::UI::Xaml::Media::CompositionTarget::Rendering += ref new EventHandler<Object^>(this, &AppPage::OnFirstRender);
    } catch(...) {}
}

void AppPage::OnUnloaded(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
    try {
        Windows::UI::Core::SystemNavigationManager::GetForCurrentView()->BackRequested -= m_back_cookie;
    } catch (...) {
    }
	continueAppFetch.store(false);

    try {
        auto window = Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow;
        if (window != nullptr) {
            try { window->KeyDown -= m_keydown_cookie; } catch(...) {}
        }
    } catch(...) {}
    // Detach host Apps collection changed handler
    try {
        if (this->host != nullptr && this->host->Apps != nullptr) {
            auto obs = dynamic_cast<Windows::Foundation::Collections::IObservableVector<MoonlightApp^>^>(this->host->Apps);
            if (obs != nullptr) {
                try { obs->VectorChanged -= m_apps_changed_token; } catch(...) {}
            }
        }
    } catch(...) {}
 
    // Unsubscribe ContainerContentChanging if still attached
    try {
        if (this->AppsGrid != nullptr) {
            if (this->m_container_content_changing_token.Value != 0) {
                this->AppsGrid->ContainerContentChanging -= this->m_container_content_changing_token;
            }
        }
    } catch(...) {}

     // Unsubscribe one-shot LayoutUpdated handler if still attached
    if (this->AppsGrid != nullptr) {
        if (this->m_layoutUpdated_token.Value != 0) {
            try { this->AppsGrid->LayoutUpdated -= this->m_layoutUpdated_token; } catch(...) {}
        }
    }

    // Unsubscribe Rendering token if still registered
    try { Windows::UI::Xaml::Media::CompositionTarget::Rendering -= m_rendering_token; } catch(...) {}

    // Unsubscribe ScrollViewer ViewChanged if we registered it
    try {
        if (m_scrollViewer != nullptr) {
            try { m_scrollViewer->ViewChanged -= m_scrollviewer_viewchanged_token; } catch(...) {}
        }
    } catch(...) {}
}

void AppPage::OnFirstRender(Object^ sender, Object^ e) {
    // Unsubscribe so it only runs once
    try { Windows::UI::Xaml::Media::CompositionTarget::Rendering -= m_rendering_token; } catch(...) {}
    
    Utils::Log("AppPage::OnFirstRender: first render occurred\n");
    try {
		this->AppsGrid->SelectedIndex = this->AppsGrid->SelectedIndex > -1 ? this->AppsGrid->SelectedIndex : 0;
		this->AppsGrid_SelectionChanged(this->AppsGrid, nullptr);
		this->CenterSelectedItem(1, true);
    } catch(...) {}
}

void AppPage::OnHostAppsChanged(Windows::Foundation::Collections::IObservableVector<MoonlightApp^>^ sender, Windows::Foundation::Collections::IVectorChangedEventArgs^ args) {
    try {
        // Reapply the current search filter on the UI thread
        this->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([this]() {
            try {
                ApplyAppFilter(this->SearchBox != nullptr ? this->SearchBox->Text : nullptr);
            } catch(...) {}
        }));
    } catch(...) {}
}

// Helper: find ScrollViewer inside ListView template
static ScrollViewer^ FindScrollViewer(DependencyObject^ parent) {
    if (parent == nullptr) return nullptr;
    int count = VisualTreeHelper::GetChildrenCount(parent);
    for (int i = 0; i < count; ++i) {
        auto child = VisualTreeHelper::GetChild(parent, i);
        auto sv = dynamic_cast<ScrollViewer^>(child);
        if (sv != nullptr) return sv;
        auto rec = FindScrollViewer(child);
        if (rec != nullptr) return rec;
    }
    return nullptr;
}

// Static helper: attempt to center a container vertically using ChangeView with retries
void AppPage::TryVerticalCentering(Windows::UI::Xaml::Controls::ScrollViewer^ sv, Windows::UI::Xaml::Controls::ListViewItem^ container, int retries, AppPage^ page) {
    if (sv == nullptr || container == nullptr || page == nullptr) return;
    try {
        Windows::Foundation::Point pt{0,0};
        try {
            auto trans = container->TransformToVisual(sv);
            if (trans != nullptr) pt = trans->TransformPoint(Windows::Foundation::Point{ 0, 0 });
            else Utils::Log("ApplyCenteringTransformIfNeeded: TransformToVisual returned null\n");
        } catch(...) { Utils::Log("ApplyCenteringTransformIfNeeded: TransformToVisual/TransformPoint threw\n"); }
        double containerCenterY = pt.Y + container->ActualHeight / 2.0;
        double desiredY = containerCenterY - (sv->ViewportHeight / 2.0);
        if (desiredY < 0) desiredY = 0;
        if (desiredY > sv->ScrollableHeight) desiredY = sv->ScrollableHeight;

        try { Utils::Logf("TryVerticalCentering: desiredY=%.2f ViewportHeight=%.2f ScrollableHeight=%.2f retries=%d\n", desiredY, sv->ViewportHeight, sv->ScrollableHeight, retries); } catch(...) {}

        if (sv->ScrollableHeight > 1.0)
        {
            try
            {
                sv->ChangeView(nullptr, desiredY, nullptr, true);
                Utils::Logf("TryVerticalCentering: ChangeView invoked to %.2f (disableAnimation=true)\n", desiredY);

                // Store pending offset so OnScrollViewerViewChanged can re-apply
                // if the ScrollViewer ends up reporting a different clamped value.
                try {
                    if (page != nullptr) {
                        page->m_pendingVerticalOffset = desiredY;
                        page->m_hasPendingVerticalOffset = true;
                        Utils::Logf("TryVerticalCentering: pendingVerticalOffset set to %.2f\n", desiredY);
                    }
                } catch(...) {}
            }
            catch (...) { Utils::Logf("TryVerticalCentering: ChangeView threw\n"); }
            return;
        }

        if (retries <= 0) {
            try {
                if (page->m_disableCenteringFallback) {
                    try { Utils::Logf("TryVerticalCentering: retries exhausted but fallback disabled (m_disableCenteringFallback=true). Skipping ApplyCenteringTransformIfNeeded\n"); } catch(...) {}
                } else {
                    try { Utils::Logf("TryVerticalCentering: retries exhausted, invoking ApplyCenteringTransformIfNeeded\n"); } catch(...) {}
                    try { page->ApplyCenteringTransformIfNeeded(container); } catch(...) {}
                }
            } catch(...) {}
            return;
        }

        int next = retries - 1;
        // Schedule a lightweight retry on the UI thread without forcing synchronous layout
        page->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([sv, container, next, page]() {
            try {
                AppPage::TryVerticalCentering(sv, container, next, page);
            } catch(...) {}
        }));
    } catch(...) {}
}

void AppPage::AppsGrid_SelectionChanged(Platform::Object ^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs ^ e) {
    auto lv = (ListView^)sender;
    if (lv == nullptr) return;

    Platform::Object^ prevItem = nullptr;
    try {
        if (e != nullptr && e->RemovedItems != nullptr && e->RemovedItems->Size > 0) {
            prevItem = e->RemovedItems->GetAt(0);
        }
    } catch(...) { prevItem = nullptr; }

    EnsureRealizedContainersInitialized(lv);

    if (lv->SelectedIndex < 0) return;
    try { this->HandleSelectionChangedAsync(lv, prevItem); } catch(...) {}
}

void AppPage::HandleSelectionChangedAsync(Windows::UI::Xaml::Controls::ListView^ lv, Platform::Object^ prevItem) {
    if (lv == nullptr) return;
    auto item = lv->SelectedItem;
    if (item == nullptr) return;

    // Helper to obtain a realized container, optionally forcing a ScrollIntoView for non-grid layouts.
    auto findOrEnsureContainer = [&](Platform::Object^ it)->ListViewItem^ {
        if (it == nullptr) return nullptr;
        ListViewItem^ c = nullptr;
        try { c = dynamic_cast<ListViewItem^>(lv->ContainerFromItem(it)); } catch(...) { c = nullptr; }
        if (c == nullptr && !m_isGridLayout) {
            try { lv->ScrollIntoView(it); } catch(...) {}
            // Container may be realized synchronously after ScrollIntoView; try again.
            try { c = dynamic_cast<ListViewItem^>(lv->ContainerFromItem(it)); } catch(...) { c = nullptr; }
        }
        return c;
    };

    ListViewItem^ container = findOrEnsureContainer(item);
	if (!m_isGridLayout) {
		this->CenterSelectedItem(1, true);
    }

    ListViewItem^ prevContainer = nullptr;
    if (prevItem != nullptr) {
        if (!m_isGridLayout) {
            try { lv->ScrollIntoView(prevItem); } catch(...) {}
        }
        try { prevContainer = dynamic_cast<ListViewItem^>(lv->ContainerFromItem(prevItem)); } catch(...) { prevContainer = nullptr; }
    }

    // Apply selection visuals to the new and previous containers
    try {
        if (container != nullptr) ApplyVisualsToContainer(container, true);
        if (prevContainer != nullptr) ApplyVisualsToContainer(prevContainer, false);
    } catch(...) {}

    // Update the shared SelectedApp text overlay
    try {
        if (this->SelectedAppText != nullptr && this->SelectedAppBox != nullptr) {
            auto selApp = dynamic_cast<moonlight_xbox_dx::MoonlightApp^>(lv->SelectedItem);
            auto res = this->Resources;
            if (selApp != nullptr) {
                this->SelectedAppText->Text = selApp->Name;
                this->SelectedAppBox->Visibility = Windows::UI::Xaml::Visibility::Visible;
                this->SelectedAppText->Visibility = Windows::UI::Xaml::Visibility::Visible;
                this->SelectedAppBox->Background = ref new SolidColorBrush(Windows::UI::Colors::Transparent);
                this->SelectedAppText->Foreground = ref new SolidColorBrush(Windows::UI::Colors::White);
                SetElementOpacityImmediate(this->SelectedAppBox, 0.0f);
                SetElementOpacityImmediate(this->SelectedAppText, 0.0f);
                if (res != nullptr) {
                    auto sbObj = res->Lookup(ref new Platform::String(L"ShowSelectedAppStoryboard"));
                    auto sb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(sbObj);
                    if (sb != nullptr) sb->Begin();
                }
            } else {
                if (res != nullptr) {
                    auto sbObj = res->Lookup(ref new Platform::String(L"HideSelectedAppStoryboard"));
                    auto sb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(sbObj);
                    if (sb != nullptr) sb->Begin();
                    else {
                        if (allowOpacityTransitions && m_compositionReady) {
                            AnimateElementOpacity(this->SelectedAppBox, 0.0f, kAnimationDurationMs);
                            AnimateElementOpacity(this->SelectedAppText, 0.0f, kAnimationDurationMs);
                        } else {
                            SetElementOpacityImmediate(this->SelectedAppBox, 0.0f);
                            SetElementOpacityImmediate(this->SelectedAppText, 0.0f);
                        }
                    }
                }
            }
        }
    } catch(...) {}
}

void AppPage::AppsGrid_Loaded(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	try {
		Utils::Logf("AppsGrid_Loaded fired.\n");
		if (m_scrollViewer == nullptr)
			m_scrollViewer = FindScrollViewer(this->AppsGrid);

        if (m_scrollViewer != nullptr) {
            try {
                // Register ViewChanged and save the token so we can unsubscribe in OnUnloaded.
                m_scrollviewer_viewchanged_token = m_scrollViewer->ViewChanged += ref new Windows::Foundation::EventHandler<Windows::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs ^>(this, &AppPage::OnScrollViewerViewChanged);
            } catch(...) {}
        }
		try {
			auto weakThis = WeakReference(this);
			Windows::UI::Xaml::Media::CompositionTarget::Rendering += ref new Windows::Foundation::EventHandler<Object ^>([weakThis](Object ^, Object ^) {
				try {
					auto that = weakThis.Resolve<AppPage>();
					if (that != nullptr) {
						that->m_compositionReady = true;
					}
				} catch (...) {
				}
			});
		} catch (...) {
		}
	} catch (...) {
	}
}

void AppPage::AppsGrid_ContextRequested(Windows::UI::Xaml::UIElement^ sender, Windows::UI::Xaml::Input::ContextRequestedEventArgs^ e) {
    try {
        Utils::Log("AppPage::AppsGrid_ContextRequested invoked\n");
        // Determine original source and set currentApp similar to RightTapped handler
        auto original = dynamic_cast<FrameworkElement^>(e->OriginalSource);
        FrameworkElement^ anchor = this->AppsGrid;
        if (original != nullptr) {
            auto gvItem = dynamic_cast<ListViewItem^>(original);
            if (gvItem != nullptr) {
                currentApp = (MoonlightApp^)gvItem->Content;
                anchor = gvItem;
            } else {
                auto dc = original->DataContext;
                if (dc != nullptr) currentApp = (MoonlightApp^)dc;
            }
        }

        // Configure flyout buttons based on running state
        this->resumeAppButton->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
        this->closeAppButton->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
        this->closeAndStartButton->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
        bool anyRunning = false; MoonlightApp^ runningApp = nullptr;
        if (this->host != nullptr) {
            for (unsigned int i = 0; i < this->host->Apps->Size; ++i) {
                auto candidate = this->host->Apps->GetAt(i);
                if (candidate != nullptr && candidate->CurrentlyRunning) { anyRunning = true; runningApp = candidate; break; }
            }
        }
        if (!anyRunning) {
            this->resumeAppButton->Text = "Open App";
            this->resumeAppButton->Visibility = Windows::UI::Xaml::Visibility::Visible;
        } else {
            if (currentApp != nullptr && currentApp->CurrentlyRunning) {
                this->resumeAppButton->Text = "Resume App";
                this->resumeAppButton->Visibility = Windows::UI::Xaml::Visibility::Visible;
                this->closeAppButton->Visibility = Windows::UI::Xaml::Visibility::Visible;
            } else {
                if (currentApp != nullptr) this->closeAndStartButton->Visibility = Windows::UI::Xaml::Visibility::Visible;
            }
        }

        if (anchor != nullptr) this->ActionsFlyout->ShowAt(anchor);
        else this->ActionsFlyout->ShowAt(this->AppsGrid);

        e->Handled = true;
    } catch (...) {}
}

void AppPage::AppsGrid_ContainerContentChanging(ListViewBase ^ sender, ContainerContentChangingEventArgs ^ args)
{
	try {
        unsigned long tid = 0;
        try { tid = ::GetCurrentThreadId(); } catch(...) { tid = 0; }
        int hasAccess = 0;
        try { hasAccess = this->Dispatcher != nullptr && this->Dispatcher->HasThreadAccess ? 1 : 0; } catch(...) { hasAccess = 0; }
        try { Utils::Logf("[AppPage] AppsGrid_ContainerContentChanging called on thread=%u HasThreadAccess=%d\n", tid, hasAccess); } catch(...) {}

        if (!hasAccess) {
            // If this handler is invoked on a non-UI thread, re-dispatch the work to the UI thread and return.
            auto weakThis = WeakReference(this);
            auto senderCopy = sender;
            auto argsCopy = args;
            try {
                this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([weakThis, senderCopy, argsCopy]() {
                    try {
                        auto that = weakThis.Resolve<AppPage>();
                        if (that == nullptr) return;
                        try {
                            if (argsCopy->ItemIndex == 0 && argsCopy->Phase == 0) {
                                auto container = dynamic_cast<ListViewItem ^>(argsCopy->ItemContainer);
                                if (container != nullptr) {
                                    if (that->m_layoutNew) {
                                        try { Utils::Logf("[AppPage] (dispatched) AppsGrid_ContainerContentChanging fired\n"); } catch(...) {}
                                    }
                                }
                            }
                        } catch(...) {}
                    } catch(...) {}
                }));
            } catch(...) {}
            return;
        }

        // Normal UI-thread path
        if (args->ItemIndex == 0 && args->Phase == 0) {
            auto container = dynamic_cast<ListViewItem ^>(args->ItemContainer);
            if (container != nullptr) {
                try { Utils::Logf("AppsGrid_ContainerContentChanging fired\n"); } catch (...) { }

                if (m_layoutNew) {
                    try { Utils::Logf("AppsGrid_ContainerContentChanging: m_layoutNew=true\n"); } catch (...) {}
                }
            }
        }
    } catch (...) {
    }
}

void AppPage::AppsGrid_LayoutUpdated(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ args) {
	try {
		if (m_layoutNew) {
		    // try { Utils::Logf("AppsGrid_LayoutUpdated fired\n"); } catch (...) {}
		    // m_layoutNew = !PollForCenterItem();
		}
	} catch (...) {
	}
}

void AppPage::AppsGrid_ScrollChanged(Platform::Object^ sender, Platform::Object^ args) {
    // Intentionally empty: lifecycle hook for scroll changes
    (void)sender; (void)args;
}

void moonlight_xbox_dx::AppPage::OnScrollViewerViewChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs^ e) {
    try {
        // Only act when scrolling has completed (not during inertia)

        // Iterate realized containers and ensure composition visuals are initialized
        if (this->AppsGrid == nullptr) return;
        for (unsigned int i = 0; i < this->AppsGrid->Items->Size; ++i) {
            auto container = dynamic_cast<ListViewItem^>(this->AppsGrid->ContainerFromIndex(i));
            if (container == nullptr) continue;
            // Force initialization via same logic used in ContainerContentChanging
            try {
                auto desFE = FindChildByName(container, "Desaturator");
                auto des = dynamic_cast<UIElement^>(desFE);
                auto imgFE = FindChildByName(container, "AppImageRect");
                auto img = dynamic_cast<UIElement^>(imgFE);
                auto nameFE = FindChildByName(container, "AppName");
                auto nameTxt = dynamic_cast<UIElement^>(nameFE);
                bool isSelected = false;
                try {
                    if (this->AppsGrid != nullptr && this->AppsGrid->SelectedIndex >= 0) {
                        int idx = this->AppsGrid->IndexFromContainer(container);
                        if (idx == this->AppsGrid->SelectedIndex) isSelected = true;
                    }
                } catch(...) { isSelected = false; }

                if (allowScaleTransitions || allowOpacityTransitions) {
                    if (img != nullptr) {
                        auto imgVis = ElementCompositionPreview::GetElementVisual(img);
                        if (imgVis != nullptr) {
                            try { imgVis->StopAnimation("Scale.X"); imgVis->StopAnimation("Scale.Y"); } catch(...) {}
                            // For initial load keep baseline immediate; only animate when composition visuals are marked ready
                            if (allowScaleTransitions && m_compositionReady) {
                                AnimateElementScale(img, kUnselectedScale, kAnimationDurationMs);
                            } else {
                                Windows::Foundation::Numerics::float3 s; s.x = kUnselectedScale; s.y = s.x; s.z = 0.0f;
                                imgVis->Scale = s;
                            }
                            auto imgFE2 = dynamic_cast<FrameworkElement^>(img);
                            if (imgFE2 != nullptr && imgFE2->ActualWidth > 0 && imgFE2->ActualHeight > 0) {
                                Windows::Foundation::Numerics::float3 cp; cp.x = (float)imgFE2->ActualWidth * 0.5f; cp.y = (float)imgFE2->ActualHeight * 0.5f; cp.z = 0.0f;
                                imgVis->CenterPoint = cp;
                            }
                        }
                    }
                    if (des != nullptr) {
                        auto desVis = ElementCompositionPreview::GetElementVisual(des);
                        if (desVis != nullptr) {
                            try { desVis->StopAnimation("Scale.X"); desVis->StopAnimation("Scale.Y"); desVis->StopAnimation("Opacity"); } catch(...) {}
                            // Set desaturator baseline for all realized items. Keep immediate on first load
                            if (allowScaleTransitions && m_compositionReady) {
                                AnimateElementScale(dynamic_cast<UIElement^>(des), kUnselectedScale, kAnimationDurationMs);
                            } else {
                                Windows::Foundation::Numerics::float3 s2; s2.x = kUnselectedScale; s2.y = s2.x; s2.z = 0.0f;
                                desVis->Scale = s2;
                            }
                            if (allowOpacityTransitions && m_compositionReady) {
                                AnimateElementOpacity(des, kDesaturatorOpacityUnselected, kAnimationDurationMs);
                            } else {
                                desVis->Opacity = kDesaturatorOpacityUnselected;
                            }
                            // Init name opacity for scroll update
                            if (nameTxt != nullptr) {
                                if (allowOpacityTransitions && m_compositionReady) {
                                    AnimateElementOpacity(nameTxt, isSelected ? 1.0f : 0.0f, kAnimationDurationMs);
                                } else {
                                    SetElementOpacityImmediate(nameTxt, isSelected ? 1.0f : 0.0f);
                                }
                            }
                            auto desFE2 = dynamic_cast<FrameworkElement^>(des);
                            if (desFE2 != nullptr && desFE2->ActualWidth > 0 && desFE2->ActualHeight > 0) {
                                Windows::Foundation::Numerics::float3 cp2; cp2.x = (float)desFE2->ActualWidth * 0.5f; cp2.y = (float)desFE2->ActualHeight * 0.5f; cp2.z = 0.0f;
                                desVis->CenterPoint = cp2;
                            }
                        }
                    }
                } else {
                    if (img != nullptr) SetElementScaleImmediate(img, isSelected ? kSelectedScale : kUnselectedScale);
                    if (des != nullptr) { SetElementScaleImmediate(des, isSelected ? kSelectedScale : kUnselectedScale); SetElementOpacityImmediate(des, isSelected ? 0.0f : kDesaturatorOpacityUnselected); }
                }
            } catch(...) {}
        }
    } catch(...) {}
}

// Keep qualified definitions outside namespace

void moonlight_xbox_dx::AppPage::ApplyCenteringTransformIfNeeded(ListViewItem^ container) {
    try {
        try { Utils::Log("[AppPage] ApplyCenteringTransformIfNeeded: start\n"); } catch(...) {}
        if (this->m_disableCenteringFallback) {
            return;
        }
        // Ensure we have a valid ScrollViewer reference; try to find it if not cached
        if (m_scrollViewer == nullptr && this->AppsGrid != nullptr) {
            try { m_scrollViewer = FindScrollViewer(this->AppsGrid); } catch(...) { m_scrollViewer = nullptr; }
        }

        if (m_scrollViewer != nullptr && m_scrollViewer->ScrollableWidth > 1.0) {
            // content scrolls normally; clear any composition transform
            if (m_itemsPanel != nullptr) {
                auto visual = ElementCompositionPreview::GetElementVisual(m_itemsPanel);
                if (visual != nullptr) {
                    // Stop any running animation by setting the Offset directly
                    try { visual->StopAnimation("Offset.X"); } catch (...) {}
                    visual->Offset = visual->Offset; // no-op to ensure stable state
                }
                m_itemsPanel->RenderTransform = nullptr;
            }
            return;
        }
        if (container == nullptr) return;
        if (m_itemsPanel == nullptr) {
            // find an ItemsWrapGrid or ItemsStackPanel within AppsGrid
            std::function<FrameworkElement^(DependencyObject^)> findItems = [&](DependencyObject^ parent)->FrameworkElement^ {
                if (parent == nullptr) return nullptr;
                int count = VisualTreeHelper::GetChildrenCount(parent);
                for (int i = 0; i < count; ++i) {
                    auto child = VisualTreeHelper::GetChild(parent, i);
                    auto fe = dynamic_cast<FrameworkElement^>(child);
                    if (fe != nullptr) {
                        // Accept either ItemsWrapGrid or ItemsStackPanel (or any panel used as items host)
                        auto typeName = fe->GetType()->FullName;
                        if (typeName != nullptr) {
                            std::wstring tn(typeName->Data());
                            if (tn.find(L"ItemsWrapGrid") != std::wstring::npos || tn.find(L"ItemsStackPanel") != std::wstring::npos) {
                                return fe;
                            }
                        }
                    }
                    auto rec = findItems(child);
                    if (rec != nullptr) return rec;
                }
                return nullptr;
            };
            m_itemsPanel = findItems(this->AppsGrid);
            if (m_itemsPanel == nullptr) {
                try {
                    if (this->AppsGrid != nullptr && this->AppsGrid->ItemsPanelRoot != nullptr) {
                        auto rootFE = dynamic_cast<FrameworkElement^>(this->AppsGrid->ItemsPanelRoot);
                        if (rootFE != nullptr) {
                            m_itemsPanel = rootFE;
                        }
                    }
                } catch(...) {}
            }
        }
        if (m_itemsPanel == nullptr) return;

        // Ensure layout updated before measuring
        container->InvalidateMeasure(); container->UpdateLayout();
        m_itemsPanel->InvalidateArrange(); m_itemsPanel->UpdateLayout();

        // Compute container position relative to items panel (panel coordinates)
        Point ptPanel{0,0};
        try {
            auto t = container->TransformToVisual(m_itemsPanel);
            if (t != nullptr) {
                ptPanel = t->TransformPoint(Point{0,0});
            } else {
                Utils::Log("ApplyCenteringTransformIfNeeded: TransformToVisual returned null for container->m_itemsPanel\n");
            }
        } catch(...) { Utils::Log("ApplyCenteringTransformIfNeeded: TransformToVisual/TransformPoint threw\n"); }
        double containerCenterInPanel = ptPanel.X + container->ActualWidth / 2.0;

        double appsGridCenter = this->AppsGrid->ActualWidth / 2.0;
        double translateX = appsGridCenter - containerCenterInPanel;
        try { Utils::Logf("[AppPage] ApplyCenteringTransformIfNeeded: containerCenterInPanel=%.2f appsGridCenter=%.2f translateX=%.2f\n", containerCenterInPanel, appsGridCenter, translateX); } catch(...) {}

        // computed translateX
        // Prefer Composition animation for smoothness. Animate the visual's Offset.X
        auto itemsVisual = ElementCompositionPreview::GetElementVisual(m_itemsPanel);
        if (itemsVisual != nullptr) {
            auto compositor = itemsVisual->Compositor;
            auto anim = compositor->CreateScalarKeyFrameAnimation();
            TimeSpan ts; ts.Duration = 200 * 10000LL; // 200ms
            anim->Duration = ts;
            anim->InsertKeyFrame(1.0f, (float)translateX);
            // stop any previous animation and start a new one
            try { itemsVisual->StopAnimation("Offset.X"); } catch (...) {}
            try {
                Utils::Log("[AppPage] ApplyCenteringTransformIfNeeded: starting composition animation\n");
                itemsVisual->StartAnimation("Offset.X", anim);
                Utils::Log("[AppPage] ApplyCenteringTransformIfNeeded: composition animation started\n");
            } catch(...) {
                Utils::Log("[AppPage] ApplyCenteringTransformIfNeeded: StartAnimation threw\n");
            }
        } else {
            // Fallback to immediate RenderTransform change
            auto tt = dynamic_cast<Windows::UI::Xaml::Media::TranslateTransform^>(m_itemsPanel->RenderTransform);
            if (tt == nullptr) tt = ref new Windows::UI::Xaml::Media::TranslateTransform();
            try { Utils::Log("[AppPage] ApplyCenteringTransformIfNeeded: using RenderTransform fallback\n"); } catch(...) {}
            tt->X = translateX;
            try { m_itemsPanel->RenderTransform = tt; } catch(...) { Utils::Log("[AppPage] ApplyCenteringTransformIfNeeded: setting RenderTransform threw\n"); }
        }
        try { Utils::Log("[AppPage] ApplyCenteringTransformIfNeeded: end\n"); } catch(...) {}
    } catch (...) {}
}

void AppPage::LayoutToggleButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
    try {
        auto toggle = dynamic_cast<Windows::UI::Xaml::Controls::Primitives::ToggleButton^>(sender);
        bool wantGrid = false;
        if (toggle != nullptr) {
            wantGrid = (toggle->IsChecked != nullptr && toggle->IsChecked->Value);
        }
        // If already in desired state, nothing to do
        if (m_isGridLayout == wantGrid) return;
        m_isGridLayout = wantGrid;

        // Switch the ItemsPanel template on the ListView
        if (this->AppsGrid != nullptr) {
            if (m_isGridLayout) {
                auto res = this->Resources;
                if (res != nullptr) {
                    auto panel = dynamic_cast<ItemsPanelTemplate^>(res->Lookup(ref new Platform::String(L"GridItemsPanelTemplate")));
                    if (panel != nullptr) this->AppsGrid->ItemsPanel = panel;
                }
                // enable vertical scrolling and show scrollbars when needed
                try { this->AppsGrid->SetValue(Windows::UI::Xaml::Controls::ScrollViewer::HorizontalScrollModeProperty, Windows::UI::Xaml::Controls::ScrollMode::Disabled); } catch(...) {}
                try { this->AppsGrid->SetValue(Windows::UI::Xaml::Controls::ScrollViewer::VerticalScrollModeProperty, Windows::UI::Xaml::Controls::ScrollMode::Enabled); } catch(...) {}
                try { this->AppsGrid->SetValue(Windows::UI::Xaml::Controls::ScrollViewer::VerticalScrollBarVisibilityProperty, Windows::UI::Xaml::Controls::ScrollBarVisibility::Auto); } catch(...) {}
            } else {
                // restore horizontal ItemsPanel template from resources
                try {
                    auto res = this->Resources;
                    if (res != nullptr) {
                        auto original = dynamic_cast<ItemsPanelTemplate^>(res->Lookup(ref new Platform::String(L"HorizontalItemsPanelTemplate")));
                        if (original != nullptr) this->AppsGrid->ItemsPanel = original;
                    }
                } catch(...) {}
                try { this->AppsGrid->SetValue(Windows::UI::Xaml::Controls::ScrollViewer::HorizontalScrollModeProperty, Windows::UI::Xaml::Controls::ScrollMode::Enabled); } catch(...) {}
                try { this->AppsGrid->SetValue(Windows::UI::Xaml::Controls::ScrollViewer::VerticalScrollModeProperty, Windows::UI::Xaml::Controls::ScrollMode::Disabled); } catch(...) {}
                try { this->AppsGrid->SetValue(Windows::UI::Xaml::Controls::ScrollViewer::VerticalScrollBarVisibilityProperty, Windows::UI::Xaml::Controls::ScrollBarVisibility::Disabled); } catch(...) {}
            }
            // Update item heights immediately for grid layout to size items correctly
            try { this->UpdateItemHeights(); } catch(...) {}
            // Reset cached panel/scrollviewer so centering logic finds the new items host
            try { m_itemsPanel = nullptr; } catch(...) {}
            try { m_scrollViewer = nullptr; } catch(...) {}
            // After toggling, ensure selected item is centered/realized
            try {
                this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this]() {
                    try {
                        if (this->AppsGrid != nullptr && this->AppsGrid->SelectedIndex >= 0) {
							EnsureRealizedContainersInitialized(this->AppsGrid);
							this->CenterSelectedItem(1, true);
							this->AppsGrid_SelectionChanged(this->AppsGrid, nullptr);
                        }
                    } catch(...) {}
                }));
            } catch(...) {}
        }
    } catch(...) {}
}

void AppPage::OnGamepadKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args) {
    try {
        auto key = args->VirtualKey;
        // Xbox controller 'Y' maps to VirtualKey::GamepadY or VirtualKey::Y depending on input routing
        using namespace Windows::System;
        if (key == VirtualKey::GamepadY) {
            // toggle layout
            this->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([this]() {
                try {
					this->SearchBox->Focus(Windows::UI::Xaml::FocusState::Programmatic);
                } catch(...) {}
            }));
            args->Handled = true;
        }

        if (key == VirtualKey::GamepadX) {
			// toggle layout
			this->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([this]() {
	            try {
		            bool newState = !this->m_isGridLayout;
		            try {
			            if (this->LayoutToggleButton != nullptr) this->LayoutToggleButton->IsChecked = newState;
		            } catch (...) {
		            }
		            this->LayoutToggleButton_Click(this->LayoutToggleButton, nullptr);
	            } catch (...) {
	            }
            }));
			args->Handled = true;
		}
    } catch(...) {}
}

void moonlight_xbox_dx::AppPage::EnsureRealizedContainersInitialized(Windows::UI::Xaml::Controls::ListView^ lv) {
     try {
         if (lv == nullptr) return;
         double listTarget = lv->ActualHeight * kAppsGridHeightFactor;

         // Update realized containers to ensure their visuals are initialized
         for (unsigned int i = 0; i < lv->Items->Size; ++i) {
             auto container = dynamic_cast<ListViewItem^>(lv->ContainerFromIndex(i));
             if (container == nullptr) continue;

             // Force a measure and arrange pass for the container
             container->InvalidateMeasure();
             container->UpdateLayout();

             // find AspectRatioBox inside the container and set Height immediately
             std::function<DependencyObject^(DependencyObject^)> find = [&](DependencyObject^ parent)->DependencyObject^ {
                 if (parent == nullptr) return nullptr;
                 int count = VisualTreeHelper::GetChildrenCount(parent);
                 for (int j = 0; j < count; ++j) {
                    DependencyObject^ child = VisualTreeHelper::GetChild(parent, j);
                    auto fe = dynamic_cast<FrameworkElement^>(child);
                    if (fe != nullptr) {
                        if (fe->GetType()->FullName == "moonlight_xbox_dx.AspectRatioBox") return child;
                    }
                    auto rec = find(child);
                    if (rec != nullptr) return rec;
                 }
                 return nullptr;
             };

             auto found = find(container);
             if (found != nullptr) {
                 auto fe = dynamic_cast<FrameworkElement^>(found);
                 if (fe != nullptr) {
                     // apply uniform height immediately so converters see consistent size
                     double desired = listTarget;
                     double prev = fe->Height;
                     if (std::isnan(prev) || std::fabs(prev - desired) > 1.0) {
                         fe->Height = desired;
                         fe->InvalidateMeasure();
                     }
                 }
             }

             // Initialize visuals for the realized container so virtualized items have correct state
             try {
                 auto desFE = FindChildByName(container, "Desaturator");
                 auto des = dynamic_cast<UIElement^>(desFE);
                 auto imgFE = FindChildByName(container, "AppImageRect");
                 auto img = dynamic_cast<UIElement^>(imgFE);
                 auto nameFE = FindChildByName(container, "AppName");
                 auto nameTxt = dynamic_cast<UIElement^>(nameFE);

                 bool isSelected = false;
                 try {
                     if (this->AppsGrid != nullptr && this->AppsGrid->SelectedIndex >= 0) {
                         int idx = this->AppsGrid->IndexFromContainer(container);
                         if (idx == this->AppsGrid->SelectedIndex) isSelected = true;
                     }
                 } catch(...) { isSelected = false; }

                 if (allowScaleTransitions || allowOpacityTransitions) {
                     // Initialize composition visual properties so animations operate from the correct baseline.
                     try {
                         if (img != nullptr) {
                             auto imgVis = ElementCompositionPreview::GetElementVisual(img);
                             if (imgVis != nullptr) {
                                 try { imgVis->StopAnimation("Scale.X"); imgVis->StopAnimation("Scale.Y"); } catch(...) {}
                                 // Initialize to unselected baseline for all realized items. Keep immediate on first load
                                 if (allowScaleTransitions && m_compositionReady) {
                                     AnimateElementScale(img, kUnselectedScale, kAnimationDurationMs);
                                 } else {
                                     Windows::Foundation::Numerics::float3 s; s.x = kUnselectedScale; s.y = s.x; s.z = 0.0f;
                                     imgVis->Scale = s;
                                 }
                                 auto imgFE2 = dynamic_cast<FrameworkElement^>(img);
                                 if (imgFE2 != nullptr && imgFE2->ActualWidth > 0 && imgFE2->ActualHeight > 0) {
                                     Windows::Foundation::Numerics::float3 cp; cp.x = (float)imgFE2->ActualWidth * 0.5f; cp.y = (float)imgFE2->ActualHeight * 0.5f; cp.z = 0.0f;
                                     imgVis->CenterPoint = cp;
                                 }
                             }
                         }
                         if (des != nullptr) {
                             auto desVis = ElementCompositionPreview::GetElementVisual(des);
                             if (desVis != nullptr) {
                                 try { desVis->StopAnimation("Scale.X"); desVis->StopAnimation("Scale.Y"); desVis->StopAnimation("Opacity"); } catch(...) {}
                                 // Set desaturator baseline for all realized items. Keep immediate on first load
                                 if (allowScaleTransitions && m_compositionReady) {
                                     AnimateElementScale(dynamic_cast<UIElement^>(des), kUnselectedScale, kAnimationDurationMs);
                                 } else {
                                     Windows::Foundation::Numerics::float3 s2; s2.x = kUnselectedScale; s2.y = s2.x; s2.z = 0.0f;
                                     desVis->Scale = s2;
                                 }
                                 if (allowOpacityTransitions && m_compositionReady) {
                                     AnimateElementOpacity(des, kDesaturatorOpacityUnselected, kAnimationDurationMs);
                                 } else {
                                     desVis->Opacity = kDesaturatorOpacityUnselected;
                                 }
                                 // Init name opacity for scroll update
                                 if (nameTxt != nullptr) {
                                     if (allowOpacityTransitions && m_compositionReady) {
                                         AnimateElementOpacity(nameTxt, isSelected ? 1.0f : 0.0f, kAnimationDurationMs);
                                     } else {
                                         SetElementOpacityImmediate(nameTxt, isSelected ? 1.0f : 0.0f);
                                     }
                                 }
                                 auto desFE2 = dynamic_cast<FrameworkElement^>(des);
                                 if (desFE2 != nullptr && desFE2->ActualWidth > 0 && desFE2->ActualHeight > 0) {
                                     Windows::Foundation::Numerics::float3 cp2; cp2.x = (float)desFE2->ActualWidth * 0.5f; cp2.y = (float)desFE2->ActualHeight * 0.5f; cp2.z = 0.0f;
                                     desVis->CenterPoint = cp2;
                                 }
                             }
                         }
                     } catch(...) {}
                 } else {
                     if (img != nullptr) SetElementScaleImmediate(img, isSelected ? kSelectedScale : kUnselectedScale);
                     if (des != nullptr) { SetElementScaleImmediate(des, isSelected ? kSelectedScale : kUnselectedScale); SetElementOpacityImmediate(des, isSelected ? 0.0f : kDesaturatorOpacityUnselected); }
                 }
             } catch(...) {}
        }
    } catch(...) {}
}

} // namespace moonlight_xbox_dx

