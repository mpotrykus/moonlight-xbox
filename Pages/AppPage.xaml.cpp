#include "pch.h"
#include "AppPage.Xaml.h"
#include "AppPage.Helpers.h"
#include "Controls\SlidingMenu.xaml.h"
#include "Common\ModalDialog.xaml.h"
#include "HostSelectorPage.xaml.h"
#include "HostSettingsPage.xaml.h"
#include "StreamPage.xaml.h"
#include "Utils.hpp"
#include "Common\XamlHelper.h"
#include "ViewModels\AppPageViewModel.h"
#include <algorithm>
#include <cwctype>
#include <sstream>
#include <vector>
#include <cmath>

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Hosting;
using namespace Windows::UI::Xaml::Navigation;
using namespace Windows::Foundation::Numerics;
using namespace concurrency;

namespace moonlight_xbox_dx {

namespace {

static double ParseDurationStringToMs(Platform::String^ durationValue) {
    if (durationValue == nullptr || durationValue->IsEmpty()) return 250.0;

    std::wstring text(durationValue->Data());
    std::wstringstream ss(text);
    std::wstring segment;
    std::vector<double> parts;

    while (std::getline(ss, segment, L':')) {
        if (segment.empty()) return 250.0;
        try {
            size_t idx = 0;
            double value = std::stod(segment, &idx);
            if (idx != segment.size()) return 250.0;
            parts.push_back(value);
        } catch (...) {
            return 250.0;
        }
    }

    double totalSeconds = 0.0;
    if (parts.size() == 3) {
        totalSeconds = (parts[0] * 3600.0) + (parts[1] * 60.0) + parts[2];
    } else if (parts.size() == 2) {
        totalSeconds = (parts[0] * 60.0) + parts[1];
    } else if (parts.size() == 1) {
        totalSeconds = parts[0];
    } else {
        return 250.0;
    }

    if (!std::isfinite(totalSeconds) || totalSeconds <= 0.0) return 250.0;
    return totalSeconds * 1000.0;
}

static double ResolveBackgroundOverlayOpacityFromPageResources(Page^ page) {
    double overlayOpacity = 0.05;
    if (page == nullptr || page->Resources == nullptr) return overlayOpacity;

    try {
        auto value = page->Resources->Lookup(ref new Platform::String(L"BackgroundOverlayOpacity"));
        auto pv = dynamic_cast<Windows::Foundation::IPropertyValue^>(value);
        if (pv != nullptr) {
            overlayOpacity = pv->GetDouble();
        }
    } catch(...) {}

    if (overlayOpacity < 0.0) overlayOpacity = 0.0;
    if (overlayOpacity > 1.0) overlayOpacity = 1.0;
    return overlayOpacity;
}

static double ResolveSharedAnimationDurationMsFromPageResources(Page^ page) {
    if (page == nullptr || page->Resources == nullptr) return 250.0;

    try {
        auto value = page->Resources->Lookup(ref new Platform::String(L"SharedAnimationDuration"));
        auto durationValue = dynamic_cast<Platform::String^>(value);
        return ParseDurationStringToMs(durationValue);
    } catch(...) {}

    return 250.0;
}

} // namespace

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

// ── AppPage::PollingIndicator fade helpers ────────────────────────────────────

void AppPage::FadeInPollingIndicator() {
    try {
        using namespace Windows::UI::Xaml::Media::Animation;
        PollingIndicator->Opacity = 0.0;
        PollingIndicator->Visibility = Windows::UI::Xaml::Visibility::Visible;
        auto anim = ref new DoubleAnimation();
        anim->To = ref new Platform::Box<double>(0.3);
        TimeSpan ts; ts.Duration = 1500000LL; // 400 ms
        anim->Duration = DurationHelper::FromTimeSpan(ts);
        auto sb = ref new Storyboard();
        sb->Children->Append(anim);
        Storyboard::SetTarget(anim, PollingIndicator);
        Storyboard::SetTargetProperty(anim, ref new Platform::String(L"(UIElement.Opacity)"));
        sb->Begin();
    } catch(...) {}
}

void AppPage::FadeOutPollingIndicator() {
    try {
        using namespace Windows::UI::Xaml::Media::Animation;
        auto anim = ref new DoubleAnimation();
        anim->To = ref new Platform::Box<double>(0.0);
        TimeSpan ts; ts.Duration = 1500000LL; // 400 ms
        anim->Duration = DurationHelper::FromTimeSpan(ts);
        auto sb = ref new Storyboard();
        sb->Children->Append(anim);
        Storyboard::SetTarget(anim, PollingIndicator);
        Storyboard::SetTargetProperty(anim, ref new Platform::String(L"(UIElement.Opacity)"));
        Platform::WeakReference weakThis(this);
        sb->Completed += ref new EventHandler<Platform::Object^>([weakThis](Platform::Object^, Platform::Object^) {
            auto that = weakThis.Resolve<AppPage>();
            if (that) try { that->PollingIndicator->Visibility = Windows::UI::Xaml::Visibility::Collapsed; } catch(...) {}
        });
        sb->Begin();
    } catch(...) {}
}

// ── AppPage::OnNavigatedTo ────────────────────────────────────────────────────

void AppPage::OnNavigatedTo(NavigationEventArgs^ e) {
    MoonlightHost^ mhost = dynamic_cast<MoonlightHost^>(e->Parameter);
    if (mhost == nullptr) return;
    host = mhost;
    host->UpdateHostInfo(true);
    host->UpdateApps();

    // Start background polling for app running state and connectivity
    continueAppFetch.store(true);
    wasConnected.store(host->Connected);
    FadeInPollingIndicator();

    {
        Platform::WeakReference weakThis(this);
        create_task([weakThis]() {
            while (true) {
                auto that = weakThis.Resolve<AppPage>();
                if (that == nullptr) break;
                if (!that->continueAppFetch.load()) break;
                CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
                    CoreDispatcherPriority::Normal,
                    ref new DispatchedHandler([weakThis]() {
                        auto ui = weakThis.Resolve<AppPage>();
                        if (ui) try { ui->FadeInPollingIndicator(); } catch(...) {}
                    }));
                try {
                    if (that->host != nullptr) {
                        that->host->UpdateAppRunningStates();
                        if (that->wasConnected.load() && !that->host->Connected) {
                            that->wasConnected.store(false);
                            CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
                                CoreDispatcherPriority::Normal,
                                ref new DispatchedHandler([weakThis]() {
                                    auto inner = weakThis.Resolve<AppPage>();
                                    if (inner == nullptr) return;
                                    try {
                                        auto dialog = ref new ContentDialog();
                                        dialog->Title = Utils::StringFromStdString("Disconnected");
                                        dialog->Content = Utils::StringFromStdString("Connection to host was lost.");
                                        dialog->PrimaryButtonText = Utils::StringFromStdString("OK");
                                        create_task(::moonlight_xbox_dx::ModalDialog::ShowOnceAsync(dialog)).then([weakThis](ContentDialogResult result) {
                                            auto that2 = weakThis.Resolve<AppPage>();
                                            if (that2 == nullptr) return;
                                            that2->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([that2]() {
                                                try {
                                                    that2->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSelectorPage::typeid));
                                                } catch (const std::exception &e) {
                                                    Utils::Logf("[AppPage] Failed to navigate to HostSelectorPage after disconnect. Exception: %s\n", e.what());
                                                } catch (...) {
                                                    Utils::Log("[AppPage] Failed to navigate to HostSelectorPage after disconnect. Unknown Exception.\n");
                                                }
                                            }));
                                        });
                                    } catch (const std::exception &e) {
                                        Utils::Logf("[AppPage] Failed to show disconnect dialog. Exception: %s\n", e.what());
                                    } catch (...) {
                                        Utils::Log("[AppPage] Failed to show disconnect dialog. Unknown Exception.\n");
                                    }
                                }));
                        } else if (!that->wasConnected.load() && that->host->Connected) {
                            that->wasConnected.store(true);
                        }
                    }
                } catch (const std::exception &e) {
                    Utils::Logf("[AppPage] Failed to poll app and host running state. Exception: %s\n", e.what());
                } catch (...) {
                    Utils::Log("[AppPage] Failed to poll app and host running state. Unknown Exception.\n");
                }
                Sleep(3000);
                CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
                    CoreDispatcherPriority::Normal,
                    ref new DispatchedHandler([weakThis]() {
                        auto ui = weakThis.Resolve<AppPage>();
                        if (ui) try { ui->FadeOutPollingIndicator(); } catch(...) {}
                    }));
                Sleep(7000);
            }
        });
    }

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

    // Restore saved layout for this host
    try {
        bool savedGrid = (host->Personalization->AppView == AppHostView::Grid);
        if (savedGrid != m_isGridLayout && LayoutToggleButton != nullptr) {
            LayoutToggleButton->IsChecked = savedGrid;
            LayoutToggleButton_Click(LayoutToggleButton, nullptr);
        }
    } catch(...) {}

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
                        //SetElementOpacityImmediate(that->SelectedAppBox,  0.0f);
                        //SetElementOpacityImmediate(that->SelectedAppText, 0.0f);
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
                            //SetElementOpacityImmediate(that->SelectedAppBox,  0.0f);
                            //SetElementOpacityImmediate(that->SelectedAppText, 0.0f);
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

