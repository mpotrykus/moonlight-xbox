#pragma once

#include "Pages\AddHostDialog.g.h"

namespace moonlight_xbox_dx
{
    [Windows::Foundation::Metadata::WebHostHidden]
    public ref class AddHostDialog sealed
    {
    public:
        AddHostDialog();
        void Configure(
            Windows::UI::Xaml::RoutedEventHandler^ onAdd,
            Windows::UI::Xaml::RoutedEventHandler^ onCancel);
        Platform::String^ GetHostname();
        void ShowError(Platform::String^ message);
        void SetAddButtonEnabled(bool enabled);

    private:
        Windows::UI::Xaml::RoutedEventHandler^ m_onAdd;
        Windows::UI::Xaml::RoutedEventHandler^ m_onCancel;
    };
}
