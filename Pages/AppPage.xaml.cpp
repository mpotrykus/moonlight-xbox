#include "pch.h"
#include "AppPage.Xaml.h"
#include "AppPage.Helpers.h"
#include "Controls\SlidingMenu.xaml.h"
#include "Common\ModalDialog.xaml.h"
#include "HostSettingsPage.xaml.h"
#include "StreamPage.xaml.h"
#include "Utils.hpp"
#include "Common\XamlHelper.h"
#include <algorithm>
#include <cwctype>

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace concurrency;

namespace moonlight_xbox_dx {

// ── AppPage::GetLeftMenu ──────────────────────────────────────────────────────

Controls::SlidingMenu^ AppPage::GetLeftMenu() {
    try {
        if (m_leftMenu == nullptr) m_leftMenu = ref new Controls::SlidingMenu();
        return m_leftMenu;
    } catch(...) { return nullptr; }
}

// ── AppPage::AppPage (constructor) ────────────────────────────────────────────

AppPage::AppPage() {
    InitializeComponent();
    Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()
        ->SetDesiredBoundsMode(Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow);

    // Zero all event tokens
    try {
        m_apps_changed_token.Value = m_back_cookie.Value = m_keydown_cookie.Value = 0;
        m_rendering_token.Value = m_scrollviewer_viewchanged_token.Value = m_layoutUpdated_token.Value = 0;
        m_appsgird_selection_token.Value = m_appsgird_itemclick_token.Value = 0;
        m_appsgird_righttapped_token.Value = m_appsgird_sizechanged_token.Value = 0;
        m_appsgird_loaded_token.Value = m_appsgird_unloaded_token.Value = m_appsgird_ccc_token.Value = 0;
    } catch(...) {}

    // Page lifecycle
    {
        auto weakThis = WeakReference(this);
        this->Loaded += ref new RoutedEventHandler([weakThis](Platform::Object^ s, RoutedEventArgs^ e) {
            auto that = weakThis.Resolve<AppPage>();
            if (that == nullptr) return;
            try { Utils::Log("AppPage: Loaded\n"); that->OnLoaded(s, e); } catch(...) {}
        });
    }
    {
        auto weakThis = WeakReference(this);
        this->Unloaded += ref new RoutedEventHandler([weakThis](Platform::Object^ s, RoutedEventArgs^ e) {
            auto that = weakThis.Resolve<AppPage>();
            if (that == nullptr) return;
            try { Utils::Log("AppPage: Unloaded\n"); that->OnUnloaded(s, e); } catch(...) {}
        });
    }
    {
        auto weakThis = WeakReference(this);
        this->SizeChanged += ref new SizeChangedEventHandler([weakThis](Platform::Object^ s, SizeChangedEventArgs^ e) {
            auto that = weakThis.Resolve<AppPage>();
            if (that) try { that->PageRoot_SizeChanged(s, e); } catch(...) {}
        });
    }

    // AppsGrid event wiring
    try {
        if (this->AppsGrid != nullptr) {
            auto weakThis = WeakReference(this);
            m_appsgird_selection_token = this->AppsGrid->SelectionChanged +=
                ref new SelectionChangedEventHandler([weakThis](Platform::Object^ s, SelectionChangedEventArgs^ e) {
                    auto that = weakThis.Resolve<AppPage>(); if (that) try { that->AppsGrid_SelectionChanged(s, e); } catch(...) {}
                });
            m_appsgird_itemclick_token = this->AppsGrid->ItemClick +=
                ref new ItemClickEventHandler([weakThis](Platform::Object^ s, ItemClickEventArgs^ e) {
                    auto that = weakThis.Resolve<AppPage>(); if (that) try { that->AppsGrid_ItemClick(s, e); } catch(...) {}
                });
            m_appsgird_righttapped_token = this->AppsGrid->RightTapped +=
                ref new RightTappedEventHandler([weakThis](Platform::Object^ s, RightTappedRoutedEventArgs^ e) {
                    auto that = weakThis.Resolve<AppPage>(); if (that) try { that->AppsGrid_RightTapped(s, e); } catch(...) {}
                });
            m_appsgird_sizechanged_token = this->AppsGrid->SizeChanged +=
                ref new SizeChangedEventHandler([weakThis](Platform::Object^ s, SizeChangedEventArgs^ e) {
                    auto that = weakThis.Resolve<AppPage>(); if (that) try { that->AppsGrid_SizeChanged(s, e); } catch(...) {}
                });
            m_appsgird_loaded_token = this->AppsGrid->Loaded +=
                ref new RoutedEventHandler([weakThis](Platform::Object^ s, RoutedEventArgs^ e) {
                    auto that = weakThis.Resolve<AppPage>(); if (that) try { that->AppsGrid_Loaded(s, e); } catch(...) {}
                });
            m_appsgird_unloaded_token = this->AppsGrid->Unloaded +=
                ref new RoutedEventHandler([weakThis](Platform::Object^ s, RoutedEventArgs^ e) {
                    auto that = weakThis.Resolve<AppPage>(); if (that) try { that->AppsGrid_Unloaded(s, e); } catch(...) {}
                });
            m_appsgird_ccc_token = this->AppsGrid->ContainerContentChanging +=
                ref new TypedEventHandler<ListViewBase^, ContainerContentChangingEventArgs^>(
                    [weakThis](ListViewBase^ s, ContainerContentChangingEventArgs^ args) {
                        auto that = weakThis.Resolve<AppPage>(); if (that) try { that->AppsGrid_ContainerContentChanging(s, args); } catch(...) {}
                    });
            m_layoutUpdated_token = this->AppsGrid->LayoutUpdated +=
                ref new EventHandler<Platform::Object^>([weakThis](Platform::Object^ s, Platform::Object^ e) {
                    auto that = weakThis.Resolve<AppPage>(); if (that) try { that->AppsGrid_LayoutUpdated(s, nullptr); } catch(...) {}
                });
        }
    } catch(...) {}

    // Sliding menu
    try {
        auto lm = this->GetLeftMenu();
        if (lm != nullptr) {
            auto weakThis = WeakReference(this);
            lm->AddPageItem(ref new MenuItem(
                ref new Platform::String(L"Sample Action"),
                ref new Platform::String(L""),
                ref new EventHandler<Platform::Object^>([weakThis](Platform::Object^, Platform::Object^) {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that) try { that->OnSampleActionClicked(); } catch(...) {}
                })));
        }
    } catch(...) {}

