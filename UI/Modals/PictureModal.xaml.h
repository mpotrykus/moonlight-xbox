#pragma once

#include "UI\Modals\PictureModal.g.h"
#include "State\StreamConfiguration.h"

namespace moonlight_xbox_dx
{
    public delegate void PictureValueChangedHandler(PictureModal^ sender, int value);

    [Windows::Foundation::Metadata::WebHostHidden]
    public ref class PictureModal sealed
    {
    public:
        PictureModal();

        property bool IsVisible {
            bool get() { return m_visible; }
        }

        void Show();
        void Hide();
        void InstantHide();
        void SetValues(int contrast, int blackLevel, int whiteLevel, int gamma, int saturation);

        event PictureValueChangedHandler^ ContrastChanged;
        event PictureValueChangedHandler^ BlackLevelChanged;
        event PictureValueChangedHandler^ WhiteLevelChanged;
        event PictureValueChangedHandler^ GammaChanged;
        event PictureValueChangedHandler^ SaturationChanged;

    private:
        void HideStoryboard_Completed(Platform::Object^ sender, Platform::Object^ e);

        void ContrastSlider_ValueChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs^ e);
        void BlackLevelSlider_ValueChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs^ e);
        void WhiteLevelSlider_ValueChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs^ e);
        void GammaSlider_ValueChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs^ e);
        void SaturationSlider_ValueChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs^ e);

        void UpdateContrastValueText(int value);
        void UpdateBlackLevelValueText(int value);
        void UpdateWhiteLevelValueText(int value);
        void UpdateGammaValueText(int value);
        void UpdateSaturationValueText(int value);

        bool m_visible = false;
        bool m_loaded = false;
    };
}
