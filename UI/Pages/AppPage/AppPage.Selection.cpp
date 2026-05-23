#include "pch.h"
#include "AppPage.xaml.h"
#include "AppPage.Helpers.h"
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
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace concurrency;

namespace moonlight_xbox_dx {

namespace {

static double ParseDurationStringToMs(Platform::String^ durationValue) {
    if (durationValue == nullptr || durationValue->IsEmpty()) return 250.0;

    std::wstring text(durationValue->Data());
    std::wstringstream ss(text);
    std::wstring segment;
    std::vector<double> parts;

    while (std::getline(ss, segment, L':')) {
        if (segment.empty()) return 250.0;
        try {
            size_t idx = 0;
            double value = std::stod(segment, &idx);
            if (idx != segment.size()) return 250.0;
            parts.push_back(value);
        } catch (...) {
            return 250.0;
        }
    }

    double totalSeconds = 0.0;
    if (parts.size() == 3) {
        totalSeconds = (parts[0] * 3600.0) + (parts[1] * 60.0) + parts[2];
    } else if (parts.size() == 2) {
        totalSeconds = (parts[0] * 60.0) + parts[1];
    } else if (parts.size() == 1) {
        totalSeconds = parts[0];
    } else {
        return 250.0;
    }

    if (!std::isfinite(totalSeconds) || totalSeconds <= 0.0) return 250.0;
    return totalSeconds * 1000.0;
}

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

    return ParseDurationStringToMs(durationValue);
}

} // namespace

// ── AppPage::ApplyVisualsToContainer ─────────────────────────────────────────

void AppPage::ApplyVisualsToContainer(ListViewItem^ container, bool selected) {
    if (container == nullptr) return;
    try {
        double hp = (!m_isGridLayout && selected) ? kSelectedHPadding : 0.0;
        Windows::UI::Xaml::Thickness targetPadding;
        targetPadding.Left = hp; targetPadding.Top = 0.0; targetPadding.Right = hp; targetPadding.Bottom = 0.0;
        //AnimateElementPadding(container, targetPadding, kAnimationDurationMs);
    } catch(...) {}
}

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

// ── AppPage::EnsureRealizedContainersInitialized ──────────────────────────────