    m_compositionReady = false;
    m_filteredApps = ref new Platform::Collections::Vector<MoonlightApp^>();
}

// ── AppPage::OnNavigatedTo ────────────────────────────────────────────────────

void AppPage::OnNavigatedTo(NavigationEventArgs^ e) {
    MoonlightHost^ mhost = dynamic_cast<MoonlightHost^>(e->Parameter);
    if (mhost == nullptr) return;
    host = mhost;
    host->UpdateHostInfo(true);
    host->UpdateApps();

    try {
        auto obs = dynamic_cast<Windows::Foundation::Collections::IObservableVector<MoonlightApp^>^>(host->Apps);
        if (obs != nullptr) {
            Platform::WeakReference weakThis(this);
            m_apps_changed_token = obs->VectorChanged +=
                ref new Windows::Foundation::Collections::VectorChangedEventHandler<MoonlightApp^>(
                [weakThis](Windows::Foundation::Collections::IObservableVector<MoonlightApp^>^ sender,
                           Windows::Foundation::Collections::IVectorChangedEventArgs^ args) {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that) try { that->OnHostAppsChanged(sender, args); } catch(...) {}
                });
        }
    } catch(...) {}

    ApplyAppFilter(nullptr);

    if (host->AutostartID >= 0 && GetApplicationState()->shouldAutoConnect) {
        GetApplicationState()->shouldAutoConnect = false;
        CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
            CoreDispatcherPriority::High, ref new DispatchedHandler([this]() {
                this->Connect(host->AutostartID);
            }));
    }
    GetApplicationState()->shouldAutoConnect = false;
}

// ── AppPage::ApplyAppFilter ───────────────────────────────────────────────────

