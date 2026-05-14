#pragma once
#include "Backgrounds\BubblesBackground.g.h"
#include <vector>
#include <random>

namespace moonlight_xbox_dx {

struct BubbleState {
    float x, y;
    float vx, vy;
    float radius;
    float opacity;
    float opacityDelta;
    float opacityMin;
    float opacityMax;
    float specularOpacity;
    int   colorIndex;
};

public ref class BubblesBackground sealed {
public:
    BubblesBackground();
    void StartAnimations();
    void StopAnimations();
private:
    Windows::UI::Xaml::DispatcherTimer^ m_timer;
    std::vector<BubbleState> m_bubbles;
    float m_canvasW = 0;
    float m_canvasH = 0;
    bool  m_initialized = false;
    std::mt19937 m_rng;

    void Canvas_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
    void OnTick(Platform::Object^ sender, Platform::Object^ args);
    void InitBubbles();
};

}