void AppPage::EnsureRealizedContainersInitialized(ListView^ lv) {
	return;
    try {
        if (lv == nullptr) return;
        double listTarget = lv->ActualHeight * kAppsGridHeightFactor;

        for (unsigned int i = 0; i < lv->Items->Size; ++i) {
            auto container = dynamic_cast<ListViewItem^>(lv->ContainerFromIndex(i));
            if (container == nullptr) continue;
            container->InvalidateMeasure();
            container->UpdateLayout();

            // Set AspectRatioBox height
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
            if (found != nullptr) {
                auto fe = dynamic_cast<FrameworkElement^>(found);
                if (fe != nullptr) {
                    double prev = fe->Height;
                    if (std::isnan(prev) || std::fabs(prev - listTarget) > 1.0) {
                        fe->Height = listTarget;
                        fe->InvalidateMeasure();
                    }
                }
            }

            // XAML item container styles now own the selected/unselected scale.
            // Keep the realized container tree stable, but do not touch scale here.
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

    // Set AspectRatioBox height so the item has a measurable size.
    //try {
    //    double listTarget = lv != nullptr ? lv->ActualHeight * kAppsGridHeightFactor : 300.0;

    //    std::function<DependencyObject^(DependencyObject^)> findAspect =
    //        [&](DependencyObject^ parent) -> DependencyObject^ {
    //            if (parent == nullptr) return nullptr;
    //            int count = VisualTreeHelper::GetChildrenCount(parent);
    //            for (int j = 0; j < count; ++j) {
    //                auto child = VisualTreeHelper::GetChild(parent, j);
    //                auto fe    = dynamic_cast<FrameworkElement^>(child);
    //                if (fe != nullptr && fe->GetType()->FullName == "moonlight_xbox_dx.AspectRatioBox")
    //                    return child;
    //                auto rec = findAspect(child);
    //                if (rec != nullptr) return rec;
    //            }
    //            return nullptr;
    //        };

    //    auto found = findAspect(container);
    //    if (found != nullptr) {
    //        auto fe = dynamic_cast<FrameworkElement^>(found);
    //        if (fe != nullptr) {
    //            double desiredH = listTarget;
    //            if (m_isGridLayout) {
    //                double itemW = 217.0; // matches XAML ItemsWrapGrid ItemWidth
    //                try {
    //                    auto panel = dynamic_cast<ItemsWrapGrid^>(lv->ItemsPanelRoot);
    //                    if (panel != nullptr && panel->ItemWidth > 0) itemW = panel->ItemWidth;
    //                } catch(...) {}
    //                constexpr double ratio = 0.65;
    //                double h = itemW / ratio;
    //                if (h > 0.0 && h < listTarget) desiredH = h;
    //            }
    //            double prev = fe->Height;
    //            if (std::isnan(prev) || std::fabs(prev - desiredH) > 1.0) {
    //                fe->Height = desiredH;
    //                fe->InvalidateMeasure();
    //            }
    //        }
    //    }
    //} catch(...) {}

    // Apply unselected visual state (no animations for fresh containers).
    //try {
    //    Windows::UI::Xaml::Thickness zero;
    //    zero.Left = zero.Top = zero.Right = zero.Bottom = 0.0;
    //    container->Margin = zero;
    //    ApplyVisualsToContainer(container, false);
    //} catch(...) {}

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
            try { ApplyVisualsToContainer(container, true); } catch(...) {}
            auto item = dynamic_cast<MoonlightApp^>(args->Item);
            if (item != nullptr) {
                if (item->BlurredImage == nullptr)
                    try { BlurAppImage(item); } catch(...) {}
                else
                    try { FadeInRealizedBlurAndReflectionIfSelected(item, item->BlurredImage); } catch(...) {}
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

    //try {
    //    auto prevApp = dynamic_cast<MoonlightApp^>(prevItem);
    //    if (prevApp != nullptr) prevApp->IsSelected = false;
    //    auto selAppState = dynamic_cast<MoonlightApp^>(item);
    //    if (selAppState != nullptr) {
    //        selAppState->IsGridLayout = m_isGridLayout;
    //        selAppState->IsSelected = true;
    //    }
    //} catch(...) {}

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

    EnsureRealizedContainersInitialized(lv);

    // Blur
    try {
        auto selApp = dynamic_cast<MoonlightApp^>(item);
        if (selApp != nullptr) {
            if (selApp->BlurredImage == nullptr) BlurAppImage(selApp);
            else FadeInRealizedBlurAndReflectionIfSelected(selApp, selApp->BlurredImage);
        }
    } catch(...) {}

    // Animate containers
    try {
        if (container     != nullptr) this->ApplyVisualsToContainer(container, true);
        if (prevContainer != nullptr) this->ApplyVisualsToContainer(prevContainer, false);
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
            //SetElementOpacityImmediate(this->SelectedAppBox,  0.0f);
            //SetElementOpacityImmediate(this->SelectedAppText, 0.0f);
            if (res != nullptr) {
                auto sb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(
                    res->Lookup(ref new Platform::String(L"ShowSelectedAppStoryboard")));
                if (sb != nullptr) sb->Begin();
            }
        } else if (res != nullptr) {
            auto sb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(
                res->Lookup(ref new Platform::String(L"HideSelectedAppStoryboard")));
            //if (sb != nullptr) sb->Begin();
            //else {
            //    if (m_compositionReady) {
            //        AnimateElementOpacity(this->SelectedAppBox,  0.0f, kAnimationDurationMs);
            //        AnimateElementOpacity(this->SelectedAppText, 0.0f, kAnimationDurationMs);
            //    } else {
            //        SetElementOpacityImmediate(this->SelectedAppBox,  0.0f);
            //        SetElementOpacityImmediate(this->SelectedAppText, 0.0f);
            //    }
            //}
        }
    } catch(...) {}

    try { CenterSelectedItem(4, false); } catch(...) {}
}

} // namespace moonlight_xbox_dx
