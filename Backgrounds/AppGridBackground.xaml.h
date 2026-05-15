#pragma once
#include "Backgrounds\AppGridBackground.g.h"
#include "State\MoonlightHost.h"
#include <vector>
#include <random>

namespace moonlight_xbox_dx {

struct AppBubble {
    float x, y;
    float vx, vy;
    float radius;
    float opacity;
    float opacityTarget;
    bool  isImageBubble;
    int   appIndex;
    int   elemBase;
};

public ref class AppGridBackground sealed {
public:
    AppGridBackground();
    void StartAnimations();
    void StopAnimations();
    void SetHosts(Windows::Foundation::Collections::IVector<MoonlightHost^>^ hosts);
private:
    Windows::UI::Xaml::DispatcherTimer^                         m_timer;
    Windows::Foundation::Collections::IVector<MoonlightHost^>^ m_hosts;
    Platform::Collections::Vector<MoonlightApp^>^               m_apps;
    std::vector<AppBubble>                                      m_bubbles;
    float m_canvasW        = 0;
    float m_canvasH        = 0;
    bool  m_initialized    = false;
    bool  m_appsLoaded     = false;
    int   m_spawnTick      = 0;
    int   m_loadRetryTick  = 0;
    int   m_nextAppIdx     = 0;
    int   m_bubbleSinceImg = 0;
    std::mt19937 m_rng;

    void Canvas_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
    void OnTick(Platform::Object^ sender, Platform::Object^ args);
    void SpawnBubble();
    void LoadAppsAsync();
};

}
