#pragma once

#include "Controls\TabsLayout.g.h"

namespace moonlight_xbox_dx
{
    public ref class TabsLayout sealed
    {
    public:
        TabsLayout();

        property Windows::UI::Xaml::UIElement^ LeftContent {
            Windows::UI::Xaml::UIElement^ get();
            void set(Windows::UI::Xaml::UIElement^ value);
        }

        property Windows::UI::Xaml::UIElement^ TabsContent {
            Windows::UI::Xaml::UIElement^ get();
            void set(Windows::UI::Xaml::UIElement^ value);
        }

        // Attached property to associate a tab button with a panel name
        static void SetTargetPanelName(Windows::UI::Xaml::UIElement^ element, Platform::String^ value);
        static Platform::String^ GetTargetPanelName(Windows::UI::Xaml::UIElement^ element);

        // Attached property to associate a tab button with a direct panel reference
        static void SetTargetPanel(Windows::UI::Xaml::UIElement^ element, Windows::UI::Xaml::UIElement^ value);
        static Windows::UI::Xaml::UIElement^ GetTargetPanel(Windows::UI::Xaml::UIElement^ element);

    private:
        void OnLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void OnUnloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void HookUpTabs();
        void UnhookTabs();
        void SelectTabByName(Platform::String^ panelName);
        void OnTabButtonClick(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void OnTabButtonGotFocus(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);

        Windows::Foundation::Collections::IVector<Windows::UI::Xaml::Controls::Button^>^ m_tabButtons;
        Windows::Foundation::Collections::IVector<long long>^ m_clickTokens;
        Windows::Foundation::Collections::IVector<long long>^ m_focusTokens;
        Windows::UI::Xaml::Controls::Button^ m_selectedButton;

        // Static DependencyProperty backing fields (private to satisfy C++/CX rules)
        static Windows::UI::Xaml::DependencyProperty^ TargetPanelNameProperty;
        static Windows::UI::Xaml::DependencyProperty^ TargetPanelProperty;
        // Attached IsSelected property
        static void SetIsSelected(Windows::UI::Xaml::UIElement^ element, bool value);
        static bool GetIsSelected(Windows::UI::Xaml::UIElement^ element);
        static Windows::UI::Xaml::DependencyProperty^ IsSelectedProperty;
    };
}