bool AppPage::ApplyAppFilter(Platform::String^ filter) {
    auto host = this->Host;
    auto vec  = this->m_filteredApps;
    if (vec == nullptr) return false;

    if (host == nullptr || host->Apps == nullptr) { vec->Clear(); return false; }

    auto newResults = ref new Platform::Collections::Vector<MoonlightApp^>();
    bool empty = (filter == nullptr || filter->Length() == 0);
    std::wstring fw;
    if (!empty) {
        std::string fstr = Utils::PlatformStringToStdString(filter);
        fw = Utils::NarrowToWideString(fstr);
        std::transform(fw.begin(), fw.end(), fw.begin(), ::towlower);
    }

    for (unsigned int i = 0; i < host->Apps->Size; ++i) {
        auto app = host->Apps->GetAt(i);
        if (app == nullptr) continue;
        if (empty) {
            newResults->Append(app);
        } else {
            std::string nstr = Utils::PlatformStringToStdString(app->Name != nullptr ? app->Name : ref new Platform::String(L""));
            std::wstring namew = Utils::NarrowToWideString(nstr);
            std::transform(namew.begin(), namew.end(), namew.begin(), ::towlower);
            if (namew.find(fw) != std::wstring::npos) newResults->Append(app);
        }
    }

    // Skip update if results are identical
    bool identical = false;
    try {
        if (vec->Size == newResults->Size) {
            identical = true;
            for (unsigned int i = 0; i < vec->Size; ++i) {
                auto a = vec->GetAt(i), b = newResults->GetAt(i);
                if ((a != nullptr ? a->Id : -1) != (b != nullptr ? b->Id : -1)) { identical = false; break; }
            }
        }
    } catch(...) { identical = false; }
    if (identical) return true;

    vec->Clear();
    for (unsigned int i = 0; i < newResults->Size; ++i) vec->Append(newResults->GetAt(i));

    // Update empty-state message on UI thread
    try {
        bool emptyResults = (vec->Size == 0);
        auto weakThis = WeakReference(this);
        this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
            ref new DispatchedHandler([weakThis, emptyResults]() {
            try {
                auto that = weakThis.Resolve<AppPage>();
                if (that == nullptr) return;
                if (that->NoAppsMessage != nullptr)
                    that->NoAppsMessage->Visibility = emptyResults ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
                if (emptyResults && that->SelectedAppText != nullptr && that->SelectedAppBox != nullptr) {
                    auto sb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(
                        that->Resources->Lookup(ref new Platform::String(L"HideSelectedAppStoryboard")));
                    if (sb != nullptr) sb->Begin();
                    else {
                        SetElementOpacityImmediate(that->SelectedAppBox,  0.0f);
                        SetElementOpacityImmediate(that->SelectedAppText, 0.0f);
                    }
                }
            } catch(...) {}
        }));
    } catch(...) {}

    // Promote first result
    try {
        if (vec->Size > 0 && this->AppsGrid != nullptr) {
            auto weakThis = WeakReference(this);
            bool isGrid = this->m_isGridLayout;
            auto svi = this->m_scrollViewer;
            auto appsGrid = this->AppsGrid;
            this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                ref new DispatchedHandler([weakThis, appsGrid, isGrid, svi]() {
                try {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that == nullptr || that->AppsGrid == nullptr) return;
                    if (that->AppsGrid->Items == nullptr || that->AppsGrid->Items->Size == 0) return;

                    // Clear visuals on all realized containers
                    for (unsigned int i = 0; i < that->AppsGrid->Items->Size; ++i) {
                        auto c = dynamic_cast<ListViewItem^>(that->AppsGrid->ContainerFromIndex(i));
                        if (c) that->ApplyVisualsToContainer(c, false);
                    }

                    auto firstItem = that->AppsGrid->Items->GetAt(0);
                    if (firstItem == nullptr) return;
                    that->AppsGrid->ScrollIntoView(firstItem);
                    auto container = dynamic_cast<ListViewItem^>(that->AppsGrid->ContainerFromItem(firstItem));

                    if (container != nullptr) {
                        that->ApplyVisualsToContainer(container, true);
                    }

                    // Update SelectedApp text
                    try {
                        auto selApp = dynamic_cast<MoonlightApp^>(firstItem);
                        if (selApp != nullptr && that->SelectedAppText != nullptr && that->SelectedAppBox != nullptr) {
                            try { that->SelectedAppText->Text = selApp->Name != nullptr ? selApp->Name : ref new Platform::String(L""); }
                            catch(...) { that->SelectedAppText->Text = selApp->Name; }
							that->SelectedAppBox->Visibility = Windows::UI::Xaml::Visibility::Visible;
							that->SelectedAppText->Visibility = Windows::UI::Xaml::Visibility::Visible;
                            that->SelectedAppText->Foreground  = ref new SolidColorBrush(Windows::UI::Colors::White);
                            SetElementOpacityImmediate(that->SelectedAppBox,  0.0f);
                            SetElementOpacityImmediate(that->SelectedAppText, 0.0f);
                            auto sb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(
                                that->Resources->Lookup(ref new Platform::String(L"ShowSelectedAppStoryboard")));
                            if (sb != nullptr) sb->Begin();
                        }
                    } catch(...) {}

                    // Select and center the first item; CenterSelectedItem retries if container not yet realized.
                    that->AppsGrid->SelectedIndex = 0;
                    that->CenterSelectedItem(4, true);
                } catch(...) {}
            }));
        }
    } catch(...) {}
    return false;
}

