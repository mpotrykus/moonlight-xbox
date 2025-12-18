#include "pch.h"
#include "AppPage.Xaml.h"
#include "Controls\SlidingMenu.xaml.h"
#include "Common\ModalDialog.xaml.h"
#include "HostSettingsPage.xaml.h"
#include "State\MoonlightClient.h"
#include "StreamPage.xaml.h"
#include "Utils.hpp"
#include "Common\XamlHelper.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <wrl.h>
#include <ppltasks.h>
#include "Common/ImageHelpers.h"
#include <thread>
#include <chrono>

using namespace Microsoft::WRL;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Graphics::Display;
using namespace Windows::ApplicationModel::Core;
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
using namespace Windows::Storage::Streams;
using namespace Windows::Storage;

namespace moonlight_xbox_dx {

// OPACITY
static constexpr float kDesaturatorOpacityUnselected = 0.8f;
static constexpr float kEmbossOpacitySelected = 0.2f;
static constexpr float kBlurOpacitySelected = 0.4f;
static constexpr float kReflectionOpacitySelected = 0.2f;

// SCALE
static constexpr float kSelectedScale = 1.0f;
static constexpr float kUnselectedScale = 0.8f;

static constexpr double kAppsGridHeightFactor = 0.7;
static constexpr int kAnimationDurationMs = 150;

static constexpr float kBlurAmount = 8.0f;

// Blur amount depends on layout mode: smaller blur in grid, larger in list
static constexpr float kBlurAmountGrid = 8.0f;
static constexpr float kBlurAmountList = 16.0f;
static inline float GetBlurAmountFor(bool isGridLayout) {
    return isGridLayout ? kBlurAmountGrid : kBlurAmountList;
}

// Forward declare helper used below
static ScrollViewer ^ FindScrollViewer(DependencyObject ^ parent);
static FrameworkElement ^ FindChildByName(DependencyObject ^ parent, Platform::String ^ name);
static void AnimateElementOpacity(UIElement ^ element, float targetOpacity, int durationMs);
static void AnimateElementWidth(FrameworkElement ^ element, double targetWidth, int durationMs);
static void SetElementOpacityImmediate(UIElement ^ element, float value);
static void SetElementScaleImmediate(UIElement ^ element, float scale);
static void AnimateElementScale(UIElement ^ element, float targetScale, int durationMs);
static FrameworkElement^ FindChildByName(DependencyObject^ parent, String^ name);

static concurrency::task<SoftwareBitmap^> CaptureXamlElementAsync(FrameworkElement^ element) {
    concurrency::task_completion_event<SoftwareBitmap^> tce;
    if (element == nullptr) {
        tce.set(nullptr);
        return concurrency::create_task(tce);
    }

    auto dispatched = ref new DispatchedHandler([element, tce]() mutable {
        try {
            auto rtb = ref new RenderTargetBitmap();
            create_task(rtb->RenderAsync(element)).then([rtb]() {
                return rtb->GetPixelsAsync();
            }).then([rtb, tce](concurrency::task<IBuffer^> prev) {
                try {
                    auto pixels = prev.get();
                    if (pixels == nullptr) {
                        moonlight_xbox_dx::Utils::Log("CaptureXamlElementAsync: GetPixelsAsync returned null pixels\n");
                        tce.set(nullptr);
                        return;
                    }
                    unsigned int w = rtb->PixelWidth;
                    unsigned int h = rtb->PixelHeight;
                    if (w == 0 || h == 0) {
                        moonlight_xbox_dx::Utils::Log("CaptureXamlElementAsync: RenderTargetBitmap reported zero width/height\n");
                        tce.set(nullptr);
                        return;
                    }
                    auto sb = SoftwareBitmap::CreateCopyFromBuffer(pixels, BitmapPixelFormat::Bgra8, w, h, BitmapAlphaMode::Premultiplied);
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
        CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, dispatched);
    } catch(...) {
        // If dispatcher invocation fails, return null
        tce.set(nullptr);
    }

    return concurrency::create_task(tce);
}

concurrency::task<IRandomAccessStream ^> AppPage::ApplyBlur(MoonlightApp ^ app, float blurDip) {
    if (app == nullptr) return concurrency::task_from_result<IRandomAccessStream ^>(nullptr);

	Platform::String ^ path = nullptr;
	try {
		path = app->ImagePath;
	} catch (...) {
		path = nullptr;
	}




    /*----MASK----*/ 


    return concurrency::create_task(ImageHelpers::LoadSoftwareBitmapFromUriOrPathAsync(path)).then([this, app, blurDip](SoftwareBitmap^ softwareBitmap) -> concurrency::task<IRandomAccessStream^> {
        if (softwareBitmap == nullptr) return concurrency::task_from_result<IRandomAccessStream ^>(nullptr);



		/*----CAPTURE DPI AND ORIGINAL IMAGE DIMENSIONS----*/

        unsigned int ui_targetW = 0, ui_targetH = 0;
        double ui_dpi = 96.0;
        try {
            auto fe_for_size = dynamic_cast<FrameworkElement ^>(this->FindName("AppImageRect"));


            /*----IMPORTANT FOR MASK SIZING----*/
            if (fe_for_size == nullptr) {
                if (this->AppsGrid != nullptr) {
                    auto container = dynamic_cast<ListViewItem ^>(this->AppsGrid->ContainerFromItem(app));
                    if (container != nullptr) {
                        auto found = FindChildByName(container, ref new Platform::String(L"AppImageRect"));
                        if (found == nullptr) found = FindChildByName(container, ref new Platform::String(L"AppImageBlurRect"));
                        fe_for_size = dynamic_cast<FrameworkElement ^>(found);
                    }
                }
            }

            if (fe_for_size != nullptr) {
                double aw = 0.0, ah = 0.0;
                try { aw = fe_for_size->ActualWidth; } catch(...) { aw = 0.0; }
                try { ah = fe_for_size->ActualHeight; } catch(...) { ah = 0.0; }
                    // If ActualWidth/Height are zero, try several fallback measurements inside the realized container
                    if ((aw <= 0 || ah <= 0) && this->AppsGrid != nullptr) {
                        try {
                            auto container = dynamic_cast<ListViewItem ^>(this->AppsGrid->ContainerFromItem(app));
                            if (container != nullptr) {
                                // Try AppAspectRatioBox inside the container
                                auto foundAR = FindChildByName(container, ref new Platform::String(L"AppAspectRatioBox"));
                                auto feAR = dynamic_cast<FrameworkElement^>(foundAR);
                                if (feAR != nullptr) {
                                    try { if (aw <= 0) aw = feAR->ActualWidth; } catch(...) {}
                                    try { if (ah <= 0) ah = feAR->ActualHeight; } catch(...) {}
                                }

                                // Try ItemGrid
                                if ((aw <= 0 || ah <= 0)) {
                                    auto foundIG = FindChildByName(container, ref new Platform::String(L"ItemGrid"));
                                    auto feIG = dynamic_cast<FrameworkElement^>(foundIG);
                                    if (feIG != nullptr) {
                                        try { if (aw <= 0) aw = feIG->ActualWidth; } catch(...) {}
                                        try { if (ah <= 0) ah = feIG->ActualHeight; } catch(...) {}
                                    }
                                }

                                // Try the blur rect inside the container
                                if ((aw <= 0 || ah <= 0)) {
                                    auto foundBlur = FindChildByName(container, ref new Platform::String(L"AppImageBlurRect"));
                                    auto feB = dynamic_cast<FrameworkElement^>(foundBlur);
                                    if (feB != nullptr) {
                                        try { if (aw <= 0) aw = feB->ActualWidth; } catch(...) {}
                                        try { if (ah <= 0) ah = feB->ActualHeight; } catch(...) {}
                                    }
                                }

                                // Fall back to the container's ActualWidth
                                if (aw <= 0) {
                                    try { aw = container->ActualWidth; } catch(...) { aw = 0.0; }
                                }
                            }

                            // As a last resort, use the AppsGrid width (whole viewport)
                            if (aw <= 0) {
                                try { aw = this->AppsGrid->ActualWidth; } catch(...) { aw = 0.0; }
                            }
                        } catch(...) {}
                    }

                    if (aw > 0 && ah > 0) {
                        try {
                            auto di = DisplayInformation::GetForCurrentView();
                            double dpi = di != nullptr ? di->LogicalDpi : 96.0;
                            ui_dpi = dpi;
                            ui_targetW = (unsigned int)std::max(1u, (unsigned int)std::round(aw * dpi / 96.0));
                            ui_targetH = (unsigned int)std::max(1u, (unsigned int)std::round(ah * dpi / 96.0));
                            moonlight_xbox_dx::Utils::Logf("ApplyBlur(UI): captured display pixel target=%u x %u from ActualWidth/Height (dpi=%.2f)\n", ui_targetW, ui_targetH, ui_dpi);
                        } catch(...) { ui_targetW = ui_targetH = 0; ui_dpi = 96.0; }
                    }
            }
        } catch(...) { ui_targetW = ui_targetH = 0; }

        

        /*----PREPARE IMAGE CAPTURE----*/

        FrameworkElement ^ imageFe = nullptr;
		try {
            imageFe = dynamic_cast<FrameworkElement ^>(this->FindName("AppImageRect"));
		} catch (...) {
			imageFe = nullptr;
		}

		if (imageFe == nullptr) {
			try {
				if (this->AppsGrid != nullptr) {
					auto container = dynamic_cast<ListViewItem ^>(this->AppsGrid->ContainerFromItem(app));
					if (container != nullptr) {
						try {
							auto found = FindChildByName(container, ref new Platform::String(L"AppImageRect"));
							if (found == nullptr) found = FindChildByName(container, ref new Platform::String(L"AppImageBlurRect"));
                            imageFe = dynamic_cast<FrameworkElement ^>(found);
						} catch (...) {
							imageFe = nullptr;
						}
					}
				}
			} catch (...) {
				imageFe = nullptr;
			}
		}

        concurrency::task<SoftwareBitmap ^> imageCaptureTask = imageFe != nullptr ? CaptureXamlElementAsync(imageFe) : concurrency::task_from_result<SoftwareBitmap ^>(nullptr);

        

        /*----PREPARE MASK CAPTURE----*/

		SoftwareBitmap ^ nullMask = nullptr;
        FrameworkElement ^ maskFe = nullptr;
		try {
                maskFe = dynamic_cast<FrameworkElement ^>(this->FindName("AppImageBlurRect"));
		} catch (...) {
			maskFe = nullptr;
		}
		if (maskFe == nullptr) {
			try {
				if (this->AppsGrid != nullptr) {
					auto container = dynamic_cast<ListViewItem ^>(this->AppsGrid->ContainerFromItem(app));
					if (container != nullptr) {
						try {
							auto found = FindChildByName(container, ref new Platform::String(L"AppImageBlurRect"));
                            maskFe = dynamic_cast<FrameworkElement ^>(found);
						} catch (...) {
							maskFe = nullptr;
						}
					}
				}
			} catch (...) {
				maskFe = nullptr;
			}
		}

		concurrency::task<SoftwareBitmap ^> maskCaptureTask = maskFe != nullptr ? CaptureXamlElementAsync(maskFe) : concurrency::task_from_result<SoftwareBitmap ^>(nullMask);


        /*----RASTERIZE----*/ 

        return imageCaptureTask.then([this, app, softwareBitmap, maskCaptureTask, ui_targetW, ui_targetH, ui_dpi, blurDip, imageFe, maskFe](SoftwareBitmap ^ capturedImage) mutable -> concurrency::task<IRandomAccessStream ^> {
            return maskCaptureTask.then([this, app, softwareBitmap, ui_targetW, ui_targetH, ui_dpi, blurDip, imageFe, maskFe](SoftwareBitmap ^ maskFromXaml) mutable -> concurrency::task<IRandomAccessStream ^> {
                // Apply rounded-corner mask composition before rasterizing.
                unsigned int targetW = ui_targetW != 0 ? ui_targetW : softwareBitmap->PixelWidth;
                unsigned int targetH = ui_targetH != 0 ? ui_targetH : softwareBitmap->PixelHeight;
                // Compute corner radius (use 8 DIP as baseline, scalable by DPI). This can be adjusted.
                unsigned int cornerRadiusPx = 0;
                try { cornerRadiusPx = (unsigned int)std::round(8.0 * ui_dpi / 96.0); } catch(...) { cornerRadiusPx = 16; }

                try { moonlight_xbox_dx::Utils::Logf("ApplyBlur: maskFromXaml=%p pixelSize=%u x %u target=%u x %u dpi=%.2f radiusPx=%u\n", maskFromXaml, maskFromXaml ? maskFromXaml->PixelWidth : 0, maskFromXaml ? maskFromXaml->PixelHeight : 0, ui_targetW, ui_targetH, ui_dpi, cornerRadiusPx); } catch(...) {}

                // We clipped the XAML element prior to capture, so the captured mask already has rounded corners.
                try { if (maskFromXaml) moonlight_xbox_dx::Utils::Logf("ApplyBlur: captured mask=%p size=%u x %u\n", maskFromXaml, maskFromXaml->PixelWidth, maskFromXaml->PixelHeight); else moonlight_xbox_dx::Utils::Log("ApplyBlur: captured mask is null\n"); } catch(...) {}

                // No runtime clip restore needed; rounded corners handled in XAML

                return ImageHelpers::CreateMaskedBlurredPngStreamAsync(softwareBitmap, maskFromXaml, targetW, targetH, ui_dpi, blurDip);
			});
		});
	});
}

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
                Point pt{0,0};
                try {
                    auto trans = container->TransformToVisual(sv);
                    if (trans != nullptr) pt = trans->TransformPoint(Point{ 0, 0 });
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
                                    Point ptLU{0,0};
                                    try {
                                        auto transLU = container->TransformToVisual(sv);
                                        if (transLU != nullptr) ptLU = transLU->TransformPoint(Point{ 0, 0 });
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
                                    auto tt = dynamic_cast<TranslateTransform^>(that->m_itemsPanel->RenderTransform);
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
                                Point pt2{0,0};
                                try {
                                    auto trans2 = containerRef->TransformToVisual(svRef);
                                    if (trans2 != nullptr) pt2 = trans2->TransformPoint(Point{ 0, 0 });
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
        // prevImageClip/prevMaskClip removed; rounded corners handled in XAML

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

void AppPage::OnSampleActionClicked() {
    try {
        Utils::Log("OnSampleActionClicked invoked\n");
        if (this->SelectedAppText != nullptr) {
            this->SelectedAppText->Text = ref new Platform::String(L"Sample Action triggered");
            auto sb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(this->Resources->Lookup(ref new Platform::String(L"ShowSelectedAppStoryboard")));
            if (sb != nullptr) sb->Begin();
        }
    } catch(...) {}
}

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

static void FindElementChildren(DependencyObject^ container, UIElement^& outDesaturator, UIElement^& outImage, UIElement^& outName, UIElement^& outBlur, UIElement^& outReflection, UIElement^& outEmboss) {
    outDesaturator = nullptr; outImage = nullptr; outName = nullptr; outBlur = nullptr; outReflection = nullptr; outEmboss = nullptr;
    if (container == nullptr) return;
    try {
        outDesaturator = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"Desaturator")));
        outImage = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppImageRect")));
        outName = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppName")));
        outBlur = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppImageBlurRect")));
        outReflection = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppImageReflectionRect")));
        outEmboss = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"Emboss")));
    } catch(...) { outDesaturator = nullptr; outImage = nullptr; outName = nullptr; outBlur = nullptr; outEmboss = nullptr; }
}

static void ApplySelectionVisuals(UIElement^ des, UIElement^ img, UIElement^ nameTxt, UIElement^ blur, UIElement^ reflection, UIElement^ emboss, bool selected, bool isGridLayout) {

    try {

        if (img != nullptr) {
            AnimateElementScale(img, selected ? kSelectedScale : kUnselectedScale, kAnimationDurationMs);
        }

        if (des != nullptr) {
            AnimateElementScale(des, selected ? kSelectedScale : kUnselectedScale, kAnimationDurationMs);
            AnimateElementOpacity(des, selected ? 0.0f : kDesaturatorOpacityUnselected, kAnimationDurationMs);
        }

        if (emboss != nullptr) {
            AnimateElementScale(emboss, selected ? kSelectedScale : kUnselectedScale, kAnimationDurationMs);
			AnimateElementOpacity(emboss, selected ? kEmbossOpacitySelected : 0.0f, kAnimationDurationMs);
        }

        if (blur != nullptr) {
			if (selected) {
				blur->Visibility = Windows::UI::Xaml::Visibility::Visible;
				AnimateElementScale(blur, kSelectedScale, kAnimationDurationMs);
				AnimateElementOpacity(blur, kBlurOpacitySelected, kAnimationDurationMs);
			} else {
				blur->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
			}
        }

        if (reflection != nullptr) {
			if (selected && !isGridLayout) {
				reflection->Visibility = Windows::UI::Xaml::Visibility::Visible;
				AnimateElementScale(reflection, kSelectedScale, kAnimationDurationMs);
				AnimateElementOpacity(reflection, kReflectionOpacitySelected, kAnimationDurationMs);
			} else {
                reflection->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
            }
        }

        if (nameTxt != nullptr) {
            AnimateElementOpacity(nameTxt, selected ? 1.0f : 0.0f, kAnimationDurationMs);
        }

    } catch(...) {}
}

void AppPage::ApplyVisualsToContainer(ListViewItem^ container, bool selected) {
    if (container == nullptr) return;
    try {
        UIElement^ des = nullptr; 
        UIElement^ img = nullptr; 
        UIElement^ nameTxt = nullptr; 
        UIElement^ blur = nullptr; 
        UIElement^ reflection = nullptr;
        UIElement^ emboss = nullptr;

        FindElementChildren(container, des, img, nameTxt, blur, reflection, emboss);
        ApplySelectionVisuals(des, img, nameTxt, blur, reflection, emboss, selected, this->m_isGridLayout);
    } catch(...) {}
}

void AppPage::FadeInRealizedBlurAndReflectionIfSelected(MoonlightApp^ app, BitmapImage^ img) {
    try {
        if (app == nullptr || img == nullptr) return;
        // Only act if this app is currently selected
        try {
            if (this->AppsGrid == nullptr) return;
            auto sel = this->AppsGrid->SelectedItem;
            if (sel == nullptr) return;
            auto selApp = dynamic_cast<MoonlightApp^>(sel);
            if (selApp == nullptr) return;
            if (selApp != app) return; // not the selected app
        } catch(...) { return; }

        // Find realized container and adjust elements
        try {
            auto container = dynamic_cast<ListViewItem ^>(this->AppsGrid->ContainerFromItem(app));
            if (container == nullptr) return;

            // Determine target width similar to existing logic
            double targetWidth = 0.0;
            auto itemGridFE = dynamic_cast<Windows::UI::Xaml::FrameworkElement ^>(FindChildByName(container, ref new Platform::String(L"ItemGrid")));
            try {
                if (itemGridFE != nullptr) { try { targetWidth = itemGridFE->ActualWidth; } catch(...) { targetWidth = 0.0; } }
                if (targetWidth <= 0.0) { try { targetWidth = container->ActualWidth; } catch(...) { targetWidth = 0.0; } }
                if (targetWidth <= 0.0 && this->AppsGrid != nullptr) { try { targetWidth = this->AppsGrid->ActualWidth; } catch(...) { targetWidth = 0.0; } }
            } catch(...) { targetWidth = 0.0; }

            double targetHeight = 0.0;
            try {
                if (itemGridFE != nullptr) { try { targetHeight = itemGridFE->ActualHeight; } catch(...) { targetHeight = 0.0; } }
                if (targetHeight <= 0.0) { try { targetHeight = container->ActualHeight; } catch(...) { targetHeight = 0.0; } }
                if (targetHeight <= 0.0 && this->AppsGrid != nullptr) { try { targetHeight = this->AppsGrid->ActualHeight; } catch(...) { targetHeight = 0.0; } }
            } catch(...) { targetHeight = 0.0; }

            // Set widths and fade-in when elements are present
            try {
                int tarWidth = 0;

                auto found = FindChildByName(container, ref new Platform::String(L"AppImageBlurRect"));
                auto blurFE = dynamic_cast<Windows::UI::Xaml::FrameworkElement ^>(found);
                if (blurFE != nullptr && img->PixelHeight > 0) {
                    try {
                        float blurAmountLocal = GetBlurAmountFor(this->m_isGridLayout);
                        tarWidth = targetWidth + (int)(blurAmountLocal * 2);
                    } catch (...) {
                        tarWidth = 0;
                    }

                    if (tarWidth <= 0 && targetWidth > 0) tarWidth = (int)std::round(targetWidth);

                    if (tarWidth > 0) {
						
                        blurFE->Opacity = 0.0;
						
                        double displayW = 0.0;
						try {
							    double dpi = 96.0;
							try {
								auto di = DisplayInformation::GetForCurrentView();
								if (di != nullptr) dpi = di->LogicalDpi;
							} catch (...) {
							}
							 bool usedBlurImage = false;
							if (img != nullptr) {
								try {
									unsigned int bpx = img->PixelWidth;
									if (bpx > 0) { 
										displayW = (double)bpx * 96.0 / dpi;
										usedBlurImage = true;
										
									}
									
								} catch (...) {
									usedBlurImage = false;
								}
								
							}
							  if (!usedBlurImage) {
								try {
									auto imgRectFE = dynamic_cast<FrameworkElement ^>(FindChildByName(container, ref new Platform::String(L"AppImageRect")));
									if (imgRectFE != nullptr) {
										displayW = imgRectFE->ActualWidth;
									}
									
								} catch (...) {
									displayW = 0.0;
								}
							}
							if (displayW <= 0.0) displayW = (double)tarWidth;

                            try {
                                float blurAmountLocal = GetBlurAmountFor(this->m_isGridLayout);
                                double paddedW = displayW + (blurAmountLocal * 2);
                                AnimateElementWidth(blurFE, paddedW, kAnimationDurationMs);
                            } catch(...) {
                                AnimateElementWidth(blurFE, (double)tarWidth, kAnimationDurationMs);
                            }
                        } catch (...) {
                            AnimateElementWidth(blurFE, (double)tarWidth, kAnimationDurationMs);
                        }
                        AnimateElementOpacity(blurFE, kBlurOpacitySelected, kAnimationDurationMs);
                    }
                }

                auto found2 = FindChildByName(container, ref new Platform::String(L"AppImageReflectionRect"));
                auto reflFE = dynamic_cast<Windows::UI::Xaml::FrameworkElement ^>(found2);
				if (reflFE != nullptr && img->PixelHeight > 0) {
                    try {
                        if (reflFE != nullptr) {
                            try { reflFE->Opacity = 0.0; } catch(...) {}
                            //try { reflFE->Width = tarWidth; } catch(...) {}
                            try { AnimateElementOpacity(reflFE, kReflectionOpacitySelected, kAnimationDurationMs); } catch(...) {}
                        }
                    } catch(...) {}
                }

            } catch(...) {}
        } catch(...) {}
    } catch(...) {}
}

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
            dbl->Duration = DurationHelper::FromTimeSpan(ts2);
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

// Animate a UIElement's width using the Composition Visual for smooth fades
static void AnimateElementWidth(
    FrameworkElement ^ element,
    double targetWidth,
    int durationMs = kAnimationDurationMs) {
	if (element == nullptr) return;

	try {
		using namespace Windows::UI::Xaml::Media::Animation;

		auto dbl = ref new DoubleAnimation();
		dbl->To = ref new Platform::Box<double>(targetWidth);

		Windows::Foundation::TimeSpan ts;
		ts.Duration = (int64_t)durationMs * 10000LL;
		dbl->Duration = DurationHelper::FromTimeSpan(ts);

		// Allow dependent animation
		dbl->EnableDependentAnimation = true;

		auto sb = ref new Storyboard();
		sb->Children->Append(dbl);
		Storyboard::SetTarget(dbl, element);
		Storyboard::SetTargetProperty(dbl, "(FrameworkElement.Width)");

		sb->Begin();

	} catch (...) {
		Utils::Logf("AnimateElementWidth failed.");
	}
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

    // Initialize event registration tokens to zero to avoid invalid remove calls later
    try {
        m_apps_changed_token.Value = 0;
        m_back_cookie.Value = 0;
        m_keydown_cookie.Value = 0;
        m_rendering_token.Value = 0;
        m_scrollviewer_viewchanged_token.Value = 0;
        m_layoutUpdated_token.Value = 0;
        m_appsgird_selection_token.Value = 0;
        m_appsgird_itemclick_token.Value = 0;
        m_appsgird_righttapped_token.Value = 0;
        m_appsgird_sizechanged_token.Value = 0;
        m_appsgird_loaded_token.Value = 0;
        m_appsgird_unloaded_token.Value = 0;
    } catch(...) {}

    // Use weak-capturing lambdas to avoid calling into a disconnected 'this' and add lightweight logging
    {
        auto weakThis = WeakReference(this);
        this->Loaded += ref new Windows::UI::Xaml::RoutedEventHandler([weakThis](Platform::Object^ s, Windows::UI::Xaml::RoutedEventArgs^ e) {
            auto that = weakThis.Resolve<AppPage>();
            if (that == nullptr) return;
            try { Utils::Log("AppPage: Loaded event invoked\n"); } catch(...) {}
            try { that->OnLoaded(s, e); } catch(...) {}
        });
    }
    {
        auto weakThis = WeakReference(this);
        this->Unloaded += ref new Windows::UI::Xaml::RoutedEventHandler([weakThis](Platform::Object^ s, Windows::UI::Xaml::RoutedEventArgs^ e) {
            auto that = weakThis.Resolve<AppPage>();
            if (that == nullptr) return;
            try { Utils::Log("AppPage: Unloaded event invoked\n"); } catch(...) {}
            try { that->OnUnloaded(s, e); } catch(...) {}
        });
    }
    {
        auto weakThis = WeakReference(this);
        this->SizeChanged += ref new Windows::UI::Xaml::SizeChangedEventHandler([weakThis](Platform::Object^ s, Windows::UI::Xaml::SizeChangedEventArgs^ e) {
            auto that = weakThis.Resolve<AppPage>();
            if (that == nullptr) return;
            try { that->PageRoot_SizeChanged(s, e); } catch(...) {}
        });
    }

    // Register AppsGrid event handlers in code (remove XAML-attached handlers to allow explicit unsubscribe)
    try {
        if (this->AppsGrid != nullptr) {
            auto weakThis = WeakReference(this);
            // SelectionChanged
            auto selToken = this->AppsGrid->SelectionChanged += ref new Windows::UI::Xaml::Controls::SelectionChangedEventHandler([weakThis](Platform::Object^ s, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e) {
                auto that = weakThis.Resolve<AppPage>(); if (that == nullptr) return; try { that->AppsGrid_SelectionChanged(s, e); } catch(...) {}
            });
            this->m_appsgird_selection_token = selToken;

            // ItemClick
            auto icToken = this->AppsGrid->ItemClick += ref new Windows::UI::Xaml::Controls::ItemClickEventHandler([weakThis](Platform::Object^ s, Windows::UI::Xaml::Controls::ItemClickEventArgs^ e) {
                auto that = weakThis.Resolve<AppPage>(); if (that == nullptr) return; try { that->AppsGrid_ItemClick(s, e); } catch(...) {}
            });
            this->m_appsgird_itemclick_token = icToken;

            // RightTapped
            auto rtToken = this->AppsGrid->RightTapped += ref new Windows::UI::Xaml::Input::RightTappedEventHandler([weakThis](Platform::Object^ s, Windows::UI::Xaml::Input::RightTappedRoutedEventArgs^ e) {
                auto that = weakThis.Resolve<AppPage>(); if (that == nullptr) return; try { that->AppsGrid_RightTapped(s, e); } catch(...) {}
            });
            this->m_appsgird_righttapped_token = rtToken;

            // SizeChanged
            auto szToken = this->AppsGrid->SizeChanged += ref new Windows::UI::Xaml::SizeChangedEventHandler([weakThis](Platform::Object^ s, Windows::UI::Xaml::SizeChangedEventArgs^ e) {
                auto that = weakThis.Resolve<AppPage>(); if (that == nullptr) return; try { that->AppsGrid_SizeChanged(s, e); } catch(...) {}
            });
            this->m_appsgird_sizechanged_token = szToken;

            // Loaded
            auto ldToken = this->AppsGrid->Loaded += ref new Windows::UI::Xaml::RoutedEventHandler([weakThis](Platform::Object^ s, Windows::UI::Xaml::RoutedEventArgs^ e) {
                auto that = weakThis.Resolve<AppPage>(); if (that == nullptr) return; try { that->AppsGrid_Loaded(s, e); } catch(...) {}
            });
            this->m_appsgird_loaded_token = ldToken;

            // Unloaded
            auto ulToken = this->AppsGrid->Unloaded += ref new Windows::UI::Xaml::RoutedEventHandler([weakThis](Platform::Object^ s, Windows::UI::Xaml::RoutedEventArgs^ e) {
                auto that = weakThis.Resolve<AppPage>(); if (that == nullptr) return; try { that->AppsGrid_Unloaded(s, e); } catch(...) {}
            });
            this->m_appsgird_unloaded_token = ulToken;

            // LayoutUpdated -> store token via add and assign to m_layoutUpdated_token

    try {
        if (this->LeftMenu != nullptr) {
            auto weakThis = WeakReference(this);
            auto sample = ref new moonlight_xbox_dx::MenuItem(
                ref new Platform::String(L"Sample Action"),
                ref new Platform::String(L"\uE710"),
                ref new Windows::Foundation::EventHandler<Platform::Object^>([weakThis](Platform::Object^ s, Platform::Object^ e) {
                    auto that = weakThis.Resolve<AppPage>(); if (that == nullptr) return;
                    try { that->OnSampleActionClicked(); } catch(...) {}
                })
            );
            this->LeftMenu->AddPageItem(sample);
        }
    } catch(...) {}
            auto luToken = this->AppsGrid->LayoutUpdated += ref new Windows::Foundation::EventHandler<Platform::Object^>([weakThis](Platform::Object^ s, Platform::Object^ e) {
                auto that = weakThis.Resolve<AppPage>(); if (that == nullptr) return; try { that->AppsGrid_LayoutUpdated(s, nullptr); } catch(...) {}
            });
            this->m_layoutUpdated_token = luToken;

            // ContainerContentChanging registration removed to avoid parsing issues; XAML no longer wires this event.
        }
    } catch(...) {}

	m_compositionReady = false;
    m_filteredApps = ref new Platform::Collections::Vector<MoonlightApp^>();
}

// Apply current search filter to Host->Apps and populate m_filteredApps
bool AppPage::ApplyAppFilter(Platform::String ^ filter) {
	auto host = this->Host;
	auto vec = this->m_filteredApps;
	bool identical = false;

	if (vec == nullptr) return identical;

	if (host == nullptr || host->Apps == nullptr) {
		vec->Clear();
		return identical;
	}

	// Build new filtered results into a temporary vector so we can compare before updating the bound collection.
	auto newResults = ref new Platform::Collections::Vector<moonlight_xbox_dx::MoonlightApp ^>();

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
				if (aid != bid) {
					identical = false;
					break;
				}
			}
		}
	} catch (...) {
		identical = false;
	}

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
        {
            auto weakThisTmp = WeakReference(this);
            this->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([weakThisTmp, emptyResults]() {
                try {
                    auto thatTmp = weakThisTmp.Resolve<AppPage>();
                    if (thatTmp == nullptr) return;
                    if (thatTmp->NoAppsMessage != nullptr) {
                        thatTmp->NoAppsMessage->Visibility = emptyResults ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
                    }

                    // If there are no results, animate out the shared SelectedAppBox/SelectedAppText
                    try {
                        if (emptyResults && thatTmp->SelectedAppText != nullptr && thatTmp->SelectedAppBox != nullptr) {
                            auto res = thatTmp->Resources;
                            if (res != nullptr) {
                                auto sbObj = res->Lookup(ref new Platform::String(L"HideSelectedAppStoryboard"));
                                auto sb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(sbObj);
                                if (sb != nullptr) {
                                    sb->Begin();
                                } else {
                                    if (thatTmp->m_compositionReady) {
                                        AnimateElementOpacity(thatTmp->SelectedAppBox, 0.0f, kAnimationDurationMs);
                                        AnimateElementOpacity(thatTmp->SelectedAppText, 0.0f, kAnimationDurationMs);
                                    } else {
                                        SetElementOpacityImmediate(thatTmp->SelectedAppBox, 0.0f);
                                        SetElementOpacityImmediate(thatTmp->SelectedAppText, 0.0f);
                                    }
                                }
                            }
                        }
                    } catch(...) {}

                } catch(...) {}
            }));
        }
    } catch (...) {}

	// If we have results, visually promote the first item and center it (no focus changes)
	try {
		if (vec->Size > 0 && this->AppsGrid != nullptr) {
			// Promote first item on the UI thread so visuals/layout are updated
			auto weakThis = WeakReference(this);
			auto isGrid = this->m_isGridLayout;
			auto svi = this->m_scrollViewer;
			auto appsGrid = this->AppsGrid;
			this->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([weakThis, appsGrid, isGrid, svi]() {
				try {
					auto that = weakThis.Resolve<AppPage>();
					if (that == nullptr) return;
					if (that->AppsGrid == nullptr) return;
					if (that->AppsGrid->Items != nullptr && that->AppsGrid->Items->Size > 0) {
						try {
							// Clear visuals on other realized containers so only the promoted item appears selected
							try {
								for (unsigned int i = 0; i < that->AppsGrid->Items->Size; ++i) {
									auto c = dynamic_cast<ListViewItem ^>(that->AppsGrid->ContainerFromIndex(i));
                                    if (c != nullptr) that->ApplyVisualsToContainer(c, false);
								}
							} catch (...) {
							}

							auto firstItem = that->AppsGrid->Items->GetAt(0);
							if (firstItem != nullptr) {
								// Ensure it's realized
								that->AppsGrid->ScrollIntoView(firstItem);
								auto container = dynamic_cast<ListViewItem ^>(that->AppsGrid->ContainerFromItem(firstItem));
                                if (container != nullptr) {
                                    that->ApplyVisualsToContainer(container, true);

									// Update the shared SelectedAppText/SelectedAppBox to mirror selection visuals
									try {
										if (that->SelectedAppText != nullptr && that->SelectedAppBox != nullptr) {
											auto selApp = dynamic_cast<moonlight_xbox_dx::MoonlightApp ^>(firstItem);
											if (selApp != nullptr) {
												that->SelectedAppText->Text = selApp->Name;
												that->SelectedAppBox->Visibility = Windows::UI::Xaml::Visibility::Visible;
												that->SelectedAppText->Visibility = Windows::UI::Xaml::Visibility::Visible;
												that->SelectedAppBox->Background = ref new SolidColorBrush(Windows::UI::Colors::Transparent);
												that->SelectedAppText->Foreground = ref new SolidColorBrush(Windows::UI::Colors::White);
												SetElementOpacityImmediate(that->SelectedAppBox, 0.0f);
												SetElementOpacityImmediate(that->SelectedAppText, 0.0f);
												auto res = that->Resources;
												if (res != nullptr) {
													auto sbObj = res->Lookup(ref new Platform::String(L"ShowSelectedAppStoryboard"));
													auto sb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard ^>(sbObj);
													if (sb != nullptr) sb->Begin();
												}
											}
										}
									} catch (...) {
									}

									// Center the container in the horizontal view without changing selection
									try {
										if (!isGrid) {
											auto sv = svi == nullptr ? FindScrollViewer(appsGrid) : svi;
											if (sv != nullptr) {
												Point pt{0, 0};
												try {
													auto trans2 = container->TransformToVisual(sv);
													if (trans2 != nullptr)
														pt = trans2->TransformPoint(Point{0, 0});
													else
														Utils::Log("OnNavigatedTo center: TransformToVisual returned null\n");
												} catch (...) {
													Utils::Log("OnNavigatedTo center: TransformToVisual/TransformPoint threw\n");
												}
												double containerCenter = pt.X + container->ActualWidth / 2.0;
												double desired = containerCenter - (sv->ViewportWidth / 2.0);
												if (desired < 0) desired = 0;
												if (desired > sv->ScrollableWidth) desired = sv->ScrollableWidth;
												if (sv->ScrollableWidth > 1.0) sv->ChangeView(desired, nullptr, nullptr, false);
											}
										}
									} catch (...) {
									}
								}
							}
						} catch (...) {
						}
					}
				} catch (...) {
				}
			}));
		}
	}
	catch (...) {
	}

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
                Platform::WeakReference weakThis(this);
                auto token = obs->VectorChanged += ref new Windows::Foundation::Collections::VectorChangedEventHandler<MoonlightApp^>([weakThis](Windows::Foundation::Collections::IObservableVector<MoonlightApp^>^ sender, Windows::Foundation::Collections::IVectorChangedEventArgs^ args) {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that == nullptr) return;
                    try { that->OnHostAppsChanged(sender, args); } catch(...) {}
                });
                m_apps_changed_token = token;
            }
        }
    } catch(...) {}

    // Populate filtered list initially (in case Apps were already present)
    ApplyAppFilter(nullptr);

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

    // Always show the flyout at the center of the window for consistent placement
    try {
        // Compute center point in coordinates relative to the root visual
        auto window = Windows::UI::Xaml::Window::Current;
        if (window != nullptr && window->Content != nullptr) {
            auto root = dynamic_cast<FrameworkElement^>(window->Content);
            if (root != nullptr) {
                double cx = 0.0, cy = 0.0;
                try { cx = root->ActualWidth / 2.0; } catch(...) { cx = 0.0; }
                try { cy = root->ActualHeight / 2.0; } catch(...) { cy = 0.0; }
                // Transform center point to the coordinate space of the anchor (if anchor exists)
                Windows::Foundation::Point centerPoint(cx, cy);
                UIElement^ placementTarget = this->AppsGrid;
                if (anchor != nullptr) placementTarget = anchor;
                try {
                    auto trans = root->TransformToVisual(placementTarget);
                    if (trans != nullptr) centerPoint = trans->TransformPoint(centerPoint);
                } catch(...) {}
                try {
                    // Show the left sliding menu instead of the flyout and populate page items
                        {
                            auto leftMenuObj_center = this->FindName(Platform::StringReference(L"LeftMenu"));
                            auto leftMenu_center = dynamic_cast<moonlight_xbox_dx::Controls::SlidingMenu^>(leftMenuObj_center);
                            if (leftMenu_center) {
                                try {
                                    leftMenu_center->ClearPageItems();
                                    Platform::WeakReference weakThis(this);

                                    // If the tapped app is running -> Resume + Close
                                    if (this->currentApp != nullptr && this->currentApp->CurrentlyRunning) {
                                        auto resumeItem = ref new moonlight_xbox_dx::MenuItem(
                                            ref new Platform::String(L"Resume App"),
                                            ref new Platform::String(L"\uE7C6"),
                                            ref new Windows::Foundation::EventHandler<Platform::Object^>([weakThis](Platform::Object^, Platform::Object^) {
                                                auto that = weakThis.Resolve<AppPage>(); if (that == nullptr) return;
                                                try { that->resumeAppButton_Click(nullptr, nullptr); } catch(...) {}
                                            })
                                        );
                                        leftMenu_center->AddPageItem(resumeItem);

                                        auto closeItem = ref new moonlight_xbox_dx::MenuItem(
                                            ref new Platform::String(L"Close App"),
                                            ref new Platform::String(L"\uE711"),
                                            ref new Windows::Foundation::EventHandler<Platform::Object^>([weakThis](Platform::Object^, Platform::Object^) {
                                                auto that = weakThis.Resolve<AppPage>(); if (that == nullptr) return;
                                                try { that->closeAppButton_Click(nullptr, nullptr); } catch(...) {}
                                            })
                                        );
                                        leftMenu_center->AddPageItem(closeItem);

                                    // Another app is running -> Close and Start
                                    } else if (anyRunning && runningApp != nullptr && runningApp->CurrentlyRunning && this->currentApp != nullptr && !this->currentApp->CurrentlyRunning) {
                                        auto casItem = ref new moonlight_xbox_dx::MenuItem(
                                            ref new Platform::String(L"Close and Start App"),
                                            ref new Platform::String(L"\uE8BB"),
                                            ref new Windows::Foundation::EventHandler<Platform::Object^>([weakThis](Platform::Object^, Platform::Object^) {
                                                auto that = weakThis.Resolve<AppPage>(); if (that == nullptr) return;
                                                try { that->closeAndStartButton_Click(nullptr, nullptr); } catch(...) {}
                                            })
                                        );
                                        leftMenu_center->AddPageItem(casItem);

                                    // No app running -> Start App
                                    } else {
                                        auto startItem = ref new moonlight_xbox_dx::MenuItem(
                                            ref new Platform::String(L"Start App"),
                                            ref new Platform::String(L"\uE768"),
                                            ref new Windows::Foundation::EventHandler<Platform::Object^>([weakThis](Platform::Object^, Platform::Object^) {
                                                auto that = weakThis.Resolve<AppPage>(); if (that == nullptr) return;
                                                try { that->resumeAppButton_Click(nullptr, nullptr); } catch(...) {}
                                            })
                                        );
                                        leftMenu_center->AddPageItem(startItem);
                                    }

                                    leftMenu_center->Open();
                                } catch(...) {}
                            }
                        }
                } catch(...) {
                    // Fallback: do nothing here (original flyout suppressed)
                }
            } else {
                if (anchor != nullptr) this->ActionsFlyout->ShowAt(anchor); else this->ActionsFlyout->ShowAt(this->AppsGrid);
            }
        } else {
            if (anchor != nullptr) this->ActionsFlyout->ShowAt(anchor); else this->ActionsFlyout->ShowAt(this->AppsGrid);
        }
    } catch(...) {
        if (anchor != nullptr) this->ActionsFlyout->ShowAt(anchor); else this->ActionsFlyout->ShowAt(this->AppsGrid);
    }
}

