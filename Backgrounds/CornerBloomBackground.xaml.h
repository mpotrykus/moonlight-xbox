#pragma once
#include "Backgrounds\CornerBloomBackground.g.h"
#include "State\MoonlightHost.h"
#include <vector>
#include <random>

namespace moonlight_xbox_dx {

struct BloomBubble {
    float cornerX, cornerY;
    float maxRadius;
    float growSpeed;
    int   totalTicks;
    int   elapsedTicks;
    bool  active;
    bool  hasImage;
    Windows::UI::Xaml::Shapes::Ellipse^         cachedEllipse;
    Windows::UI::Xaml::Media::SolidColorBrush^  solidBrush;
};

public ref class CornerBloomBackground sealed {
public:
    CornerBloomBackground();
    void StartAnimations();
    void StopAnimations();
    void ResetAndReload();
    void SetHosts(Windows::Foundation::Collections::IVector<MoonlightHost^>^ hosts);
private:
    Windows::UI::Xaml::DispatcherTimer^                         m_timer;
    Windows::Foundation::Collections::IVector<MoonlightHost^>^ m_hosts;
    Platform::Collections::Vector<MoonlightApp^>^               m_apps;
    std::vector<BloomBubble>                                    m_pool;
    float m_canvasW       = 0;
    float m_canvasH       = 0;
    bool  m_initialized   = false;
    bool  m_appsLoaded    = false;
    int   m_spawnTick     = 0;
    int   m_loadRetryTick = 0;
    int   m_nextAppIdx    = 0;
    int   m_spawnCount    = 0;

    std::mt19937 m_rng;

    void Canvas_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
    void OnTick(Platform::Object^ sender, Platform::Object^ args);
    void SpawnBubble();
    void LoadAppsAsync();
    void DeactivateBubble(BloomBubble& b);
};

}
