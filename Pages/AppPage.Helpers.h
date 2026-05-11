#pragma once
#include "pch.h"

// Internal helpers shared across AppPage*.cpp translation units.
// Each .cpp must include pch.h before this header.

namespace moonlight_xbox_dx {

// ── Constants ────────────────────────────────────────────────────────────────

static constexpr float  kDesaturatorOpacityUnselected =  0.8f;
static constexpr float  kEmbossOpacitySelected        =  0.0f; //0.2f;
static constexpr float  kBackgroundOpacity            = 0.05f;
static constexpr float  kBackgroundSaturation         = 1.25f;
static constexpr float  kSelectedScale                =  1.3f;
static constexpr float  kUnselectedScale              =  1.0f;
static constexpr double kSelectedHPadding             =  75.0;
static constexpr double kAppsGridHeightFactor         =  0.50;
static constexpr int    kAnimationDurationMs          =   500; //150;
static constexpr int    kBgPanDurationSec             =    10;
static constexpr float  kBlurAmountBackground         =  2.0f;
// kBlurAmountGlow is intentionally removed; the glow blur amount is read at
// runtime from the "BlurAmount" XAML resource (AppPage.Resources.xaml) so
// that the blur processing and the padding/margin converters share one value.
static constexpr float  kBlurGlowPaddingDip           = 60.0f;
static constexpr float  kBlurGlowOpacity              = 0.50f;

// ── Visual-tree helpers ───────────────────────────────────────────────────────

Windows::UI::Xaml::Controls::ScrollViewer^
    FindScrollViewer(Windows::UI::Xaml::DependencyObject^ parent);

Windows::UI::Xaml::FrameworkElement^
    FindChildByName(Windows::UI::Xaml::DependencyObject^ parent, Platform::String^ name);

void FindElementChildren(
    Windows::UI::Xaml::DependencyObject^  container,
    Windows::UI::Xaml::UIElement^& outDesaturator,
    Windows::UI::Xaml::UIElement^& outImage,
    Windows::UI::Xaml::UIElement^& outName,
    Windows::UI::Xaml::UIElement^& outBlur,
    Windows::UI::Xaml::UIElement^& outReflection,
    Windows::UI::Xaml::UIElement^& outPlay,
    Windows::UI::Xaml::UIElement^& outEmboss);

// ── Color helpers ─────────────────────────────────────────────────────────────

Windows::UI::Color AdjustColorHSLLightSat(Windows::UI::Color in, double satMul, double lightMul);

// ── Animation helpers ─────────────────────────────────────────────────────────

//void AnimateElementOpacity(Windows::UI::Xaml::UIElement^ element, float targetOpacity,
//                           int durationMs = kAnimationDurationMs);
//
//void AnimateElementWidth(Windows::UI::Xaml::FrameworkElement^ element, double targetWidth,
//                         int durationMs = kAnimationDurationMs);
//
//void AnimateElementPadding(Windows::UI::Xaml::FrameworkElement^ element,
//                           Windows::UI::Xaml::Thickness targetPadding,
//                           int durationMs = kAnimationDurationMs);
//
//void SetElementOpacityImmediate(Windows::UI::Xaml::UIElement^ element, float value);
//void SetElementScaleImmediate(Windows::UI::Xaml::UIElement^ element, float scale);

// ── Selection visuals ─────────────────────────────────────────────────────────

void ApplySelectionVisuals(
    Windows::UI::Xaml::UIElement^ des,
    Windows::UI::Xaml::UIElement^ img,
    Windows::UI::Xaml::UIElement^ nameTxt,
    Windows::UI::Xaml::UIElement^ blur,
    Windows::UI::Xaml::UIElement^ reflection,
    Windows::UI::Xaml::UIElement^ play,
    Windows::UI::Xaml::UIElement^ emboss,
    bool selected,
    bool isGridLayout);

} // namespace moonlight_xbox_dx