void AppPage::SearchBox_TextChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::TextChangedEventArgs^ e) {
    try {
        auto tb = dynamic_cast<TextBox^>(sender);
        if (tb == nullptr) return;
        bool collectionChanged = !ApplyAppFilter(tb->Text);
		if (collectionChanged) {
			// this->EnsureRealizedContainersInitialized(this->AppsGrid);
			this->AppsGrid->SelectedIndex = this->AppsGrid->SelectedIndex > -1 ? this->AppsGrid->SelectedIndex : 0;
			this->AppsGrid_SelectionChanged(this->AppsGrid, nullptr);
			// this->CenterSelectedItem(1, true);
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
    try {
        auto leftMenuObj_settings = this->FindName(Platform::StringReference(L"LeftMenu"));
        auto leftMenu_settings = dynamic_cast<moonlight_xbox_dx::Controls::SlidingMenu^>(leftMenuObj_settings);
        if (leftMenu_settings) leftMenu_settings->Open();
    } catch(...) {
        bool result = this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSettingsPage::typeid), Host);
    }
}

void AppPage::Page_RightTapped(Platform::Object^ sender, Windows::UI::Xaml::Input::RightTappedRoutedEventArgs^ e) {
    try {
        auto leftMenuObj_righttap = this->FindName(Platform::StringReference(L"LeftMenu"));
        auto leftMenu_righttap = dynamic_cast<moonlight_xbox_dx::Controls::SlidingMenu^>(leftMenuObj_righttap);
        if (leftMenu_righttap) leftMenu_righttap->Open();
        e->Handled = true;
    } catch(...) { }
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

void AppPage::BlurAppImage(MoonlightApp ^ selApp) {
	try {
		if (selApp->BlurredImage == nullptr) {
			Platform::WeakReference weakThis(this);
			try {
                ApplyBlur(selApp, GetBlurAmountFor(this->m_isGridLayout)).then([selApp, weakThis](Windows::Storage::Streams::IRandomAccessStream ^ stream) {
					try {
						if (stream == nullptr) {
							try {
								::moonlight_xbox_dx::Utils::Logf("[AppPage] SelectionChanged: ApplyBlur returned null stream for app id=%d\n", selApp->Id);
							} catch (...) {
							}
							return;
						}
						auto that = weakThis.Resolve<AppPage>();
						if (that == nullptr) return;
						that->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([selApp, stream, weakThis]() {
						    try {
							    auto thatInner = weakThis.Resolve<AppPage>();
							    if (thatInner == nullptr) return;
							    if (selApp == nullptr) return;
							    auto img = ref new BitmapImage();
							    concurrency::create_task(img->SetSourceAsync(stream)).then([weakThis, selApp, img, stream]() {
								    try {
									    try {
										    ::moonlight_xbox_dx::Utils::Logf("[AppPage] SelectionChanged: SetSourceAsync completed for app id=%d\n", selApp->Id);
									    } catch (...) {
									    }
									    auto thatCont = weakThis.Resolve<AppPage>();
									    if (thatCont == nullptr) return;
									    // Assign the resulting BitmapImage to the app's properties on UI thread
									    thatCont->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([thatCont, selApp, img]() {
										    try {

											    selApp->BlurredImage = img;
											    try {
												    ::moonlight_xbox_dx::Utils::Logf("[AppPage] SelectionChanged: assigned BlurredImage for app id=%d\n", selApp->Id);
											    } catch (...) {
											    }
											    // Attempt to size and fade-in blur/reflection for the selected app
											    try {
												    thatCont->FadeInRealizedBlurAndReflectionIfSelected(selApp, img);
											    } catch (...) {
											    }

										    } catch (Platform::Exception ^ ex) {
											    try {
												    Utils::Logf("[AppPage] SelectionChanged: Platform::Exception assigning BlurredImage id=%d msg=%S\n", selApp->Id, ex->Message->Data());
											    } catch (...) {
											    }
										    } catch (...) {
											    try {
												    Utils::Logf("[AppPage] SelectionChanged: unknown exception assigning BlurredImage for app id=%d\n", selApp->Id);
											    } catch (...) {
											    }
										    }
									    }));

									    // Also assign ReflectionImage
									    thatCont->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([selApp, img]() {
										    try {
											    selApp->ReflectionImage = img;
											    try {
												    ::moonlight_xbox_dx::Utils::Logf("[AppPage] SelectionChanged: assigned ReflectionImage for app id=%d\n", selApp->Id);
											    } catch (...) {
											    }
										    } catch (Platform::Exception ^ ex) {
											    try {
												    Utils::Logf("[AppPage] SelectionChanged: Platform::Exception assigning ReflectionImage id=%d msg=%S\n", selApp->Id, ex->Message->Data());
											    } catch (...) {
											    }
										    } catch (...) {
											    try {
												    Utils::Logf("[AppPage] SelectionChanged: unknown exception assigning ReflectionImage for app id=%d\n", selApp->Id);
											    } catch (...) {
											    }
										    }
									    }));

								    } catch (Platform::Exception ^ ex) {
									    try {
										    Utils::Logf("[AppPage] SelectionChanged: Platform::Exception in SetSourceAsync continuation id=%d msg=%S\n", selApp->Id, ex->Message->Data());
									    } catch (...) {
									    }
								    } catch (...) {
									    try {
										    Utils::Logf("[AppPage] SelectionChanged: unknown exception in SetSourceAsync continuation for app id=%d\n", selApp->Id);
									    } catch (...) {
									    }
								    }
							    },
								concurrency::task_continuation_context::use_arbitrary());
						    } catch (Platform::Exception ^ ex) {
							    try {
								    Utils::Logf("[AppPage] SelectionChanged: Platform::Exception dispatching to UI for app id=%d msg=%S\n", selApp->Id, ex->Message->Data());
							    } catch (...) {
							    }
						    } catch (...) {
							    try {
								    Utils::Logf("[AppPage] SelectionChanged: unknown exception dispatching to UI for app id=%d\n", selApp->Id);
							    } catch (...) {
							    }
						    }
					    }));
					} catch (Platform::Exception ^ ex) {
						try {
							Utils::Logf("[AppPage] SelectionChanged: Platform::Exception in ApplyBlur continuation id=%d msg=%S\n", selApp->Id, ex->Message->Data());
						} catch (...) {
						}
					} catch (...) {
						try {
							Utils::Logf("[AppPage] SelectionChanged: unknown exception in ApplyBlur continuation for app id=%d\n", selApp->Id);
						} catch (...) {
						}
					}
				},
                concurrency::task_continuation_context::use_current());
			} catch (Platform::Exception ^ ex) {
				try {
					Utils::Logf("[AppPage] SelectionChanged: Platform::Exception starting ApplyBlur for app id=%d msg=%S\n", selApp->Id, ex->Message->Data());
				} catch (...) {
				}
			} catch (...) {
				try {
					Utils::Logf("[AppPage] SelectionChanged: unknown exception starting ApplyBlur for app id=%d\n", selApp->Id);
				} catch (...) {
				}
			}
		}
	} catch (...) {
	}
}

