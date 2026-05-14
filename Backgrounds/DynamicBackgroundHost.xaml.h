#pragma once
#include "Backgrounds\DynamicBackgroundHost.g.h"

namespace moonlight_xbox_dx {

public ref class DynamicBackgroundHost sealed {
public:
    DynamicBackgroundHost();
    void Refresh();
    void StartAnimations();
    void StopAnimations();
private:
    Platform::String^ m_currentKey;
    void OnLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
};

}
