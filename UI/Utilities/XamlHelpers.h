#pragma once
#include "pch.h"

namespace moonlight_xbox_dx {

Windows::UI::Xaml::Controls::ScrollViewer^
    FindScrollViewer(Windows::UI::Xaml::DependencyObject^ parent);

Windows::UI::Xaml::FrameworkElement^
    FindChildByName(Windows::UI::Xaml::DependencyObject^ parent, Platform::String^ name);

void ApplyAccentColor(Windows::UI::Color color);
Windows::UI::Color GetAppliedAccentColor();

} // namespace moonlight_xbox_dx