void AppPage::OnBackRequested(Platform::Object ^ e, Windows::UI::Core::BackRequestedEventArgs ^ args) {
    try { Utils::Logf("[AppPage] Handler Enter: OnBackRequested\n"); } catch(...) {}
	// UWP on Xbox One triggers a back request whenever the B
	// button is pressed which can result in the app being
	// suspended if unhandled
	if (this->Frame->CanGoBack) {
		this->Frame->GoBack();
		args->Handled = true;
	}
}

void AppPage::AppsGrid_SizeChanged(Platform::Object ^ sender, Windows::UI::Xaml::SizeChangedEventArgs ^ e) {
}

void AppPage::AppsGrid_Unloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
}

void AppPage::AppsGrid_StatusChanged(Platform::Object^ sender, Platform::Object^ args) {
}

void AppPage::AppsGrid_ItemsChanged(Platform::Object^ sender, Platform::Object^ args) {
}

void AppPage::PageRoot_SizeChanged(Platform::Object ^ sender, Windows::UI::Xaml::SizeChangedEventArgs ^ e) {
    // Trigger an immediate update of item heights so AspectRatioBox.Height bindings re-evaluate.
    // UpdateItemHeights();
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
    {
        Platform::WeakReference weakThis(this);
        auto token = navigation->BackRequested += ref new EventHandler<BackRequestedEventArgs ^>([
            weakThis
        ](Platform::Object^ sender, BackRequestedEventArgs^ args) {
            auto that = weakThis.Resolve<AppPage>();
            if (that != nullptr) that->OnBackRequested(sender, args);
        });
        m_back_cookie = token;
    }

    // No diagnostics attached in OnLoaded (clean build)

    try {
        auto window = Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow;
        if (window != nullptr) {
            {
                Platform::WeakReference weakThis(this);
                auto kd = window->KeyDown += ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>([
                    weakThis
                ](CoreWindow^ sender, KeyEventArgs^ args) {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that != nullptr) that->OnGamepadKeyDown(sender, args);
                });
                m_keydown_cookie = kd;
            }
        }
    } catch(...) {}

    // Subscribe to a one-shot Rendering event so we can run logic after first visual render.
    try {
        // Save token so we can unsubscribe correctly later
        {
            Platform::WeakReference weakThis(this);
            auto rt = Windows::UI::Xaml::Media::CompositionTarget::Rendering += ref new EventHandler<Object^>([
                weakThis
            ](Platform::Object^ sender, Platform::Object^ args) {
                auto that = weakThis.Resolve<AppPage>();
                if (that != nullptr) that->OnFirstRender(sender, args);
            });
            m_rendering_token = rt;
        }
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

    // Unsubscribe AppsGrid event handlers registered in constructor
    try {
        if (this->AppsGrid != nullptr) {
            try { this->AppsGrid->SelectionChanged -= this->m_appsgird_selection_token; } catch(...) {}
            try { this->AppsGrid->ItemClick -= this->m_appsgird_itemclick_token; } catch(...) {}
            try { this->AppsGrid->RightTapped -= this->m_appsgird_righttapped_token; } catch(...) {}
            try { this->AppsGrid->SizeChanged -= this->m_appsgird_sizechanged_token; } catch(...) {}
            try { this->AppsGrid->Loaded -= this->m_appsgird_loaded_token; } catch(...) {}
            try { this->AppsGrid->Unloaded -= this->m_appsgird_unloaded_token; } catch(...) {}
            try { this->AppsGrid->LayoutUpdated -= this->m_layoutUpdated_token; } catch(...) {}
        }
    } catch(...) {}
}