// ── AppPage::OnHostAppsChanged ────────────────────────────────────────────────

void AppPage::OnHostAppsChanged(
    Windows::Foundation::Collections::IObservableVector<MoonlightApp^>^,
    Windows::Foundation::Collections::IVectorChangedEventArgs^)
{
    try {
        auto weakThis = WeakReference(this);
        this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
            ref new DispatchedHandler([weakThis]() {
            try {
                auto that = weakThis.Resolve<AppPage>();
                if (that) that->ApplyAppFilter(that->SearchBox != nullptr ? that->SearchBox->Text : nullptr);
            } catch(...) {}
        }));
    } catch(...) {}
}

// ── AppPage::StartBgPanAnimation ──────────────────────────────────────────────

void AppPage::StartBgPanAnimation() {
    using namespace Windows::UI::Xaml::Media;
    using namespace Windows::UI::Xaml::Media::Animation;

    try {
        if (PageBackgroundImage == nullptr) return;

        auto transform = ref new TranslateTransform();
        PageBackgroundImage->RenderTransform = transform;

        auto sb = ref new Storyboard();
        sb->RepeatBehavior = RepeatBehaviorHelper::Forever;
        sb->AutoReverse = true;

        Windows::UI::Xaml::Duration dur(TimeSpan{ kBgPanDurationSec * 10000000LL });

        auto ease = ref new SineEase();
        ease->EasingMode = EasingMode::EaseInOut;

        auto animY = ref new DoubleAnimation();
        animY->To = ref new Platform::Box<double>(120.0);
        animY->Duration = dur;
        animY->EasingFunction = ease;
        Storyboard::SetTarget(animY, transform);
        Storyboard::SetTargetProperty(animY, ref new Platform::String(L"Y"));
        sb->Children->Append(animY);

        sb->Begin();
        m_bgPanStoryboard = sb;
    } catch(...) {}
}

// ── AppPage::OnLoaded ─────────────────────────────────────────────────────────

void AppPage::OnLoaded(Platform::Object^, RoutedEventArgs^) {
    Platform::WeakReference weakThis(this);
    m_back_cookie = Windows::UI::Core::SystemNavigationManager::GetForCurrentView()->BackRequested +=
        ref new EventHandler<BackRequestedEventArgs^>([weakThis](Platform::Object^ s, BackRequestedEventArgs^ args) {
            auto that = weakThis.Resolve<AppPage>();
            if (that) that->OnBackRequested(s, args);
        });

    try {
        auto window = CoreApplication::MainView->CoreWindow;
        if (window != nullptr) {
            m_keydown_cookie = window->KeyDown +=
                ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>([weakThis](CoreWindow^ s, KeyEventArgs^ args) {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that) that->OnGamepadKeyDown(s, args);
                });
        }
    } catch(...) {}

    try {
        m_rendering_token = Windows::UI::Xaml::Media::CompositionTarget::Rendering +=
            ref new EventHandler<Object^>([weakThis](Platform::Object^ s, Platform::Object^ args) {
                auto that = weakThis.Resolve<AppPage>();
                if (that) that->OnFirstRender(s, args);
            });
    } catch(...) {}

    StartBgPanAnimation();
}

// ── AppPage::OnUnloaded ───────────────────────────────────────────────────────

