#include "pch.h"
#include "AppPage.Helpers.h"
#include "Utils.hpp"
#include <chrono>
#include <cmath>
#include <sstream>
#include <vector>

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
    UIElement^& outBlur, UIElement^& outPlay)
{
    outDesaturator = outImage = outName = outBlur = outPlay = nullptr;
    if (container == nullptr) return;
    try {
        outDesaturator = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"Desaturator")));
        outImage       = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppImageRect")));
        outName        = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppName")));
        outBlur        = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppImageBlurRect")));
        outPlay        = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"Play")));
    } catch(...) {
        outDesaturator = outImage = outName = outBlur = outPlay = nullptr;
    }
}

// ── Utility helpers ───────────────────────────────────────────────────────────

double ParseDurationStringToMs(Platform::String^ durationValue) {
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

} // namespace moonlight_xbox_dx
