#include "pch.h"
#include "Controls\LunarPhaseControl.xaml.h"
#include <cmath>

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Media::Animation;
using namespace Windows::Foundation;

static constexpr long long kAnimationMs = 5000;

Windows::UI::Xaml::DependencyProperty^ LunarPhaseControl::m_showOrbitProperty =
    DependencyProperty::Register(
        "ShowOrbit", bool::typeid, LunarPhaseControl::typeid,
        ref new PropertyMetadata(false,
            ref new PropertyChangedCallback(&LunarPhaseControl::OnShowOrbitChanged)));

Windows::UI::Xaml::DependencyProperty^ LunarPhaseControl::m_showLockProperty =
    DependencyProperty::Register(
        "ShowLock", Windows::UI::Xaml::Visibility::typeid, LunarPhaseControl::typeid,
        ref new PropertyMetadata(Windows::UI::Xaml::Visibility::Collapsed,
            ref new PropertyChangedCallback(&LunarPhaseControl::OnShowLockChanged)));

Windows::UI::Xaml::DependencyProperty^ LunarPhaseControl::m_showDisconnectedProperty =
    DependencyProperty::Register(
        "ShowDisconnected", Windows::UI::Xaml::Visibility::typeid, LunarPhaseControl::typeid,
        ref new PropertyMetadata(Windows::UI::Xaml::Visibility::Collapsed,
            ref new PropertyChangedCallback(&LunarPhaseControl::OnShowDisconnectedChanged)));

Windows::UI::Xaml::DependencyProperty^ LunarPhaseControl::m_shadowCenterXProperty =
    DependencyProperty::Register(
        "ShadowCenterX", double::typeid, LunarPhaseControl::typeid,
        ref new PropertyMetadata(80.0,
            ref new PropertyChangedCallback(&LunarPhaseControl::OnShadowCenterXChanged)));

LunarPhaseControl::LunarPhaseControl()
{
    InitializeComponent();
    m_phaseStoryboard = nullptr;

    // Build PathGeometry for the crescent shape entirely in code so no x:Name
    // is needed on geometry objects inside Path.Data.
    m_outerArc = ref new ArcSegment();
    m_outerArc->Size = Size(76.0f, 76.0f);
    m_outerArc->IsLargeArc = true;
    m_outerArc->SweepDirection = SweepDirection::Clockwise;

    m_innerArc = ref new ArcSegment();
    m_innerArc->Size = Size(76.0f, 76.0f);
    m_innerArc->IsLargeArc = false;
    m_innerArc->SweepDirection = SweepDirection::Counterclockwise;

    m_crescentFigure = ref new PathFigure();
    m_crescentFigure->IsClosed = true;
    m_crescentFigure->IsFilled = true;
    m_crescentFigure->Segments->Append(m_outerArc);
    m_crescentFigure->Segments->Append(m_innerArc);

    auto pathGeo = ref new PathGeometry();
    pathGeo->Figures->Append(m_crescentFigure);

    CrescentPath->Data = pathGeo;

    // Start hidden (shadow coincident = new moon)
    CrescentPath->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
}

bool LunarPhaseControl::ShowOrbit::get()
{
    return (bool)GetValue(m_showOrbitProperty);
}
void LunarPhaseControl::ShowOrbit::set(bool v)
{
    SetValue(m_showOrbitProperty, v);
}

Windows::UI::Xaml::Visibility LunarPhaseControl::ShowLock::get()
{
    return (Windows::UI::Xaml::Visibility)GetValue(m_showLockProperty);
}
void LunarPhaseControl::ShowLock::set(Windows::UI::Xaml::Visibility v)
{
    SetValue(m_showLockProperty, v);
}

Windows::UI::Xaml::Visibility LunarPhaseControl::ShowDisconnected::get()
{
    return (Windows::UI::Xaml::Visibility)GetValue(m_showDisconnectedProperty);
}
void LunarPhaseControl::ShowDisconnected::set(Windows::UI::Xaml::Visibility v)
{
    SetValue(m_showDisconnectedProperty, v);
}

double LunarPhaseControl::ShadowCenterX::get()
{
    return (double)GetValue(m_shadowCenterXProperty);
}
void LunarPhaseControl::ShadowCenterX::set(double v)
{
    SetValue(m_shadowCenterXProperty, v);
}

void LunarPhaseControl::OnShowOrbitChanged(
    DependencyObject^ d, DependencyPropertyChangedEventArgs^ e)
{
    auto ctrl = dynamic_cast<LunarPhaseControl^>(d);
    if (ctrl == nullptr) return;
    bool show = (bool)e->NewValue;
    ctrl->OrbitCanvas->Visibility = show
        ? Windows::UI::Xaml::Visibility::Visible
        : Windows::UI::Xaml::Visibility::Collapsed;
    VisualStateManager::GoToState(ctrl, show ? "Orbiting" : "NotOrbiting", true);
}

