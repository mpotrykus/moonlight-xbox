#include "pch.h"
#include "AppPage.xaml.h"
#include "AppPage.Helpers.h"
#include "Utils.hpp"
#include "Common/ImageHelpers.h"

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Graphics::Display;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Hosting;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace Windows::Storage::Streams;
using namespace concurrency;

namespace moonlight_xbox_dx {

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

                // Compute and store average color for overlay
                try {
                    unsigned int avg = 0;
                    try {
                        if (capturedImage != nullptr)
                            avg = ImageHelpers::CreateAverageColorArgb(capturedImage);
                        else if (softwareBitmap != nullptr)
                            avg = ImageHelpers::CreateAverageColorArgb(softwareBitmap);
                    } catch(...) { avg = 0xFF000000; }

                    if (app != nullptr) app->AverageColorArgb = avg;
                    auto weakThis = WeakReference(this);
                    this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                        ref new DispatchedHandler([weakThis, app]() {
                            auto that = weakThis.Resolve<AppPage>();
                            if (that == nullptr) return;
                            try { that->UpdateAverageColorOverlay(app); } catch(...) {}
                        }));
                } catch(...) {}

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

        // Background blur — always generated, used for page background and reflection
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
                                            try { thatCont->FadeInRealizedBlurAndReflectionIfSelected(selApp, imgCapture); } catch(...) {}
                                        } catch(...) {}
                                    }));

                                    thatCont->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                                        ref new DispatchedHandler([selApp, imgCapture]() {
                                        try { selApp->ReflectionImage = imgCapture; } catch(...) {}
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

// ── AppPage::FadeInRealizedBlurAndReflectionIfSelected ───────────────────────

void AppPage::FadeInRealizedBlurAndReflectionIfSelected(MoonlightApp^ app, BitmapImage^ img) {
    try {
        if (app == nullptr || img == nullptr) return;
        try {
            if (this->AppsGrid == nullptr) return;
            auto selApp = dynamic_cast<MoonlightApp^>(this->AppsGrid->SelectedItem);
            if (selApp == nullptr || selApp != app) return;
        } catch(...) { return; }

        try {
            // Notify ViewModel to transition to the new blurred image
            auto vm = this->ViewModel;
            if (vm != nullptr) {
                vm->TransitionToBlurredImage(img);
            }
        } catch(...) {}

        UpdateAverageColorOverlay(app);
    } catch(...) {}
}

// ── AppPage::UpdateAverageColorOverlay ────────────────────────────────────────

void AppPage::UpdateAverageColorOverlay(MoonlightApp^ app) {
    try {
        unsigned int argb = 0xFF000000;
        if (app != nullptr) { try { argb = app->AverageColorArgb; } catch(...) {} }
        uint8_t a = (uint8_t)((argb >> 24) & 0xFF);
        uint8_t r = (uint8_t)((argb >> 16) & 0xFF);
        uint8_t g = (uint8_t)((argb >>  8) & 0xFF);
        uint8_t b = (uint8_t)( argb        & 0xFF);
        Windows::UI::Color centerColor; centerColor.A = a; centerColor.R = r; centerColor.G = g; centerColor.B = b;
        Windows::UI::Color adjusted = AdjustColorHSLLightSat(centerColor, 3.0, 0.25);
        if (app != nullptr && app->AverageBrush != nullptr) {
            try { app->AverageBrush->Color = adjusted; } catch(...) {}
        }
    } catch(...) {}
}

} // namespace moonlight_xbox_dx} // namespace moonlight_xbox_dx