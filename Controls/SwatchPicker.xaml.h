#pragma once
#include "Controls\SwatchPicker.g.h"

namespace moonlight_xbox_dx {

// A single entry in the SwatchPicker grid.
[Windows::UI::Xaml::Data::Bindable]
public ref class SwatchEntry sealed : Windows::UI::Xaml::Data::INotifyPropertyChanged
{
private:
    bool m_isSelected = false;
    bool m_isSystemAccent;
    Windows::UI::Color m_color;

    void Notify(Platform::String^ prop) {
        PropertyChanged(this, ref new Windows::UI::Xaml::Data::PropertyChangedEventArgs(prop));
    }

public:
    SwatchEntry(Windows::UI::Color color, bool isSystem)
        : m_color(color), m_isSystemAccent(isSystem) {}

    virtual event Windows::UI::Xaml::Data::PropertyChangedEventHandler^ PropertyChanged;

    property Windows::UI::Color DisplayColor {
        Windows::UI::Color get() { return m_color; }
    }

    property bool IsSystemAccent {
        bool get() { return m_isSystemAccent; }
    }

    property bool IsSelected {
        bool get() { return m_isSelected; }
        void set(bool v) {
            if (m_isSelected == v) return;
            m_isSelected = v;
            Notify("IsSelected");
            Notify("CheckVisibility");
        }
    }

    property Windows::UI::Xaml::Visibility CheckVisibility {
        Windows::UI::Xaml::Visibility get() {
            return m_isSelected ? Windows::UI::Xaml::Visibility::Visible
                               : Windows::UI::Xaml::Visibility::Collapsed;
        }
    }

    property Windows::UI::Xaml::Visibility SystemIndicatorVisibility {
        Windows::UI::Xaml::Visibility get() {
            return m_isSystemAccent ? Windows::UI::Xaml::Visibility::Visible
                                   : Windows::UI::Xaml::Visibility::Collapsed;
        }
    }
};

public delegate void SwatchColorChangedHandler(
    Platform::Object^ sender,
    Windows::UI::Color color,
    bool useSystemAccent);

[Windows::UI::Xaml::Data::Bindable]
public ref class SwatchPicker sealed
{
public:
    SwatchPicker();

    // The resolved color of the current selection.
    property Windows::UI::Color SelectedColor {
        Windows::UI::Color get() { return m_selectedColor; }
    }

    // True when the "System Accent" swatch is selected.
    property bool UseSystemAccent {
        bool get() { return m_useSystemAccent; }
    }

    // Replace the color palette (system swatch is always prepended automatically).
    void SetSwatches(Windows::Foundation::Collections::IVector<Windows::UI::Color>^ colors);

    // Programmatically restore a saved selection without firing ColorChanged.
    void SelectColor(Windows::UI::Color color, bool useSystem);

    event SwatchColorChangedHandler^ ColorChanged;

private:
    Windows::UI::Color m_selectedColor;
    bool m_useSystemAccent = true;
    Platform::Collections::Vector<SwatchEntry^>^ m_entries;
    SwatchEntry^ m_selectedEntry = nullptr;

    void SwatchGrid_ItemClick(Platform::Object^ sender,
                              Windows::UI::Xaml::Controls::ItemClickEventArgs^ e);
    void BuildDefaultSwatches();
    void SelectEntry(SwatchEntry^ entry, bool fireEvent);
    Windows::UI::Color GetSystemAccentColor();
};

} // namespace moonlight_xbox_dx
