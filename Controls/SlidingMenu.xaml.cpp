#include "pch.h"
#include "SlidingMenu.xaml.h"
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Animation.h>

using namespace moonlight_xbox_dx::Controls;
using namespace Windows::UI::Xaml::Media::Animation;
using namespace Windows::UI::Xaml;

SlidingMenu::SlidingMenu()
{
    InitializeComponent();
    auto app = dynamic_cast<App^>(Application::Current);
    // Initialize collections: GlobalItems (from App if available), PageItems (empty), and Items (combined)
    if (app != nullptr && app->GlobalMenuItems != nullptr) {
        GlobalItems = app->GlobalMenuItems;
    }
    else {
        GlobalItems = ref new Platform::Collections::Vector<moonlight_xbox_dx::MenuItem^>();
    }
    PageItems = ref new Platform::Collections::Vector<moonlight_xbox_dx::MenuItem^>();

    // Bind GlobalItems and PageItems to separate ItemsControls in XAML
    try { GlobalItemsList->ItemsSource = GlobalItems; } catch(...) {}
    try { PageItemsList->ItemsSource = PageItems; } catch(...) {}
}

void SlidingMenu::AddPageItem(moonlight_xbox_dx::MenuItem^ item) {
    if (item == nullptr) return;
    PageItems->Append(item);
}

void SlidingMenu::ClearPageItems() {
    PageItems->Clear();
}

void SlidingMenu::Open()
{
    auto da = ref new DoubleAnimation();
    da->To = ref new Platform::Box<double>(0.0);
    Windows::Foundation::TimeSpan span;
    span.Duration = 1500000; // 0.2s in 100ns units
    da->Duration = span;
    Storyboard::SetTarget(da, SlideTransform);
    Storyboard::SetTargetProperty(da, "X");
    // fade the overlay in (animate Opacity from 0 -> 1)
    auto oa = ref new DoubleAnimation();
    oa->From = ref new Platform::Box<double>(0.0);
    oa->To = ref new Platform::Box<double>(1.0);
    oa->Duration = span;
    Storyboard::SetTarget(oa, Overlay);
    Storyboard::SetTargetProperty(oa, "Opacity");

    auto sb = ref new Storyboard();
    sb->Children->Append(da);
    sb->Children->Append(oa);
    // ensure overlay is visible and receives input before animating
    Overlay->Opacity = 0.0;
    SetOverlayActive(true);
    sb->Begin();
}

void SlidingMenu::Close()
{
    auto da = ref new DoubleAnimation();
    da->To = ref new Platform::Box<double>(-320.0);
    Windows::Foundation::TimeSpan span2;
    span2.Duration = 2000000;
    da->Duration = span2;
    Storyboard::SetTarget(da, SlideTransform);
    Storyboard::SetTargetProperty(da, "X");
    // fade overlay out (Opacity 1 -> 0)
    auto oa = ref new DoubleAnimation();
    oa->To = ref new Platform::Box<double>(0.0);
    oa->Duration = span2;
    Storyboard::SetTarget(oa, Overlay);
    Storyboard::SetTargetProperty(oa, "Opacity");

    auto sb = ref new Storyboard();
    sb->Children->Append(da);
    sb->Children->Append(oa);
    // disable overlay after closing animation completes
    sb->Completed += ref new Windows::Foundation::EventHandler<Platform::Object^>([this](Platform::Object^ s, Platform::Object^ e) { SetOverlayActive(false); });
    sb->Begin();
}

void SlidingMenu::SetOverlayActive(bool active)
{
    if (active) {
        Overlay->Visibility = Windows::UI::Xaml::Visibility::Visible;
        Overlay->IsHitTestVisible = true;
        auto rootWindow = Windows::UI::Xaml::Window::Current;
        // set focus to the control so it receives input
        auto focused = this->Focus(Windows::UI::Xaml::FocusState::Programmatic);
        (void)focused;
    }
    else {
        Overlay->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
        Overlay->IsHitTestVisible = false;
    }
}

void SlidingMenu::OnMenuItemClicked(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    auto btn = safe_cast<Windows::UI::Xaml::Controls::Button^>(sender);
    auto item = safe_cast<moonlight_xbox_dx::MenuItem^>(btn->DataContext);
    if (item != nullptr) {
        // If the item has a per-item ClickAction, invoke it first
        try {
            if (item->ClickAction != nullptr) {
                try { item->ClickAction(item, nullptr); } catch(...) {}
            }
        } catch(...) {}
        // raise the event for subscribers (sender is this control)
        try { MenuItemInvoked(this, item); } catch(...) {}
    }
    // close menu after click
    Close();
}

void SlidingMenu::Overlay_Tapped(Platform::Object^ sender, Windows::UI::Xaml::Input::TappedRoutedEventArgs^ e)
{
    Close();
}
