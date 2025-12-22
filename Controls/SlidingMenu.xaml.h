#pragma once
#include "Controls\SlidingMenu.g.h"
#include "Common\MenuItem.h"

namespace moonlight_xbox_dx::Controls
{
    public ref class SlidingMenu sealed
    {
    public:
        SlidingMenu();

        // Global items shared across pages (backed by App::GlobalMenuItems when available)
        property Windows::Foundation::Collections::IObservableVector<moonlight_xbox_dx::MenuItem^>^ GlobalItems;

        // Page-specific items that should be cleared when navigating away
        property Windows::Foundation::Collections::IObservableVector<moonlight_xbox_dx::MenuItem^>^ PageItems;

        // Add and clear page items
        void AddPageItem(moonlight_xbox_dx::MenuItem^ item);
        void ClearPageItems();

        void Open();
        void Close();
        property bool IsOpen { bool get(); }
        event Windows::Foundation::TypedEventHandler<Platform::Object^, moonlight_xbox_dx::MenuItem^>^ MenuItemInvoked;
    private:
        Platform::Object^ m_prevFocusedElement = nullptr;
    protected:
        virtual void OnApplyTemplate() override;
    private:
        void OnMenuItemClicked(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void Overlay_Tapped(Platform::Object^ sender, Windows::UI::Xaml::Input::TappedRoutedEventArgs^ e);
    private:
        bool m_isOpen = false;
    };
}
