#include "pch.h"
#include "AppPage.Helpers.h"
#include "Utils.hpp"
#include <chrono>
#include <cmath>

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Hosting;
using namespace Windows::UI::Composition;
using namespace Windows::UI::Xaml::Media;
using namespace concurrency;

namespace moonlight_xbox_dx {

// ── Visual-tree helpers ───────────────────────────────────────────────────────

FrameworkElement^ FindChildByName(DependencyObject^ parent, Platform::String^ name) {
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

ScrollViewer^ FindScrollViewer(DependencyObject^ parent) {
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

void FindElementChildren(DependencyObject^ container,
    UIElement^& outDesaturator, UIElement^& outImage, UIElement^& outName,
    UIElement^& outBlur, UIElement^& outReflection, UIElement^& outPlay, UIElement^& outEmboss)
{
    outDesaturator = outImage = outName = outBlur = outReflection = outPlay = outEmboss = nullptr;
    if (container == nullptr) return;
    try {
        outDesaturator = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"Desaturator")));
        outImage       = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppImageRect")));
        outName        = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppName")));
        outBlur        = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppImageBlurRect")));
        outReflection  = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppImageReflectionRect")));
        outPlay        = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"Play")));
        outEmboss      = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"Emboss")));
    } catch(...) {
        outDesaturator = outImage = outName = outBlur = outEmboss = nullptr;
    }
}

// ── Color helpers ─────────────────────────────────────────────────────────────

static void RGBtoHSL(uint8_t r, uint8_t g, uint8_t b, double& h, double& s, double& l) {
    double rd = r / 255.0, gd = g / 255.0, bd = b / 255.0;
    double maxv = std::max(rd, std::max(gd, bd));
    double minv = std::min(rd, std::min(gd, bd));
    double delta = maxv - minv;
    l = (maxv + minv) / 2.0;
    if (delta < 1e-6) { h = s = 0.0; return; }
    s = l < 0.5 ? delta / (maxv + minv) : delta / (2.0 - maxv - minv);
    if      (maxv == rd) h = (gd - bd) / delta;
    else if (maxv == gd) h = 2.0 + (bd - rd) / delta;
    else                 h = 4.0 + (rd - gd) / delta;
    h *= 60.0;
    if (h < 0.0) h += 360.0;
}

static double hue2rgb(double p, double q, double t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0/6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0/2.0) return q;
    if (t < 2.0/3.0) return p + (q - p) * (2.0/3.0 - t) * 6.0;
    return p;
}

static void HSLtoRGB(double h, double s, double l, uint8_t& r, uint8_t& g, uint8_t& b) {
    double rd = 0.0, gd = 0.0, bd = 0.0;
    if (s <= 1e-6) {
        rd = gd = bd = l;
    } else {
        double hh = h / 360.0;
        double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
        double p = 2.0 * l - q;
        rd = hue2rgb(p, q, hh + 1.0/3.0);
        gd = hue2rgb(p, q, hh);
        bd = hue2rgb(p, q, hh - 1.0/3.0);
    }
    r = (uint8_t)std::round(std::max(0.0, std::min(1.0, rd)) * 255.0);
    g = (uint8_t)std::round(std::max(0.0, std::min(1.0, gd)) * 255.0);
    b = (uint8_t)std::round(std::max(0.0, std::min(1.0, bd)) * 255.0);
}

Windows::UI::Color AdjustColorHSLLightSat(Windows::UI::Color in, double satMul, double lightMul) {
    double h = 0, s = 0, l = 0;
    RGBtoHSL(in.R, in.G, in.B, h, s, l);
    s = std::max(0.0, std::min(1.0, s * satMul));
    l = std::max(0.0, std::min(1.0, l * lightMul));
    uint8_t r = 0, g = 0, b = 0;
    HSLtoRGB(h, s, l, r, g, b);
    Windows::UI::Color out; out.A = in.A; out.R = r; out.G = g; out.B = b;
    return out;
}

// ── Animation helpers ─────────────────────────────────────────────────────────

void SetElementOpacityImmediate(UIElement^ element, float value) {
    if (element == nullptr) return;
    try {
        auto vis = ElementCompositionPreview::GetElementVisual(element);
        if (vis != nullptr) {
            try { vis->StopAnimation("Opacity"); } catch(...) {}
            vis->Opacity = value;
        }
        element->Opacity = value;
    } catch(...) {}
}

void SetElementScaleImmediate(UIElement^ element, float scale) {
    if (element == nullptr) return;
    try {
        auto vis = ElementCompositionPreview::GetElementVisual(element);
        if (vis != nullptr) {
            try { vis->StopAnimation("Scale.X"); vis->StopAnimation("Scale.Y"); } catch(...) {}
            Windows::Foundation::Numerics::float3 s; s.x = scale; s.y = scale; s.z = 0.0f;
            vis->Scale = s;
        }
    } catch(...) {}
}

void AnimateElementOpacity(UIElement^ element, float targetOpacity, int durationMs) {
    if (element == nullptr) return;
    try {
        try {
            auto vis = ElementCompositionPreview::GetElementVisual(element);
            if (vis != nullptr) {
                auto compositor = vis->Compositor;
                auto anim = compositor->CreateScalarKeyFrameAnimation();
                TimeSpan ts; ts.Duration = (int64_t)durationMs * 10000LL;
                anim->Duration = ts;
                anim->InsertKeyFrame(1.0f, targetOpacity);
                try { vis->StopAnimation("Opacity"); } catch(...) {}
                vis->StartAnimation("Opacity", anim);
            }
        } catch(...) {}

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
            sb->Begin();
        } catch(...) {}
    } catch(...) {}
}

