#pragma once
#include "Backgrounds\DynamicBackgroundHost.g.h"
#include "State\MoonlightHost.h"

namespace moonlight_xbox_dx {

public ref class DynamicBackgroundHost sealed {
public:
    DynamicBackgroundHost();
    void Refresh();
    void StartAnimations();
    void StopAnimations();
    void ResetBackground();
    void SetHosts(Windows::Foundation::Collections::IVector<MoonlightHost^>^ hosts);
private:
    Platform::String^ m_currentKey;
    Platform::String^ m_incomingKey;
    Windows::UI::Xaml::Media::Animation::Storyboard^ m_fadeStoryboard;
    Windows::Foundation::Collections::IVector<MoonlightHost^>^ m_hosts;
    void OnLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
};

}
