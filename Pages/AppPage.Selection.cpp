#include "pch.h"
#include "AppPage.xaml.h"
#include "AppPage.Helpers.h"
#include "Utils.hpp"
#include <cmath>
#include <functional>
#include <vector>

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Hosting;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace concurrency;

namespace moonlight_xbox_dx {

// ── AppPage::ApplyVisualsToContainer ─────────────────────────────────────────

void AppPage::ApplyVisualsToContainer(ListViewItem^ container, bool selected) {
    if (container == nullptr) return;
    try {
        UIElement^ des = nullptr, ^img = nullptr, ^nameTxt = nullptr;
        UIElement^ blur = nullptr, ^reflection = nullptr, ^play = nullptr, ^emboss = nullptr;
        FindElementChildren(container, des, img, nameTxt, blur, reflection, play, emboss);
        ApplySelectionVisuals(des, img, nameTxt, blur, reflection, play, emboss, selected, this->m_isGridLayout);

        double hp = (!m_isGridLayout && selected) ? kSelectedHPadding : 0.0;
        Windows::UI::Xaml::Thickness targetPadding;
        targetPadding.Left = hp; targetPadding.Top = 0.0; targetPadding.Right = hp; targetPadding.Bottom = 0.0;
        AnimateElementPadding(container, targetPadding, kAnimationDurationMs);
    } catch(...) {}
}

// ── AppPage::CenterSelectedItem ───────────────────────────────────────────────

void AppPage::CenterSelectedItem(int attempts, bool immediate) {
    auto lv = this->AppsGrid;
    if (lv == nullptr || lv->SelectedIndex < 0) return;

    auto item = lv->SelectedItem;
    if (item == nullptr) return;

    if (m_scrollViewer == nullptr) m_scrollViewer = FindScrollViewer(lv);
    auto sv = m_scrollViewer;
    if (sv == nullptr) return;

    auto container = dynamic_cast<ListViewItem^>(lv->ContainerFromItem(item));

    if (container == nullptr) {
        try { lv->ScrollIntoView(item); } catch(...) {}
        if (attempts > 0) {
            auto wt = WeakReference(this);
            this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                ref new DispatchedHandler([wt, attempts, immediate]() {
                    auto that = wt.Resolve<AppPage>();
                    if (that) that->CenterSelectedItem(attempts - 1, immediate);
                }));
        }
        return;
    }

    if (container->ActualWidth <= 0 || sv->ViewportWidth <= 0) {
        if (attempts > 0) {
            auto wt = WeakReference(this);
            this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                ref new DispatchedHandler([wt, attempts, immediate]() {
                    auto that = wt.Resolve<AppPage>();
                    if (that) that->CenterSelectedItem(attempts - 1, immediate);
                }));
        }
        return;
    }

    try {
        if (m_isGridLayout) {
            // Mark pending; OnScrollViewerViewChanged applies centering after the
            // ListView's own keyboard-nav scroll finishes (ViewChanged !IsIntermediate).
            m_gridCenterPending = true;
        } else {
            // Horizontal centering. TransformToVisual captures mid-animation layout: the
            // previously-selected item still has its full margin (kSelectedHPadding on each
            // side) when SelectionChanged fires, skewing pt.X. Instead compute the final
            // settled center directly: each preceding unselected item occupies nominalWidth
            // pixels, then the selected item adds kSelectedHPadding before its content.
            double nominalWidth = container->ActualWidth; // container not yet animated — unselected width
            double finalCenter  = lv->SelectedIndex * nominalWidth + kSelectedHPadding + nominalWidth / 2.0;

            double desired = finalCenter - sv->ViewportWidth / 2.0;
            if (sv->ScrollableWidth > 1.0 && desired > 0.0) {
                // Viewport scrolls; clear any panel translate first.
                try {
                    auto panel = dynamic_cast<FrameworkElement^>(lv->ItemsPanelRoot);
                    if (panel != nullptr) {
                        auto vis = ElementCompositionPreview::GetElementVisual(panel);
                        if (vis != nullptr) {
                            try { vis->StopAnimation("Offset.X"); } catch(...) {}
                            auto o = vis->Offset; o.x = 0.0f; vis->Offset = o;
                        }
                    }
                } catch(...) {}
                if (desired > sv->ScrollableWidth) desired = sv->ScrollableWidth;
                try { sv->ChangeView(desired, nullptr, nullptr, immediate); } catch(...) {}
            } else {
                // All items fit, or selected item is near the start (desired ≤ 0):
                // translate the panel so the item appears centered.
                float translateX = (float)(sv->ViewportWidth / 2.0 - finalCenter);
                try {
                    auto panel = dynamic_cast<FrameworkElement^>(lv->ItemsPanelRoot);
                    if (panel != nullptr) {
                        auto vis = ElementCompositionPreview::GetElementVisual(panel);
                        if (vis != nullptr) {
                            auto compositor = vis->Compositor;
                            auto anim = compositor->CreateScalarKeyFrameAnimation();
                            TimeSpan ts; ts.Duration = kAnimationDurationMs * 10000LL;
                            anim->Duration = ts;
                            anim->InsertKeyFrame(1.0f, translateX);
                            try { vis->StopAnimation("Offset.X"); } catch(...) {}
                            vis->StartAnimation("Offset.X", anim);
                        }
                    }
                } catch(...) {}
            }
            try {
                if (!m_suppressSelectionFocus) container->Focus(Windows::UI::Xaml::FocusState::Programmatic);
            } catch(...) {}
            if (immediate) m_initialCenteringScheduled = true;
        }
    } catch(...) {}
}