void AppPage::OnUnloaded(Platform::Object^, RoutedEventArgs^) {
    try { Windows::UI::Core::SystemNavigationManager::GetForCurrentView()->BackRequested -= m_back_cookie; } catch(...) {}
    continueAppFetch.store(false);

    try {
        auto window = CoreApplication::MainView->CoreWindow;
        if (window != nullptr) try { window->KeyDown -= m_keydown_cookie; } catch(...) {}
    } catch(...) {}

    try {
        if (this->host != nullptr && this->host->Apps != nullptr) {
            auto obs = dynamic_cast<Windows::Foundation::Collections::IObservableVector<MoonlightApp^>^>(this->host->Apps);
            if (obs != nullptr) try { obs->VectorChanged -= m_apps_changed_token; } catch(...) {}
        }
    } catch(...) {}

    try { Windows::UI::Xaml::Media::CompositionTarget::Rendering -= m_rendering_token; } catch(...) {}

    try { if (m_bgPanStoryboard != nullptr) { m_bgPanStoryboard->Stop(); m_bgPanStoryboard = nullptr; } } catch(...) {}

    try {
        if (m_scrollViewer != nullptr) try { m_scrollViewer->ViewChanged -= m_scrollviewer_viewchanged_token; } catch(...) {}
    } catch(...) {}

    try {
        if (this->AppsGrid != nullptr) {
            try { this->AppsGrid->SelectionChanged -= m_appsgird_selection_token; } catch(...) {}
            try { this->AppsGrid->ItemClick        -= m_appsgird_itemclick_token;  } catch(...) {}
            try { this->AppsGrid->RightTapped      -= m_appsgird_righttapped_token; } catch(...) {}
            try { this->AppsGrid->SizeChanged      -= m_appsgird_sizechanged_token; } catch(...) {}
            try { this->AppsGrid->Loaded                   -= m_appsgird_loaded_token;   } catch(...) {}
            try { this->AppsGrid->Unloaded                 -= m_appsgird_unloaded_token; } catch(...) {}
            try { this->AppsGrid->ContainerContentChanging -= m_appsgird_ccc_token;      } catch(...) {}
            try { this->AppsGrid->LayoutUpdated            -= m_layoutUpdated_token;     } catch(...) {}
        }
    } catch(...) {}
}

// ── AppPage::OnFirstRender ────────────────────────────────────────────────────

void AppPage::OnFirstRender(Object^, Object^) {
    Utils::Log("AppPage::OnFirstRender\n");
    try { Windows::UI::Xaml::Media::CompositionTarget::Rendering -= m_rendering_token; } catch(...) {}
    try {
        AppsGrid->SelectedIndex = this->AppsGrid->SelectedIndex > -1 ? this->AppsGrid->SelectedIndex : 0;
        AppsGrid_SelectionChanged(this->AppsGrid, nullptr);
    } catch(...) {}
}

// ── AppPage::OnBackRequested ──────────────────────────────────────────────────

void AppPage::OnBackRequested(Platform::Object^, BackRequestedEventArgs^ args) {
    try {
        auto lm = this->GetLeftMenu();
        if (lm != nullptr && lm->IsOpen) { lm->Close(); args->Handled = true; return; }
    } catch(...) {}

    if (this->Frame->CanGoBack) { this->Frame->GoBack(); args->Handled = true; }
}

// ── AppPage::OnGamepadKeyDown ─────────────────────────────────────────────────

void AppPage::OnGamepadKeyDown(CoreWindow^, KeyEventArgs^ args) {
    try {
        using namespace Windows::System;
        auto key = args->VirtualKey;

        if (key == VirtualKey::GamepadY || key == VirtualKey::Y) {
            auto weakThis = WeakReference(this);
            this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                ref new DispatchedHandler([weakThis]() {
                    auto that = weakThis.Resolve<AppPage>();
				                           if (that) try {
						                           that->SearchBox->Focus(Windows::UI::Xaml::FocusState::Programmatic);
					                           } catch (...) {
					                           }
                }));
            args->Handled = true;
        }

        if (key == VirtualKey::B) {
            try {
                auto lm = this->GetLeftMenu();
                if (lm != nullptr && lm->IsOpen) { lm->Close(); args->Handled = true; }
                else if (this->Frame->CanGoBack) { this->Frame->GoBack(); args->Handled = true; }
            } catch(...) {}
        }

        if (key == VirtualKey::GamepadX || key == VirtualKey::X) {
            auto weakThis = WeakReference(this);
            this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                ref new DispatchedHandler([weakThis]() {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that == nullptr) return;
                    try {
                        bool newState = !that->m_isGridLayout;
                        if (that->LayoutToggleButton) that->LayoutToggleButton->IsChecked = newState;
                        that->LayoutToggleButton_Click(that->LayoutToggleButton, nullptr);
                    } catch(...) {}
                }));
            args->Handled = true;
        }

        if (!m_isGridLayout &&
            (key == VirtualKey::GamepadDPadRight || key == VirtualKey::GamepadLeftThumbstickRight)) {
            if (this->AppsGrid) {
                int target = (int)this->AppsGrid->SelectedIndex + 1;
                int count  = (int)this->AppsGrid->Items->Size;
                if (target > 0 && target < count)
                    try { this->AppsGrid->SelectedIndex = target; } catch(...) {}
            }
            args->Handled = true;
        }

        if (!m_isGridLayout &&
            (key == VirtualKey::GamepadDPadLeft || key == VirtualKey::GamepadLeftThumbstickLeft)) {
            if (this->AppsGrid) {
                int target = (int)this->AppsGrid->SelectedIndex - 1;
                if (target >= 0)
                    try { this->AppsGrid->SelectedIndex = target; } catch(...) {}
            }
            args->Handled = true;
        }

    } catch(...) {}
}

