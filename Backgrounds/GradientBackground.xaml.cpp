#include "pch.h"
#include "Backgrounds\GradientBackground.xaml.h"

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Media::Animation;

GradientBackground::GradientBackground()
{
    InitializeComponent();

    auto brush = ref new LinearGradientBrush();
    brush->StartPoint = Point(0.0f, 0.0f);
    brush->EndPoint = Point(1.0f, 1.0f);

    auto stop0 = ref new GradientStop();
    stop0->Color = ColorHelper::FromArgb(255, 13, 27, 42);
    stop0->Offset = 0.0;

    auto stop1 = ref new GradientStop();
    stop1->Color = ColorHelper::FromArgb(255, 26, 26, 46);
    stop1->Offset = 0.5;

    auto stop2 = ref new GradientStop();
    stop2->Color = ColorHelper::FromArgb(255, 22, 33, 62);
    stop2->Offset = 1.0;

    brush->GradientStops->Append(stop0);
    brush->GradientStops->Append(stop1);
    brush->GradientStops->Append(stop2);

    GradientRect->Fill = brush;

    m_storyboard = ref new Storyboard();
    m_storyboard->RepeatBehavior = RepeatBehaviorHelper::Forever;

    auto makeAnim = [](GradientStop^ stop, Color from, Color to, long long durationSec, long long beginSec) {
        auto anim = ref new ColorAnimation();
        anim->From = from;
        anim->To = to;
        TimeSpan dur; dur.Duration = durationSec * 10000000LL;
        anim->Duration = DurationHelper::FromTimeSpan(dur);
        anim->AutoReverse = true;
        anim->EnableDependentAnimation = true;
        if (beginSec > 0) {
            TimeSpan begin; begin.Duration = beginSec * 10000000LL;
            anim->BeginTime = begin;
        }
        Storyboard::SetTarget(anim, stop);
        Storyboard::SetTargetProperty(anim, ref new Platform::String(L"Color"));
        return anim;
    };

    m_storyboard->Children->Append(
        makeAnim(stop0,
            ColorHelper::FromArgb(255, 13, 27, 42),
            ColorHelper::FromArgb(255, 27, 13, 42),
            8LL, 0LL));

    m_storyboard->Children->Append(
        makeAnim(stop1,
            ColorHelper::FromArgb(255, 26, 26, 46),
            ColorHelper::FromArgb(255, 46, 26, 26),
            6LL, 2LL));

    m_storyboard->Children->Append(
        makeAnim(stop2,
            ColorHelper::FromArgb(255, 22, 33, 62),
            ColorHelper::FromArgb(255, 62, 33, 22),
            10LL, 4LL));
}

void GradientBackground::StartAnimations()
{
    if (m_storyboard != nullptr) m_storyboard->Begin();
}

void GradientBackground::StopAnimations()
{
    if (m_storyboard != nullptr) m_storyboard->Stop();
}
