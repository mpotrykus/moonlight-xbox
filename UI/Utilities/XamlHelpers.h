#pragma once
#include "pch.h"

namespace moonlight_xbox_dx {

Windows::UI::Xaml::Controls::ScrollViewer^
    FindScrollViewer(Windows::UI::Xaml::DependencyObject^ parent);

Windows::UI::Xaml::FrameworkElement^
    FindChildByName(Windows::UI::Xaml::DependencyObject^ parent, Platform::String^ name);

Windows::UI::Color AdjustColorHSLLightSat(Windows::UI::Color in, double satMul, double lightMul);

void ApplyAccentColor(Windows::UI::Color color);

} // namespace moonlight_xbox_dx