void AppPage::OnFirstRender(Object^ sender, Object^ e) {

    Utils::Log("AppPage::OnFirstRender: first render occurred\n");
    
    // Unsubscribe so it only runs once
    try { Windows::UI::Xaml::Media::CompositionTarget::Rendering -= m_rendering_token; } catch(...) {}
    
    try {
        AppsGrid->SelectedIndex = this->AppsGrid->SelectedIndex > -1 ? this->AppsGrid->SelectedIndex : 0;
		AppsGrid_SelectionChanged(this->AppsGrid, nullptr);
    } catch(...) {}
}

void AppPage::OnHostAppsChanged(Windows::Foundation::Collections::IObservableVector<MoonlightApp^>^ sender, Windows::Foundation::Collections::IVectorChangedEventArgs^ args) {
    try {
        // Reapply the current search filter on the UI thread
        {
            auto weakThis = WeakReference(this);
            this->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([weakThis]() {
                try {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that == nullptr) return;
                    that->ApplyAppFilter(that->SearchBox != nullptr ? that->SearchBox->Text : nullptr);
                } catch(...) {}
            }));
        }
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
        Point pt{0,0};
        try {
            auto trans = container->TransformToVisual(sv);
            if (trans != nullptr) pt = trans->TransformPoint(Point{ 0, 0 });
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
    try { Utils::Logf("[AppPage] Handler Enter: AppsGrid_SelectionChanged\n"); } catch(...) {}

    // VALIDATE

    auto lv = (ListView^)sender;
    if (lv == nullptr) return;

    if (lv->SelectedIndex < 0) return;

    if (lv == nullptr) return;
    auto item = lv->SelectedItem;
    if (item == nullptr) return;

    Platform::Object^ prevItem = nullptr;
    try {
        if (e != nullptr && e->RemovedItems != nullptr && e->RemovedItems->Size > 0) {
            prevItem = e->RemovedItems->GetAt(0);
        }
    } catch(...) { prevItem = nullptr; }

	// SCROLL INTO VIEW

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

    // STYLING

    // Sets centerpoints
    EnsureRealizedContainersInitialized(lv);

    // Get blur
    try {
            auto selApp = dynamic_cast<moonlight_xbox_dx::MoonlightApp ^>(item);
		    if (selApp != nullptr) {
                if (selApp->BlurredImage == nullptr)
                {
			        BlurAppImage(selApp);
                } else {
				    FadeInRealizedBlurAndReflectionIfSelected(selApp, selApp->BlurredImage);
                }
		    }
		//}
	} catch (...) {
	}

    // Animate
    try {
        if (container != nullptr) this->ApplyVisualsToContainer(container, true);
        if (prevContainer != nullptr) this->ApplyVisualsToContainer(prevContainer, false);
    } catch(...) {}

    // Update the shared SelectedApp text overlay
    try {

        auto selApp = dynamic_cast<moonlight_xbox_dx::MoonlightApp^>(lv->SelectedItem);
        auto res = this->Resources;
        if (selApp != nullptr) {
            
            if (this->SelectedAppText != nullptr && this->SelectedAppBox != nullptr) {
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
                        if (m_compositionReady) {
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
                // Register ViewChanged (weak lambda) and save token for unsubscribe in OnUnloaded.
                Platform::WeakReference weakThis(this);
                auto token = m_scrollViewer->ViewChanged += ref new Windows::Foundation::EventHandler<Windows::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs ^>([
                    weakThis
                ](Platform::Object^ s, Windows::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs^ args) {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that == nullptr) return;
                    try { that->OnScrollViewerViewChanged(s, args); } catch(...) {}
                });
                m_scrollviewer_viewchanged_token = token;
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
}

void AppPage::AppsGrid_ContainerContentChanging(ListViewBase ^ sender, ContainerContentChangingEventArgs ^ args) {
}

void AppPage::AppsGrid_LayoutUpdated(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ args) {
}

void AppPage::AppsGrid_ScrollChanged(Platform::Object^ sender, Platform::Object^ args) {
}

void moonlight_xbox_dx::AppPage::OnScrollViewerViewChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs^ e) {
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

        if (m_isGridLayout == wantGrid) return;
        m_isGridLayout = wantGrid;

        if (this->AppsGrid != nullptr) {

            auto res = this->Resources;

            if (m_isGridLayout) {

                if (res != nullptr) {
                    auto panel = dynamic_cast<ItemsPanelTemplate^>(res->Lookup(ref new Platform::String(L"GridItemsPanelTemplate")));
                    if (panel != nullptr) this->AppsGrid->ItemsPanel = panel;
                }

				this->AppsGrid->SetValue(Windows::UI::Xaml::Controls::ScrollViewer::HorizontalScrollModeProperty, Windows::UI::Xaml::Controls::ScrollMode::Disabled);
                this->AppsGrid->SetValue(Windows::UI::Xaml::Controls::ScrollViewer::VerticalScrollModeProperty, Windows::UI::Xaml::Controls::ScrollMode::Enabled);
                this->AppsGrid->SetValue(Windows::UI::Xaml::Controls::ScrollViewer::VerticalScrollBarVisibilityProperty, Windows::UI::Xaml::Controls::ScrollBarVisibility::Auto);

            } else {

                if (res != nullptr) {
                    auto original = dynamic_cast<ItemsPanelTemplate^>(res->Lookup(ref new Platform::String(L"HorizontalItemsPanelTemplate")));
                    if (original != nullptr) this->AppsGrid->ItemsPanel = original;
                }

                this->AppsGrid->SetValue(Windows::UI::Xaml::Controls::ScrollViewer::HorizontalScrollModeProperty, Windows::UI::Xaml::Controls::ScrollMode::Enabled);
                this->AppsGrid->SetValue(Windows::UI::Xaml::Controls::ScrollViewer::VerticalScrollModeProperty, Windows::UI::Xaml::Controls::ScrollMode::Disabled);
                this->AppsGrid->SetValue(Windows::UI::Xaml::Controls::ScrollViewer::VerticalScrollBarVisibilityProperty, Windows::UI::Xaml::Controls::ScrollBarVisibility::Disabled);

            }


            m_itemsPanel = nullptr;
            m_scrollViewer = nullptr;

            for (int i = 0; i < this->Host->Apps->Size; i++) {
				auto app = this->Host->Apps->GetAt(i);
				app->BlurredImage = nullptr;
                app->ReflectionImage = nullptr;
				this->Host->Apps->SetAt(i, app);
            }

            this->UpdateItemHeights();

            if (this->AppsGrid->SelectedIndex >= 0) {
                this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this]() {
					AppsGrid_SelectionChanged(this->AppsGrid, nullptr);
                }));
            }
        }
    } catch(...) {}
}

