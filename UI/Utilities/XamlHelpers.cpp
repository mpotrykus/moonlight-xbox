#include "pch.h"
#include "XamlHelpers.h"
#include <cmath>

using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media;

namespace moonlight_xbox_dx {

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

void ApplyAccentColor(Windows::UI::Color color) {
    // Use XamlBindingHelper::ConvertValue to produce a properly XAML-boxed Color.
    // Direct C++/CX boxing of WinRT structs (IReference<Color>) is rejected by
    // ResourceDictionary at runtime with E_UNEXPECTED.
    wchar_t buf[16];
    swprintf_s(buf, L"#%02X%02X%02X%02X", color.A, color.R, color.G, color.B);
    Windows::UI::Xaml::Interop::TypeName colorType;
    colorType.Name = "Windows.UI.Color";
    colorType.Kind = Windows::UI::Xaml::Interop::TypeKind::Metadata;
    auto boxed = Windows::UI::Xaml::Markup::XamlBindingHelper::ConvertValue(
        colorType, ref new Platform::String(buf));

    static const wchar_t* themeKeys[] = { L"Default", L"Dark", L"Light" };
    auto appThemes = Windows::UI::Xaml::Application::Current->Resources->ThemeDictionaries;
    for (auto themeKey : themeKeys) {
        auto key = ref new Platform::String(themeKey);
        Windows::UI::Xaml::ResourceDictionary^ dict;
        if (appThemes->HasKey(key)) {
            dict = dynamic_cast<Windows::UI::Xaml::ResourceDictionary^>(appThemes->Lookup(key));
        } else {
            dict = ref new Windows::UI::Xaml::ResourceDictionary();
            appThemes->Insert(key, dict);
        }
        if (dict != nullptr) {
            dict->Insert("SystemAccentColor", boxed);
            // Override accent-derived brush resources so controls that don't use
            // SystemAccentColor directly (ToggleButton checked background, TextBox
            // focus border) also pick up the custom color.
            auto brush = ref new Windows::UI::Xaml::Media::SolidColorBrush(color);
            dict->Insert("SystemControlHighlightAccentBrush", brush);
            dict->Insert("SystemControlBackgroundAccentBrush", brush);
        }
    }
}

} // namespace moonlight_xbox_dx
