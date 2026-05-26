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

static Windows::UI::Color s_appliedAccentColor = Windows::UI::Color{ 255, 0, 120, 215 };

Windows::UI::Color GetAppliedAccentColor() {
    return s_appliedAccentColor;
}

void ApplyAccentColor(Windows::UI::Color color) {
    s_appliedAccentColor = color;
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