void LunarPhaseControl::OnShowLockChanged(
    DependencyObject^ d, DependencyPropertyChangedEventArgs^ e)
{
    auto ctrl = dynamic_cast<LunarPhaseControl^>(d);
    if (ctrl == nullptr) return;
    ctrl->LockIcon->Visibility = (Windows::UI::Xaml::Visibility)e->NewValue;
}

void LunarPhaseControl::OnShowDisconnectedChanged(
    DependencyObject^ d, DependencyPropertyChangedEventArgs^ e)
{
    auto ctrl = dynamic_cast<LunarPhaseControl^>(d);
    if (ctrl == nullptr) return;
    ctrl->DisconnectedIcon->Visibility = (Windows::UI::Xaml::Visibility)e->NewValue;
}

void LunarPhaseControl::OnShadowCenterXChanged(
    DependencyObject^ d, DependencyPropertyChangedEventArgs^ e)
{
    auto ctrl = dynamic_cast<LunarPhaseControl^>(d);
    if (ctrl == nullptr) return;
    ctrl->SetCrescentPath((double)e->NewValue);
}

void LunarPhaseControl::SetCrescentPath(double sx)
{
    const double cx = 80.0, cy = 80.0, r = 76.0;
    double d = std::abs(sx - cx);

    if (d < 0.5) {
        CrescentPath->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
        return;
    }

    CrescentPath->Visibility = Windows::UI::Xaml::Visibility::Visible;

    // Clamp so circles always overlap (d < 2r) to avoid degenerate case
    if (d > 2.0 * r - 0.5) d = 2.0 * r - 0.5;
    // Recompute sx with clamped d
    sx = (sx < cx) ? cx - d : cx + d;

    double ix = (cx + sx) / 2.0;
    double h = std::sqrt(r * r - (d / 2.0) * (d / 2.0));
    double iy1 = cy - h; // top intersection
    double iy2 = cy + h; // bottom intersection

    bool crescentOnRight = (sx < cx);

    m_crescentFigure->StartPoint = Point((float)ix, (float)iy1);

    // Outer arc: from top intersection to bottom, going around the LIT side.
    // Always a large arc (> 180 degrees) going CW for right-crescent, CCW for left-crescent.
    m_outerArc->Point = Point((float)ix, (float)iy2);
    m_outerArc->IsLargeArc = true;
    m_outerArc->SweepDirection = crescentOnRight
        ? SweepDirection::Clockwise
        : SweepDirection::Counterclockwise;

    // Inner arc: from bottom back to top, going around the shadow side (short arc).
    m_innerArc->Point = Point((float)ix, (float)iy1);
    m_innerArc->IsLargeArc = false;
    m_innerArc->SweepDirection = crescentOnRight
        ? SweepDirection::Counterclockwise
        : SweepDirection::Clockwise;
}

void LunarPhaseControl::SetSelected(bool selected, bool animated)
{
    VisualStateManager::GoToState(this, selected ? "Selected" : "Normal", animated);
}

void LunarPhaseControl::UpdatePhase(double fillAmount, int side, bool animated)
{
    // Compute target shadow center X.
    // Cap at 150 so the shadow circle always overlaps the outer circle (avoids overflow artifacts).
    double targetX;
    if (side == 0) {
        targetX = 80.0;
    } else if (side > 0) {
        targetX = 80.0 - fillAmount * 150.0;
    } else {
        targetX = 80.0 + fillAmount * 150.0;
    }

    // Capture current displayed value BEFORE Stop(), which reverts the
    // dependency property to its base value and would lose the mid-animation position.
    double fromX = ShadowCenterX;

    if (m_phaseStoryboard != nullptr) {
        try { m_phaseStoryboard->Stop(); } catch (...) {}
        m_phaseStoryboard = nullptr;
    }

    if (!animated) {
        ShadowCenterX = targetX;
        return;
    }

    // Re-commit as base value so a future Stop() also lands here, not at the original default.
    ShadowCenterX = fromX;

    auto anim = ref new DoubleAnimation();
    anim->From = fromX;
    anim->To = targetX;

    TimeSpan ts;
    ts.Duration = kAnimationMs * 10000LL;
    anim->Duration = Windows::UI::Xaml::Duration(ts);
    anim->EnableDependentAnimation = true;

    auto ease = ref new CubicEase();
    ease->EasingMode = EasingMode::EaseOut;
    anim->EasingFunction = ease;

    auto sb = ref new Storyboard();
    Storyboard::SetTarget(anim, this);
    Storyboard::SetTargetProperty(anim, "ShadowCenterX");
    sb->Children->Append(anim);

    m_phaseStoryboard = sb;
    sb->Begin();
}