void AppPage::OnGamepadKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args) {
    try {
        auto key = args->VirtualKey;

        using namespace Windows::System;
        if (key == VirtualKey::GamepadY) {
            {
                auto weakThis = WeakReference(this);
                this->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([weakThis]() {
                    try {
                        auto that = weakThis.Resolve<AppPage>();
                        if (that == nullptr) return;
                        try { that->SearchBox->Focus(Windows::UI::Xaml::FocusState::Programmatic); } catch(...) {}
                    } catch(...) {}
                }));
            }
            args->Handled = true;
        }

        if (key == VirtualKey::GamepadX) {
            {
                auto weakThis = WeakReference(this);
                this->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([weakThis]() {
                    try {
                        auto that = weakThis.Resolve<AppPage>();
                        if (that == nullptr) return;
                        try {
                            bool newState = !that->m_isGridLayout;
                            try { if (that->LayoutToggleButton != nullptr) that->LayoutToggleButton->IsChecked = newState; } catch(...) {}
                            that->LayoutToggleButton_Click(that->LayoutToggleButton, nullptr);
                        } catch(...) {}
                    } catch(...) {}
                }));
            }
			args->Handled = true;
		}
    } catch(...) {}
}

void moonlight_xbox_dx::AppPage::EnsureRealizedContainersInitialized(Windows::UI::Xaml::Controls::ListView^ lv) {
     try {
         if (lv == nullptr) return;
         double listTarget = lv->ActualHeight * kAppsGridHeightFactor;

         for (unsigned int i = 0; i < lv->Items->Size; ++i) {
             auto container = dynamic_cast<ListViewItem^>(lv->ContainerFromIndex(i));
             if (container == nullptr) continue;

             container->InvalidateMeasure();
             container->UpdateLayout();

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
                     double desired = listTarget;
                     double prev = fe->Height;
                     if (std::isnan(prev) || std::fabs(prev - desired) > 1.0) {
                         fe->Height = desired;
                         fe->InvalidateMeasure();
                     }
                 }
             }

             try {
                 auto desFE = FindChildByName(container, "Desaturator");
                 auto des = dynamic_cast<UIElement^>(desFE);
                 auto imgFE = FindChildByName(container, "AppImageRect");
				 auto img = dynamic_cast<UIElement ^>(imgFE);
				 auto blurFE = FindChildByName(container, "AppImageBlurRect");
				 auto blur = dynamic_cast<UIElement ^>(blurFE);
				 auto reflFE = FindChildByName(container, "AppImageReflectionRect");
				 auto refl = dynamic_cast<UIElement ^>(reflFE);
                 auto nameFE = FindChildByName(container, "AppName");
                 auto nameTxt = dynamic_cast<UIElement^>(nameFE);

                 bool isSelected = false;
                 try {
                     if (this->AppsGrid != nullptr && this->AppsGrid->SelectedIndex >= 0) {
                         int idx = this->AppsGrid->IndexFromContainer(container);
                         if (idx == this->AppsGrid->SelectedIndex) isSelected = true;
                     }
                 } catch(...) { isSelected = false; }

                try {
                    if (img != nullptr) {
                        auto imgVis = ElementCompositionPreview::GetElementVisual(img);
                        if (imgVis != nullptr) {
                            try { imgVis->StopAnimation("Scale.X"); imgVis->StopAnimation("Scale.Y"); } catch(...) {}

                            if (m_compositionReady) {
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
					if (blur != nullptr) {
						auto blurVis = ElementCompositionPreview::GetElementVisual(blur);
						if (blurVis != nullptr) {

							try {
								blurVis->StopAnimation("Scale.X");
								blurVis->StopAnimation("Scale.Y");
							} catch (...) {
							}

							if (m_compositionReady) {
								AnimateElementScale(blur, kUnselectedScale, kAnimationDurationMs);
							} else {
								Windows::Foundation::Numerics::float3 s;
								s.x = kUnselectedScale;
								s.y = s.x;
								s.z = 0.0f;
								blurVis->Scale = s;
							}

							auto blurFE2 = dynamic_cast<FrameworkElement ^>(blur);
							if (blurFE2 != nullptr && blurFE2->ActualWidth > 0 && blurFE2->ActualHeight > 0) {
								Windows::Foundation::Numerics::float3 cp;
								cp.x = (float)blurFE2->ActualWidth * 0.5f;
								cp.y = (float)blurFE2->ActualHeight * 0.5f;
								cp.z = 0.0f;
								blurVis->CenterPoint = cp;
							}
						}
					}
					if (refl != nullptr) {
						auto reflVis = ElementCompositionPreview::GetElementVisual(refl);
						if (reflVis != nullptr) {

							try {
								reflVis->StopAnimation("Scale.X");
								reflVis->StopAnimation("Scale.Y");
							} catch (...) {
							}

							if (m_compositionReady) {
								AnimateElementScale(refl, kUnselectedScale, kAnimationDurationMs);
							} else {
								Windows::Foundation::Numerics::float3 s;
								s.x = kUnselectedScale;
								s.y = s.x;
								s.z = 0.0f;
								reflVis->Scale = s;
							}

							auto reflFE2 = dynamic_cast<FrameworkElement ^>(refl);
							if (reflFE2 != nullptr && reflFE2->ActualWidth > 0 && reflFE2->ActualHeight > 0) {
								Windows::Foundation::Numerics::float3 cp;
								cp.x = (float)reflFE2->ActualWidth * 0.5f;
								cp.y = (float)reflFE2->ActualHeight * 0.5f;
								cp.z = 0.0f;
								reflVis->CenterPoint = cp;
							}
						}
					}
                    if (des != nullptr) {
                        auto desVis = ElementCompositionPreview::GetElementVisual(des);
                        if (desVis != nullptr) {

                            try { desVis->StopAnimation("Scale.X"); desVis->StopAnimation("Scale.Y"); desVis->StopAnimation("Opacity"); } catch(...) {}
                            
                            if (m_compositionReady) {
                                AnimateElementScale(dynamic_cast<UIElement^>(des), kUnselectedScale, kAnimationDurationMs);
                                AnimateElementOpacity(des, kDesaturatorOpacityUnselected, kAnimationDurationMs);
                            } else {
                                Windows::Foundation::Numerics::float3 s2; s2.x = kUnselectedScale; s2.y = s2.x; s2.z = 0.0f;
                                desVis->Scale = s2;
                                desVis->Opacity = kDesaturatorOpacityUnselected;
                            }
                            // Initialize Emboss to be invisible/unselected by default
                            try {
                                auto embossFE = FindChildByName(container, "Emboss");
                                auto emboss = dynamic_cast<UIElement^>(embossFE);
                                if (emboss != nullptr) {
                                    auto embossVis = ElementCompositionPreview::GetElementVisual(emboss);
                                    if (embossVis != nullptr) {
                                        try { embossVis->StopAnimation("Scale.X"); embossVis->StopAnimation("Scale.Y"); embossVis->StopAnimation("Opacity"); } catch(...) {}
                                        if (m_compositionReady) {
                                            AnimateElementScale(emboss, kUnselectedScale, kAnimationDurationMs);
                                            AnimateElementOpacity(emboss, 0.0f, kAnimationDurationMs);
                                        } else {
                                            Windows::Foundation::Numerics::float3 s4; s4.x = kUnselectedScale; s4.y = s4.x; s4.z = 0.0f;
                                            embossVis->Scale = s4;
                                            embossVis->Opacity = 0.0f;
                                        }
                                    }
                                }
                            } catch(...) {}

                            if (nameTxt != nullptr) {
                                if (m_compositionReady) {
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
                            // Also initialize Emboss element if present
                            try {
                                auto embossFE = FindChildByName(container, "Emboss");
                                auto emboss = dynamic_cast<UIElement^>(embossFE);
                                if (emboss != nullptr) {
                                    auto embossVis = ElementCompositionPreview::GetElementVisual(emboss);
                                    if (embossVis != nullptr) {
                                        try { embossVis->StopAnimation("Scale.X"); embossVis->StopAnimation("Scale.Y"); embossVis->StopAnimation("Opacity"); } catch(...) {}
                                        if (m_compositionReady) {
                                            AnimateElementScale(emboss, ::moonlight_xbox_dx::kUnselectedScale, kAnimationDurationMs);
                                            AnimateElementOpacity(emboss, 0.0f, kAnimationDurationMs);
                                        } else {
                                            embossVis->Opacity = 0.0f;
                                            Windows::Foundation::Numerics::float3 s3; s3.x = ::moonlight_xbox_dx::kUnselectedScale; s3.y = s3.x; s3.z = 0.0f;
                                            embossVis->Scale = s3;
                                        }
                                        auto embossFE2 = dynamic_cast<FrameworkElement^>(emboss);
                                        if (embossFE2 != nullptr && embossFE2->ActualWidth > 0 && embossFE2->ActualHeight > 0) {
                                            Windows::Foundation::Numerics::float3 cp3; cp3.x = (float)embossFE2->ActualWidth * 0.5f; cp3.y = (float)embossFE2->ActualHeight * 0.5f; cp3.z = 0.0f;
                                            embossVis->CenterPoint = cp3;
                                        }
                                    }
                                }
                            } catch(...) {}
                        }
                    }
                } catch(...) {}
             } catch(...) {}
        }
    } catch(...) {}
}

}

