#pragma once
#include "Backgrounds\GradientBackground.g.h"

namespace moonlight_xbox_dx {

public ref class GradientBackground sealed {
public:
    GradientBackground();
    void StartAnimations();
    void StopAnimations();
private:
    Windows::UI::Xaml::Media::Animation::Storyboard^ m_storyboard;
};

}
