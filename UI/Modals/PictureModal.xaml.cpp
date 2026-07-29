#include "pch.h"
#include "UI\Modals\PictureModal.xaml.h"

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;

PictureModal::PictureModal()
{
    InitializeComponent();
    m_loaded = true;
}

void PictureModal::Show() {
    if (m_visible) return;
    m_visible = true;
    this->Root->Visibility = Windows::UI::Xaml::Visibility::Visible;
    this->HideStoryboard->Stop();
    this->ShowStoryboard->Begin();
    this->ContrastSlider->Focus(Windows::UI::Xaml::FocusState::Programmatic);
}

void PictureModal::Hide() {
    if (!m_visible) return;
    m_visible = false;
    this->ShowStoryboard->Stop();
    this->HideStoryboard->Begin();
    // Visibility collapsed after hide animation in HideStoryboard_Completed
}

void PictureModal::InstantHide() {
    m_visible = false;
    this->ShowStoryboard->Stop();
    this->HideStoryboard->Stop();
    this->Root->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
}

void PictureModal::HideStoryboard_Completed(Platform::Object^ sender, Platform::Object^ e) {
    this->Root->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
}

void PictureModal::SetValues(int contrast, int blackLevel, int whiteLevel, int gamma, int saturation) {
    UpdateContrastValueText(contrast);
    UpdateBlackLevelValueText(blackLevel);
    UpdateWhiteLevelValueText(whiteLevel);
    UpdateGammaValueText(gamma);
    UpdateSaturationValueText(saturation);
}

void PictureModal::UpdateContrastValueText(int value) {
    this->ContrastValueText->Text = ref new Platform::String((std::to_wstring(value) + L"%").c_str());
    this->ContrastSlider->Value = value;
}

void PictureModal::ContrastSlider_ValueChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs^ e) {
    if (!m_loaded) return;
    int value = std::max(50, std::min((int)e->NewValue, 200));
    UpdateContrastValueText(value);
    ContrastChanged(this, value);
}

void PictureModal::UpdateBlackLevelValueText(int value) {
    this->BlackLevelValueText->Text = ref new Platform::String((std::to_wstring(value) + L"%").c_str());
    this->BlackLevelSlider->Value = value;
}

void PictureModal::BlackLevelSlider_ValueChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs^ e) {
    if (!m_loaded) return;
    int value = std::max(0, std::min((int)e->NewValue, 50));
    UpdateBlackLevelValueText(value);
    BlackLevelChanged(this, value);
}

void PictureModal::UpdateWhiteLevelValueText(int value) {
    this->WhiteLevelValueText->Text = ref new Platform::String((std::to_wstring(value) + L"%").c_str());
    this->WhiteLevelSlider->Value = value;
}

void PictureModal::WhiteLevelSlider_ValueChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs^ e) {
    if (!m_loaded) return;
    int value = std::max(50, std::min((int)e->NewValue, 100));
    UpdateWhiteLevelValueText(value);
    WhiteLevelChanged(this, value);
}

void PictureModal::UpdateGammaValueText(int value) {
    wchar_t buf[16];
    swprintf_s(buf, L"%.2f", value / 100.0);
    this->GammaValueText->Text = ref new Platform::String(buf);
    this->GammaSlider->Value = value;
}

void PictureModal::GammaSlider_ValueChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs^ e) {
    if (!m_loaded) return;
    int value = std::max(50, std::min((int)e->NewValue, 200));
    UpdateGammaValueText(value);
    GammaChanged(this, value);
}

void PictureModal::UpdateSaturationValueText(int value) {
    this->SaturationValueText->Text = ref new Platform::String((std::to_wstring(value) + L"%").c_str());
    this->SaturationSlider->Value = value;
}

void PictureModal::SaturationSlider_ValueChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs^ e) {
    if (!m_loaded) return;
    int value = std::max(0, std::min((int)e->NewValue, 200));
    UpdateSaturationValueText(value);
    SaturationChanged(this, value);
}