// ── AppPage::DoGridCentering ──────────────────────────────────────────────────

void AppPage::DoGridCentering() {
    m_gridCenterPending = false; // clear BEFORE ChangeView so the ViewChanged it fires won't re-enter
    auto lv = this->AppsGrid;
    if (lv == nullptr || lv->SelectedIndex < 0) return;
    if (m_scrollViewer == nullptr) m_scrollViewer = FindScrollViewer(lv);
    auto sv = m_scrollViewer;
    if (sv == nullptr || sv->ScrollableHeight <= 1.0) return;

    auto container = dynamic_cast<ListViewItem^>(lv->ContainerFromItem(lv->SelectedItem));
    if (container == nullptr || container->ActualHeight <= 0) return;

    try {
        // TransformToVisual gives position relative to lv's current viewport (scroll-adjusted).
        // Adding VerticalOffset converts to content/document coordinates, which is what
        // ChangeView expects — no need to manually account for panel margins or padding.
        auto transform = container->TransformToVisual(lv);
        auto pt = transform->TransformPoint(Windows::Foundation::Point(0, 0));
        double itemTopInContent = pt.Y + sv->VerticalOffset;
        double dY = itemTopInContent + container->ActualHeight / 2.0 - sv->ViewportHeight / 2.0;
        if (dY < 0) dY = 0;
        if (dY > sv->ScrollableHeight) dY = sv->ScrollableHeight;
        sv->ChangeView(nullptr, dY, nullptr, false);
    } catch(...) {}
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
    try {
        if (lv == nullptr) return;
        double listTarget = lv->ActualHeight * kAppsGridHeightFactor;
        float initScale = kUnselectedScale;

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

            try {
                auto des     = dynamic_cast<UIElement^>(FindChildByName(container, "Desaturator"));
                auto img     = dynamic_cast<UIElement^>(FindChildByName(container, "AppImageRect"));
                auto blur    = dynamic_cast<UIElement^>(FindChildByName(container, "AppImageBlurRect"));
                auto refl    = dynamic_cast<UIElement^>(FindChildByName(container, "AppImageReflectionRect"));
                auto nameTxt = dynamic_cast<UIElement^>(FindChildByName(container, "AppName"));

                bool isSelected = false;
                try {
                    if (this->AppsGrid != nullptr && this->AppsGrid->SelectedIndex >= 0) {
                        int idx = this->AppsGrid->IndexFromContainer(container);
                        if (idx == this->AppsGrid->SelectedIndex) isSelected = true;
                    }
                } catch(...) {}

                auto initVisual = [&](UIElement^ el, float scale, float opacity) {
                    if (el == nullptr) return;
                    auto vis = ElementCompositionPreview::GetElementVisual(el);
                    if (vis == nullptr) return;
                    try { vis->StopAnimation("Scale.X"); vis->StopAnimation("Scale.Y"); } catch(...) {}
                    if (m_compositionReady) {
                        AnimateElementScale(el, scale, kAnimationDurationMs);
                        if (opacity >= 0.0f) AnimateElementOpacity(el, opacity, kAnimationDurationMs);
                    } else {
                        Windows::Foundation::Numerics::float3 s; s.x = scale; s.y = s.x; s.z = 0.0f;
                        vis->Scale = s;
                        if (opacity >= 0.0f) vis->Opacity = opacity;
                    }
                    auto fe2 = dynamic_cast<FrameworkElement^>(el);
                    if (fe2 != nullptr && fe2->ActualWidth > 0 && fe2->ActualHeight > 0) {
                        Windows::Foundation::Numerics::float3 cp;
                        cp.x = (float)fe2->ActualWidth * 0.5f;
                        cp.y = (float)fe2->ActualHeight * 0.5f;
                        cp.z = 0.0f;
                        vis->CenterPoint = cp;
                    }
                };

                try { initVisual(img,  initScale, -1.0f); } catch(...) {}
                try { initVisual(blur, initScale, -1.0f); } catch(...) {}
                try { initVisual(refl, initScale, -1.0f); } catch(...) {}

                try {
                    auto playFE = FindChildByName(container, "Play");
                    initVisual(dynamic_cast<UIElement^>(playFE), initScale, -1.0f);
                } catch(...) {}

                if (des != nullptr) {
                    auto desVis = ElementCompositionPreview::GetElementVisual(des);
                    if (desVis != nullptr) {
                        try { desVis->StopAnimation("Scale.X"); desVis->StopAnimation("Scale.Y"); desVis->StopAnimation("Opacity"); } catch(...) {}
                        if (m_compositionReady) {
                            AnimateElementScale(dynamic_cast<UIElement^>(des), initScale, kAnimationDurationMs);
                            AnimateElementOpacity(des, kDesaturatorOpacityUnselected, kAnimationDurationMs);
                        } else {
                            Windows::Foundation::Numerics::float3 s2; s2.x = initScale; s2.y = s2.x; s2.z = 0.0f;
                            desVis->Scale = s2;
                            desVis->Opacity = kDesaturatorOpacityUnselected;
                        }
                        if (nameTxt != nullptr) {
                            if (m_compositionReady) AnimateElementOpacity(nameTxt, isSelected ? 1.0f : 0.0f, kAnimationDurationMs);
                            else SetElementOpacityImmediate(nameTxt, isSelected ? 1.0f : 0.0f);
                        }
                        auto desFE2 = dynamic_cast<FrameworkElement^>(des);
                        if (desFE2 != nullptr && desFE2->ActualWidth > 0 && desFE2->ActualHeight > 0) {
                            Windows::Foundation::Numerics::float3 cp2;
                            cp2.x = (float)desFE2->ActualWidth * 0.5f; cp2.y = (float)desFE2->ActualHeight * 0.5f; cp2.z = 0.0f;
                            desVis->CenterPoint = cp2;
                        }
                        // Emboss
                        try {
                            auto emboss = dynamic_cast<UIElement^>(FindChildByName(container, "Emboss"));
                            if (emboss != nullptr) {
                                auto embossVis = ElementCompositionPreview::GetElementVisual(emboss);
                                if (embossVis != nullptr) {
                                    try { embossVis->StopAnimation("Scale.X"); embossVis->StopAnimation("Scale.Y"); embossVis->StopAnimation("Opacity"); } catch(...) {}
                                    if (m_compositionReady) {
                                        AnimateElementScale(emboss, initScale, kAnimationDurationMs);
                                        AnimateElementOpacity(emboss, 0.0f, kAnimationDurationMs);
                                    } else {
                                        embossVis->Opacity = 0.0f;
                                        Windows::Foundation::Numerics::float3 s3; s3.x = initScale; s3.y = s3.x; s3.z = 0.0f;
                                        embossVis->Scale = s3;
                                    }
                                    auto embossFE2 = dynamic_cast<FrameworkElement^>(emboss);
                                    if (embossFE2 != nullptr && embossFE2->ActualWidth > 0 && embossFE2->ActualHeight > 0) {
                                        Windows::Foundation::Numerics::float3 cp3;
                                        cp3.x = (float)embossFE2->ActualWidth * 0.5f; cp3.y = (float)embossFE2->ActualHeight * 0.5f; cp3.z = 0.0f;
                                        embossVis->CenterPoint = cp3;
                                    }
                                }
                            }
                        } catch(...) {}
                    }
                }
            } catch(...) {}
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
            UIElement^ des = nullptr, ^img = nullptr, ^nameTxt = nullptr;
            UIElement^ blur = nullptr, ^refl = nullptr, ^play = nullptr, ^emboss = nullptr;
            FindElementChildren(container, des, img, nameTxt, blur, refl, play, emboss);
            if (img)    SetElementScaleImmediate(img,    kUnselectedScale);
            if (des)  { SetElementScaleImmediate(des,    kUnselectedScale);
                        SetElementOpacityImmediate(des,  kDesaturatorOpacityUnselected); }
            if (play)   SetElementScaleImmediate(play,   kUnselectedScale);
            if (emboss){ SetElementScaleImmediate(emboss, kUnselectedScale);
                         SetElementOpacityImmediate(emboss, 0.0f); }
            if (blur)  { SetElementOpacityImmediate(blur, 0.0f);
                         blur->Visibility = Windows::UI::Xaml::Visibility::Collapsed; }
            if (refl)    refl->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
            if (nameTxt) SetElementOpacityImmediate(nameTxt, 0.0f);
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
    try {
        double listTarget = lv != nullptr ? lv->ActualHeight * kAppsGridHeightFactor : 300.0;

        std::function<DependencyObject^(DependencyObject^)> findAspect =
            [&](DependencyObject^ parent) -> DependencyObject^ {
                if (parent == nullptr) return nullptr;
                int count = VisualTreeHelper::GetChildrenCount(parent);
                for (int j = 0; j < count; ++j) {
                    auto child = VisualTreeHelper::GetChild(parent, j);
                    auto fe    = dynamic_cast<FrameworkElement^>(child);
                    if (fe != nullptr && fe->GetType()->FullName == "moonlight_xbox_dx.AspectRatioBox")
                        return child;
                    auto rec = findAspect(child);
                    if (rec != nullptr) return rec;
                }
                return nullptr;
            };

        auto found = findAspect(container);
        if (found != nullptr) {
            auto fe = dynamic_cast<FrameworkElement^>(found);
            if (fe != nullptr) {
                double desiredH = listTarget;
                if (m_isGridLayout) {
                    double itemW = 217.0; // matches XAML ItemsWrapGrid ItemWidth
                    try {
                        auto panel = dynamic_cast<ItemsWrapGrid^>(lv->ItemsPanelRoot);
                        if (panel != nullptr && panel->ItemWidth > 0) itemW = panel->ItemWidth;
                    } catch(...) {}
                    constexpr double ratio = 0.65;
                    double h = itemW / ratio;
                    if (h > 0.0 && h < listTarget) desiredH = h;
                }
                double prev = fe->Height;
                if (std::isnan(prev) || std::fabs(prev - desiredH) > 1.0) {
                    fe->Height = desiredH;
                    fe->InvalidateMeasure();
                }
            }
        }
    } catch(...) {}

    // Apply unselected visual state (no animations for fresh containers).
    try {
        UIElement^ des = nullptr, ^img = nullptr, ^nameTxt = nullptr;
        UIElement^ blur = nullptr, ^refl = nullptr, ^play = nullptr, ^emboss = nullptr;
        FindElementChildren(container, des, img, nameTxt, blur, refl, play, emboss);
        if (img)    SetElementScaleImmediate(img,    kUnselectedScale);
        if (play)   SetElementScaleImmediate(play,   kUnselectedScale);
        if (des)  { SetElementScaleImmediate(des,    kUnselectedScale);
                    SetElementOpacityImmediate(des,  kDesaturatorOpacityUnselected); }
        if (emboss){ SetElementScaleImmediate(emboss, kUnselectedScale);
                     SetElementOpacityImmediate(emboss, 0.0f); }
        if (blur)  { SetElementOpacityImmediate(blur, 0.0f);
                     blur->Visibility = Windows::UI::Xaml::Visibility::Collapsed; }
        if (refl)    refl->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
        if (nameTxt) SetElementOpacityImmediate(nameTxt, 0.0f);
        Windows::UI::Xaml::Thickness zero;
        zero.Left = zero.Top = zero.Right = zero.Bottom = 0.0;
        container->Margin = zero;
    } catch(...) {}

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
    this->CenterSelectedItem(4, true);

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
            SetElementOpacityImmediate(this->SelectedAppBox,  0.0f);
            SetElementOpacityImmediate(this->SelectedAppText, 0.0f);
            if (res != nullptr) {
                auto sb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(
                    res->Lookup(ref new Platform::String(L"ShowSelectedAppStoryboard")));
                if (sb != nullptr) sb->Begin();
            }
        } else if (res != nullptr) {
            auto sb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(
                res->Lookup(ref new Platform::String(L"HideSelectedAppStoryboard")));
            if (sb != nullptr) sb->Begin();
            else {
                if (m_compositionReady) {
                    AnimateElementOpacity(this->SelectedAppBox,  0.0f, kAnimationDurationMs);
                    AnimateElementOpacity(this->SelectedAppText, 0.0f, kAnimationDurationMs);
                } else {
                    SetElementOpacityImmediate(this->SelectedAppBox,  0.0f);
                    SetElementOpacityImmediate(this->SelectedAppText, 0.0f);
                }
            }
        }
    } catch(...) {}
}

} // namespace moonlight_xbox_dx