void AnimateElementWidth(FrameworkElement^ element, double targetWidth, int durationMs) {
    if (element == nullptr) return;
    try {
        using namespace Windows::UI::Xaml::Media::Animation;
        auto dbl = ref new DoubleAnimation();
        dbl->To = ref new Platform::Box<double>(targetWidth);
        TimeSpan ts; ts.Duration = (int64_t)durationMs * 10000LL;
        dbl->Duration = DurationHelper::FromTimeSpan(ts);
        dbl->EnableDependentAnimation = true;
        auto sb = ref new Storyboard();
        sb->Children->Append(dbl);
        Storyboard::SetTarget(dbl, element);
        Storyboard::SetTargetProperty(dbl, "(FrameworkElement.Width)");
        sb->Begin();
    } catch(...) {}
}

void AnimateElementScale(UIElement^ element, float targetScale, int durationMs) {
    if (element == nullptr) return;
    try {
        auto vis = ElementCompositionPreview::GetElementVisual(element);
        if (vis == nullptr) return;
        auto compositor = vis->Compositor;
        auto animX = compositor->CreateScalarKeyFrameAnimation();
        auto animY = compositor->CreateScalarKeyFrameAnimation();
        TimeSpan ts; ts.Duration = (int64_t)durationMs * 10000LL;
        animX->Duration = ts; animY->Duration = ts;
        animX->InsertKeyFrame(1.0f, targetScale);
        animY->InsertKeyFrame(1.0f, targetScale);
        try { vis->StopAnimation("Scale.X"); vis->StopAnimation("Scale.Y"); } catch(...) {}
        try { vis->StartAnimation("Scale.X", animX); vis->StartAnimation("Scale.Y", animY); } catch(...) {}
    } catch(...) {}
}

void AnimateElementPadding(FrameworkElement^ element, Windows::UI::Xaml::Thickness target, int durationMs) {
    if (element == nullptr) return;
    try {
        auto start = element->Margin;
        double durationSec = durationMs / 1000.0;
        auto startTime = std::chrono::steady_clock::now();

        auto timer = ref new DispatcherTimer();
        TimeSpan iv; iv.Duration = 166667LL; // ~60 fps
        timer->Interval = iv;

        WeakReference weakEl(element);
        WeakReference weakTimer(timer);

        timer->Tick += ref new EventHandler<Platform::Object^>(
            [weakEl, weakTimer, start, target, startTime, durationSec](Platform::Object^, Platform::Object^) {
                auto el  = weakEl.Resolve<FrameworkElement>();
                auto tmr = weakTimer.Resolve<DispatcherTimer>();
                if (el == nullptr || tmr == nullptr) { if (tmr != nullptr) tmr->Stop(); return; }
                double t = std::min(std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - startTime).count() / durationSec, 1.0);
                Windows::UI::Xaml::Thickness cur;
                cur.Left   = start.Left   + (target.Left   - start.Left)   * t;
                cur.Right  = start.Right  + (target.Right  - start.Right)  * t;
                cur.Top    = 0.0; cur.Bottom = 0.0;
                el->Margin = cur;
                if (t >= 1.0) tmr->Stop();
            });
        timer->Start();
    } catch(...) {}
}

// ── Selection visuals ─────────────────────────────────────────────────────────

void ApplySelectionVisuals(UIElement^ des, UIElement^ img, UIElement^ nameTxt,
    UIElement^ blur, UIElement^ reflection, UIElement^ play, UIElement^ emboss,
    bool selected, bool isGridLayout)
{
    try {
        float targetScale = (!isGridLayout && selected) ? kSelectedScale : kUnselectedScale;

        if (img  != nullptr) AnimateElementScale(img,  targetScale, kAnimationDurationMs);

        if (des  != nullptr) {
            AnimateElementScale(des,  targetScale, kAnimationDurationMs);
            AnimateElementOpacity(des, selected ? 0.0f : kDesaturatorOpacityUnselected, kAnimationDurationMs);
        }

        if (play != nullptr) AnimateElementScale(play, targetScale, kAnimationDurationMs);

        if (emboss != nullptr) {
            AnimateElementScale(emboss, targetScale, kAnimationDurationMs);
            AnimateElementOpacity(emboss, selected ? kEmbossOpacitySelected : 0.0f, kAnimationDurationMs);
        }

        if (blur != nullptr) {
            if (!isGridLayout && selected) {
                blur->Visibility = Visibility::Visible;
                AnimateElementOpacity(blur, kBlurGlowOpacity, kAnimationDurationMs);
            } else {
                AnimateElementOpacity(blur, 0.0f, kAnimationDurationMs);
                blur->Visibility = Visibility::Collapsed;
            }
        }
        if (reflection != nullptr) reflection->Visibility = Visibility::Collapsed;
        if (nameTxt != nullptr)    AnimateElementOpacity(nameTxt, selected ? 1.0f : 0.0f, kAnimationDurationMs);
    } catch(...) {}
}

} // namespace moonlight_xbox_dx
