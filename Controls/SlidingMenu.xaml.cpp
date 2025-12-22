#include "pch.h"
#include "SlidingMenu.xaml.h"
#include "Utils.hpp"

using namespace moonlight_xbox_dx::Controls;
using namespace Windows::UI::Xaml::Media::Animation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Core;
using namespace concurrency;

static constexpr int kAnimationDurationMs = 150;

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

void SlidingMenu::OnApplyTemplate()
{
    __super::OnApplyTemplate();

    try {
        auto backgroundEl = this->GetTemplateChild("BackgroundElement");
        if (backgroundEl != nullptr) {
            auto border = dynamic_cast<Windows::UI::Xaml::Controls::Border^>(backgroundEl);
            if (border != nullptr) {
                auto w = border->ActualWidth;
                auto h = border->ActualHeight;
                auto mw = border->MaxWidth;
                auto mh = border->MaxHeight;
                auto minw = border->MinWidth;
                auto minh = border->MinHeight;
                auto rt = dynamic_cast<Windows::UI::Xaml::Media::TranslateTransform^>(border->RenderTransform);
                double tx = 0, ty = 0;
                if (rt != nullptr) { tx = rt->X; ty = rt->Y; }
                Utils::Logf("SlidingMenu: BackgroundElement Actual=(%f,%f) Min=(%f,%f) Max=(%f,%f) RenderTransform=(%f,%f)", w, h, minw, minh, mw, mh, tx, ty);
            } else {
                Utils::Log("SlidingMenu: BackgroundElement found but not a Border");
            }
        } else {
            Utils::Log("SlidingMenu: BackgroundElement not found in template");
        }
    } catch(...) {
        Utils::Log("SlidingMenu: exception inspecting template");
    }
}

bool SlidingMenu::IsOpen::get() {
    return m_isOpen;
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
    if (m_isOpen) return;
    m_isOpen = true;
    try {
        // Ensure ShowAsync runs on the UI thread
        if (this->Dispatcher != nullptr && this->Dispatcher->HasThreadAccess) {
            auto asyncOp = this->ShowAsync();
            // When dialog is shown, run the Opened visual state to animate content in
            try { VisualStateManager::GoToState(this, "Opened", true); } catch(...) {}
            concurrency::create_task(asyncOp).then([this](Windows::UI::Xaml::Controls::ContentDialogResult result) {
                m_isOpen = false;
                try { VisualStateManager::GoToState(this, "Closed", false); } catch(...) {}
            });
        } else {
            // Dispatch to UI thread; use a weak ref to avoid lifetime issues
            Platform::WeakReference weakThis(this);
            auto disp = this->Dispatcher;
            if (disp == nullptr) {
                try { auto coreView = Windows::ApplicationModel::Core::CoreApplication::MainView; if (coreView != nullptr && coreView->CoreWindow != nullptr) disp = coreView->CoreWindow->Dispatcher; } catch(...) { disp = nullptr; }
            }
            if (disp != nullptr) {
                disp->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([weakThis]() {
                    auto that = weakThis.Resolve<SlidingMenu>();
                    if (that == nullptr) return;
                    try {
                        auto asyncOp = that->ShowAsync();
                        try { VisualStateManager::GoToState(that, "Opened", true); } catch(...) {}
                        concurrency::create_task(asyncOp).then([that](Windows::UI::Xaml::Controls::ContentDialogResult result) {
                            that->m_isOpen = false;
                            try { VisualStateManager::GoToState(that, "Closed", false); } catch(...) {}
                        });
                    } catch(...) {
                        that->m_isOpen = false;
                    }
                }));
            } else {
                // No dispatcher available; fall back to calling ShowAsync directly and guarding
                try {
                    auto asyncOp = this->ShowAsync();
                    try { VisualStateManager::GoToState(this, "Opened", true); } catch(...) {}
                    concurrency::create_task(asyncOp).then([this](Windows::UI::Xaml::Controls::ContentDialogResult result) {
                        m_isOpen = false;
                        try { VisualStateManager::GoToState(this, "Closed", false); } catch(...) {}
                    });
                } catch(...) {
                    m_isOpen = false;
                }
            }
        }
    } catch(...) {
        m_isOpen = false;
    }
}

void SlidingMenu::Close()
{
	if (!m_isOpen) return;

    try {
        // animate closed state then hide
        try { VisualStateManager::GoToState(this, "Closed", true); } catch(...) {}
        this->Hide();
    } catch(...) {}
    m_isOpen = false;
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