// ── AppPage::SearchBox_TextChanged ────────────────────────────────────────────

void AppPage::SearchBox_TextChanged(Platform::Object^ sender, TextChangedEventArgs^) {
    try {
        auto tb = dynamic_cast<TextBox^>(sender);
        if (tb == nullptr) return;
        bool collectionChanged = !ApplyAppFilter(tb->Text);
        if (collectionChanged) {
            this->AppsGrid->SelectedIndex = this->AppsGrid->SelectedIndex > -1 ? this->AppsGrid->SelectedIndex : 0;
            this->AppsGrid_SelectionChanged(this->AppsGrid, nullptr);
        }
    } catch(...) {}
}

// ── AppPage::LayoutToggleButton_Click ─────────────────────────────────────────

void AppPage::LayoutToggleButton_Click(Platform::Object^ sender, RoutedEventArgs^) {
    try {
        auto toggle = dynamic_cast<Windows::UI::Xaml::Controls::Primitives::ToggleButton^>(sender);
        bool wantGrid = toggle != nullptr && toggle->IsChecked != nullptr && toggle->IsChecked->Value;
        if (m_isGridLayout == wantGrid) return;
        m_isGridLayout = wantGrid;

        if (this->AppsGrid != nullptr) {
            auto res = this->Resources;
            if (m_isGridLayout) {
                if (res != nullptr) {
                    auto panel = dynamic_cast<ItemsPanelTemplate^>(res->Lookup(ref new Platform::String(L"GridItemsPanelTemplate")));
                    if (panel != nullptr) this->AppsGrid->ItemsPanel = panel;
                }
                this->AppsGrid->SetValue(ScrollViewer::HorizontalScrollModeProperty, ScrollMode::Disabled);
                this->AppsGrid->SetValue(ScrollViewer::VerticalScrollModeProperty,   ScrollMode::Enabled);
                this->AppsGrid->SetValue(ScrollViewer::VerticalScrollBarVisibilityProperty, ScrollBarVisibility::Auto);
                VisualStateManager::GoToState(this, "GridLayout", false);
            } else {
                if (res != nullptr) {
                    auto panel = dynamic_cast<ItemsPanelTemplate^>(res->Lookup(ref new Platform::String(L"HorizontalItemsPanelTemplate")));
                    if (panel != nullptr) this->AppsGrid->ItemsPanel = panel;
                }
                this->AppsGrid->SetValue(ScrollViewer::HorizontalScrollModeProperty, ScrollMode::Enabled);
                this->AppsGrid->SetValue(ScrollViewer::VerticalScrollModeProperty,   ScrollMode::Disabled);
                this->AppsGrid->SetValue(ScrollViewer::VerticalScrollBarVisibilityProperty, ScrollBarVisibility::Disabled);
                VisualStateManager::GoToState(this, "ListLayout", false);
            }

            m_scrollViewer = nullptr;

            for (int i = 0; i < (int)this->Host->Apps->Size; ++i) {
                auto app = this->Host->Apps->GetAt(i);
                app->BlurredImage = nullptr;
                app->ReflectionImage = nullptr;
                app->GlowImage = nullptr;
                this->Host->Apps->SetAt(i, app);
            }

            this->UpdateItemHeights();

            if (this->AppsGrid->SelectedIndex >= 0) {
                this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                    ref new DispatchedHandler([this]() {
                        AppsGrid_SelectionChanged(this->AppsGrid, nullptr);
                    }));
            }
        }
    } catch(...) {}
}