// ── AppPage::StartBannerSlideInAnimation ──────────────────────────────────────

void AppPage::StartBannerSlideInAnimation() {
    using namespace Windows::UI::Xaml::Media;
    using namespace Windows::UI::Xaml::Media::Animation;

    try {
        if (ComputerNameBannerContainer == nullptr) return;

        auto transform = ref new TranslateTransform();
        ComputerNameBannerContainer->RenderTransform = transform;

        auto ease = ref new CubicEase();
        ease->EasingMode = EasingMode::EaseInOut;

        auto anim = ref new DoubleAnimation();
        anim->From = ref new Platform::Box<double>(-200.0);
        anim->To   = ref new Platform::Box<double>(0.0);
        TimeSpan ts; ts.Duration = 15000000LL;
        anim->Duration = DurationHelper::FromTimeSpan(ts);
        anim->EasingFunction = ease;

        auto sb = ref new Storyboard();
        sb->Children->Append(anim);
        Storyboard::SetTarget(anim, transform);
        Storyboard::SetTargetProperty(anim, ref new Platform::String(L"X"));
        sb->Begin();
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

    // Initialize ViewModel with the page background border
    try {
        if (this->ViewModel != nullptr && this->PageBackgroundImage != nullptr) {
            this->ViewModel->SetPageBackgroundBorder(this->PageBackgroundImage);
            this->ViewModel->SetBackgroundTransitionSettings(
                ResolveSharedAnimationDurationMsFromPageResources(this),
                ResolveBackgroundOverlayOpacityFromPageResources(this));
        }
    } catch(...) {}

    StartBgPanAnimation();
    StartBannerSlideInAnimation();
}

// ── AppPage::OnUnloaded ───────────────────────────────────────────────────────

void AppPage::OnUnloaded(Platform::Object^, RoutedEventArgs^) {
    try { Windows::UI::Core::SystemNavigationManager::GetForCurrentView()->BackRequested -= m_back_cookie; } catch(...) {}
    continueAppFetch.store(false);
    try { PollingIndicator->Visibility = Windows::UI::Xaml::Visibility::Collapsed; } catch(...) {}

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

        // DPad left/right is handled natively by the ListView's XY focus navigation
        // (same path as arrow keys on PC). Handling it here too causes a double skip
        // because CoreWindow::KeyDown Handled=true does not suppress gamepad XY nav.
        // Thumbstick keys are NOT handled by native XY nav, so we manage those here.
        // The cooldown prevents the thumbstick auto-repeat from firing multiple navigations
        // per flick — the stick crosses the threshold, fires, then may fire again before
        // returning to neutral.
        if (!m_isGridLayout &&
            (key == VirtualKey::GamepadLeftThumbstickRight || key == VirtualKey::GamepadLeftThumbstickLeft)) {
            auto now = std::chrono::steady_clock::now();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastThumbstickNav).count();
            if (elapsedMs >= kAnimationDurationMs) {
                m_lastThumbstickNav = now;
                using namespace Windows::UI::Xaml::Input;
                auto dir = (key == VirtualKey::GamepadLeftThumbstickRight)
                    ? FocusNavigationDirection::Right
                    : FocusNavigationDirection::Left;
                try { FocusManager::TryMoveFocus(dir); } catch(...) {}
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

        if (host != nullptr) {
            host->Personalization->AppView = wantGrid ? AppHostView::Grid : AppHostView::List;
            GetApplicationState()->UpdateFile();
        }

        if (this->AppsGrid != nullptr) {
            auto res = this->Resources;
            if (m_isGridLayout) {
                if (res != nullptr) {
                    auto panel = dynamic_cast<ItemsPanelTemplate^>(res->Lookup(ref new Platform::String(L"GridItemsPanelTemplate")));
                    if (panel != nullptr) this->AppsGrid->ItemsPanel = panel;
                    auto style = dynamic_cast<Windows::UI::Xaml::Style^>(res->Lookup(ref new Platform::String(L"AppGridViewItemContainerStyle")));
                    if (style != nullptr) this->AppsGrid->ItemContainerStyle = style;
                }
                this->AppsGrid->SetValue(ScrollViewer::HorizontalScrollModeProperty, ScrollMode::Disabled);
                this->AppsGrid->SetValue(ScrollViewer::VerticalScrollModeProperty,   ScrollMode::Enabled);
                this->AppsGrid->SetValue(ScrollViewer::VerticalScrollBarVisibilityProperty, ScrollBarVisibility::Auto);
                VisualStateManager::GoToState(this, "GridLayout", false);
            } else {
                if (res != nullptr) {
                    auto panel = dynamic_cast<ItemsPanelTemplate^>(res->Lookup(ref new Platform::String(L"HorizontalItemsPanelTemplate")));
                    if (panel != nullptr) this->AppsGrid->ItemsPanel = panel;
                    auto style = dynamic_cast<Windows::UI::Xaml::Style^>(res->Lookup(ref new Platform::String(L"AppListViewItemContainerStyle")));
                    if (style != nullptr) this->AppsGrid->ItemContainerStyle = style;
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

            // Signal LayoutUpdated to center once the new panel has measured.
            m_pendingToggleCentering = true;
            Utils::Log("LayoutToggle: m_pendingToggleCentering set\n");

            if (this->AppsGrid->SelectedIndex >= 0) {
                auto weakThis = WeakReference(this);
                this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                    ref new DispatchedHandler([weakThis]() {
                        auto that = weakThis.Resolve<AppPage>();
                        if (that == nullptr || that->AppsGrid == nullptr) return;
                        that->AppsGrid_SelectionChanged(that->AppsGrid, nullptr);
                        // Restore keyboard focus to the list after the panel rebuild.
                        // In list mode CenterSelectedItem already focuses the container.
                        // In grid mode we must NOT call container->Focus() here: XAML's
                        // "bring focused element into view" auto-scroll fires after
                        // DoGridCentering and overrides the centering scroll position.
                        // Focusing the ListView itself gives arrow-key nav without triggering
                        // per-item auto-scroll in either mode.
                        auto lv = that->AppsGrid;
                        try { lv->Focus(Windows::UI::Xaml::FocusState::Programmatic); } catch(...) {}
                        // AppsGrid_SelectionChanged → CenterSelectedItem may have used the
                        // translate path (ScrollableWidth==0 during panel swap). One extra
                        // pump lets the ScrollViewer update its ExtentWidth so re-centering
                        // can use ChangeView with the correct ScrollableWidth.
                        // Suppress container focus during this re-centering: focusing a
                        // specific ListViewItem causes XAML bring-into-view which scrolls
                        // to the nearest edge and overrides the centering scroll position.
                        {
                            auto wt2 = WeakReference(that);
                            bool isGrid = that->m_isGridLayout;
                            that->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                                ref new DispatchedHandler([wt2, isGrid]() {
                                    auto that2 = wt2.Resolve<AppPage>();
                                    if (that2 == nullptr) return;
                                    // Suppress focus before UpdateLayout(): UpdateLayout() is
                                    // synchronous and fires LayoutUpdated during the call, which
                                    // calls CenterSelectedItem — if focus is not suppressed there,
                                    // XAML bring-into-view shifts the scroll before we can correct it.
                                    if (!isGrid) that2->m_suppressSelectionFocus = true;
                                    // Force a synchronous layout pass so container->ActualWidth
                                    // reflects the new panel's measurements, not the stale
                                    // grid/list width from the previous layout mode. Without this,
                                    // the centering error grows linearly with the selected index.
                                    try { that2->AppsGrid->UpdateLayout(); } catch(...) {}
                                    // Reset Composition CenterPoints on the selected item's visuals.
                                    // They were set from grid-mode dimensions; after UpdateLayout()
                                    // the elements have their correct list-mode sizes. Without this
                                    // the 1.3x scale anchors from the wrong center, shifting the art
                                    // by (listCX - gridCX) * (scale - 1) ≈ 23 px.
                                    try {
                                        auto lv2 = that2->AppsGrid;
                                        if (lv2 != nullptr && lv2->SelectedItem != nullptr) {
                                            auto c2 = dynamic_cast<ListViewItem^>(lv2->ContainerFromItem(lv2->SelectedItem));
                                            if (c2 != nullptr) {
                                                UIElement^ des2 = nullptr, ^img2 = nullptr, ^nm2 = nullptr;
                                                UIElement^ bl2  = nullptr, ^rf2  = nullptr, ^pl2 = nullptr, ^em2 = nullptr;
                                                FindElementChildren(c2, des2, img2, nm2, bl2, rf2, pl2, em2);
                                                auto resetCP = [](UIElement^ el) {
                                                    if (el == nullptr) return;
                                                    auto fe = dynamic_cast<FrameworkElement^>(el);
                                                    if (fe == nullptr || fe->ActualWidth <= 0 || fe->ActualHeight <= 0) return;
                                                    auto vis = ElementCompositionPreview::GetElementVisual(el);
                                                    if (vis == nullptr) return;
                                                    Windows::Foundation::Numerics::float3 cp;
                                                    cp.x = (float)fe->ActualWidth  * 0.5f;
                                                    cp.y = (float)fe->ActualHeight * 0.5f;
                                                    cp.z = 0.0f;
                                                    vis->CenterPoint = cp;
                                                };
                                                resetCP(img2); resetCP(des2); resetCP(pl2); resetCP(em2);
                                            }
                                        }
                                    } catch(...) {}
                                    if (isGrid) {
                                        if (that2->m_isGridLayout)
                                            try { that2->DoGridCentering(); } catch(...) {}
                                    } else {
                                        if (!that2->m_isGridLayout) {
                                            try { that2->CenterSelectedItem(3, true); } catch(...) {}
                                            that2->m_suppressSelectionFocus = false;
                                        }
                                    }
                                }));
                        }
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
    if (e != nullptr) {
        e->Handled = false;
    }
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
void AppPage::AppsGrid_LayoutUpdated(Platform::Object^, RoutedEventArgs^) {
    if (m_pendingToggleCentering) m_pendingToggleCentering = false;
}
void AppPage::PageRoot_SizeChanged(Platform::Object^, SizeChangedEventArgs^) {}
void AppPage::OnScrollViewerViewChanged(Platform::Object^, ScrollViewerViewChangedEventArgs^) {}

} // namespace moonlight_xbox_dx
