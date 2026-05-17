#pragma once
#include "pch.h"

namespace moonlight_xbox_dx {

[Windows::UI::Xaml::Data::Bindable]
public ref class UIPersonalization sealed : Windows::UI::Xaml::Data::INotifyPropertyChanged
{
private:
    Platform::String^ background = "";

public:
    virtual event Windows::UI::Xaml::Data::PropertyChangedEventHandler^ PropertyChanged;

    void OnPropertyChanged(Platform::String^ propertyName)
    {
        PropertyChanged(this, ref new Windows::UI::Xaml::Data::PropertyChangedEventArgs(propertyName));
    }

    property Platform::String^ Background
    {
        Platform::String^ get() { return this->background; }
        void set(Platform::String^ value) {
            if (background == value) return;
            this->background = value;
            OnPropertyChanged("Background");
        }
    }
};

}