// ── AppPage::AppsGrid_Loaded ──────────────────────────────────────────────────

void AppPage::AppsGrid_Loaded(Platform::Object^, RoutedEventArgs^) {
    try {
        Utils::Log("AppsGrid_Loaded\n");
        if (m_scrollViewer == nullptr) m_scrollViewer = FindScrollViewer(this->AppsGrid);

        if (m_scrollViewer != nullptr) {
            Platform::WeakReference weakThis(this);
            m_scrollviewer_viewchanged_token = m_scrollViewer->ViewChanged +=
                ref new EventHandler<ScrollViewerViewChangedEventArgs^>([weakThis](Platform::Object^ s, ScrollViewerViewChangedEventArgs^ args) {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that) try { that->OnScrollViewerViewChanged(s, args); } catch(...) {}
                });
        }

        try {
            auto weakThis = WeakReference(this);
            Windows::UI::Xaml::Media::CompositionTarget::Rendering +=
                ref new EventHandler<Object^>([weakThis](Object^, Object^) {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that) that->m_compositionReady = true;
                });
        } catch(...) {}
    } catch(...) {}
}

// ── Button click handlers ─────────────────────────────────────────────────────

void AppPage::backButton_Click(Platform::Object^, RoutedEventArgs^) {
    this->Frame->GoBack();
}

void AppPage::settingsButton_Click(Platform::Object^, RoutedEventArgs^) {
    try {
        auto lm = this->GetLeftMenu();
        if (lm != nullptr) {
            try { lm->Title = ref new Platform::String(L"Settings"); } catch(...) {}
            lm->Open();
            return;
        }
    } catch(...) {}
    this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSettingsPage::typeid), Host);
}

void AppPage::Page_RightTapped(Platform::Object^, RightTappedRoutedEventArgs^ e) {
    try {
        auto lm = this->GetLeftMenu();
        if (lm != nullptr) {
            try {
                lm->Title = (this->currentApp != nullptr && this->currentApp->Name != nullptr)
                    ? this->currentApp->Name : ref new Platform::String(L"");
            } catch(...) {}
            lm->Open();
        }
        e->Handled = true;
    } catch(...) {}
}

void AppPage::helpButton_Click(Platform::Object^, RoutedEventArgs^) {
    auto path = ref new Platform::String(L"/Pages/HelpDialog.xaml");
    create_task(XamlHelper::LoadXamlFileAsStringAsync(path)).then([this](Platform::String^ xaml) {
        try { ModalDialog::ShowOnceAsyncWithXaml(xaml, nullptr, Utils::StringFromStdString("OK")); } catch(...) {}
    });
}

void AppPage::OnSampleActionClicked() {
    try {
        if (this->SelectedAppText != nullptr) {
            this->SelectedAppText->Text = ref new Platform::String(L"SAMPLE ACTION TRIGGERED");
            auto sb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(
                this->Resources->Lookup(ref new Platform::String(L"ShowSelectedAppStoryboard")));
            if (sb != nullptr) sb->Begin();
        }
    } catch(...) {}
}

// ── Empty event-handler stubs (registered, no current logic needed) ───────────

void AppPage::AppsGrid_SizeChanged(Platform::Object^, SizeChangedEventArgs^) {}
void AppPage::AppsGrid_Unloaded(Platform::Object^, RoutedEventArgs^) {}
void AppPage::AppsGrid_LayoutUpdated(Platform::Object^, RoutedEventArgs^) {}
void AppPage::PageRoot_SizeChanged(Platform::Object^, SizeChangedEventArgs^) {}
void AppPage::OnScrollViewerViewChanged(Platform::Object^, ScrollViewerViewChangedEventArgs^ args) {
    // Only act in grid mode, and only when the scroll has fully settled.
    // IsIntermediate=false means the ListView's keyboard-nav scroll just finished,
    // so it's safe to apply our centering without being overridden.
    if (!m_isGridLayout) return;
    if (args != nullptr && args->IsIntermediate) return;
    if (m_gridCenterPending) DoGridCentering();
}

} // namespace moonlight_xbox_dx
