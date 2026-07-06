#include "pch.h"
#include "AppPage.Xaml.h"
#include "UI\Controls\SlidingMenu.xaml.h"
#include "UI\Modals\AlertDialog.xaml.h"
#include "UI\Modals\AppActionsDialog.xaml.h"
#include "UI\Modals\ConfirmDialog.xaml.h"
#include "UI\Pages\HostSelectorPage.xaml.h"
#include "UI\Pages\HostSettingsPage.xaml.h"
#include "UI\Pages\MoonlightSettings.xaml.h"
#include "UI\Pages\StreamPage.xaml.h"
#include "State\MoonlightClient.h"
#include "Utils.hpp"
#include "UI\Models\ViewModels\AppPageViewModel.h"
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
        return Utils::DurationStringToMs(durationValue);
    } catch(...) {}

    return 250.0;
}

static void FindElementChildren(DependencyObject^ container,
    UIElement^& outDesaturator, UIElement^& outImage, UIElement^& outName,
    UIElement^& outBlur, UIElement^& outPlay)
{
    outDesaturator = outImage = outName = outBlur = outPlay = nullptr;
    if (container == nullptr) return;
    try {
        outDesaturator = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"Desaturator")));
        outImage       = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppImageRect")));
        outName        = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppName")));
        outBlur        = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"AppImageBlurRect")));
        outPlay        = dynamic_cast<UIElement^>(FindChildByName(container, ref new Platform::String(L"Play")));
    } catch(...) {
        outDesaturator = outImage = outName = outBlur = outPlay = nullptr;
    }
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
        m_layoutUpdated_token.Value = 0;
        m_appsgird_selection_token.Value = m_appsgird_itemclick_token.Value = 0;
        m_appsgird_righttapped_token.Value = 0;
        m_appsgird_loaded_token.Value = m_appsgird_ccc_token.Value = 0;
        m_searchbox_gettingfocus_token.Value = 0;
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
            m_appsgird_loaded_token = this->AppsGrid->Loaded +=
                ref new RoutedEventHandler([weakThis](Platform::Object^ s, RoutedEventArgs^ e) {
                    auto that = weakThis.Resolve<AppPage>(); if (that) try { that->AppsGrid_Loaded(s, e); } catch(...) {}
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

    // GettingFocus fires synchronously inside the XY-nav pipeline, before focus
    // moves.  Cancel unexpected arrivals; allow only when m_searchIsOpen is true
    // (Y button / DPad-Down return path) or the nav direction is Up (DPad-Up from
    // the list/grid first row intentionally moving up to the search bar).
    try {
        if (this->SearchBox != nullptr) {
            auto weakThis = WeakReference(this);
            m_searchbox_gettingfocus_token = this->SearchBox->GettingFocus +=
                ref new Windows::Foundation::TypedEventHandler<
                    Windows::UI::Xaml::UIElement^,
                    Windows::UI::Xaml::Input::GettingFocusEventArgs^>(
                [weakThis](Windows::UI::Xaml::UIElement^, Windows::UI::Xaml::Input::GettingFocusEventArgs^ args) {
                    auto that = weakThis.Resolve<AppPage>();
                    if (that == nullptr) return;
                    try {
                        if (that->m_searchIsOpen) return;  // intentionally open — allow any direction
                        if (args->Direction == FocusNavigationDirection::Up) {
                            that->m_searchIsOpen = true;   // DPad-Up intent — mark open and allow
                            return;
                        }
                        args->Cancel = true;  // block all other accidental directions (Left, Right, None…)
                    } catch(...) {}
                });
        }
    } catch(...) {}

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

    // Start background polling for app running state and connectivity
    continueAppFetch.store(true);
    wasConnected.store(true); // HostSelectorPage verified connectivity before navigating here
    FadeInPollingIndicator();

    // Load the app list in background to avoid blocking the UI thread.
    {
        Platform::WeakReference weakThis(this);
        create_task([weakThis]() {
            auto that = weakThis.Resolve<AppPage>();
            if (that == nullptr || !that->continueAppFetch.load()) return;
            that->host->UpdateApps();
        });
    }

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
                                        auto dialog = ref new ::moonlight_xbox_dx::AlertDialog();
                                        dialog->Configure(L"Disconnected", L"Connection to host was lost.");
                                        try { dialog->XamlRoot = inner->XamlRoot; } catch (...) {}
                                        create_task(dialog->ShowAsync()).then([weakThis](ContentDialogResult result) {
                                            auto that2 = weakThis.Resolve<AppPage>();
                                            if (that2 == nullptr) return;
                                            that2->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([that2]() {
                                                try {
                                                    auto slideBack = ref new Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionInfo();
                                                    slideBack->Effect = Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionEffect::FromLeft;
                                                    that2->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSelectorPage::typeid), nullptr, slideBack);
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

    // Apply the per-host accent color so {ThemeResource SystemAccentColor} reflects
    // this host's personalization throughout the app while AppPage is active.
    {
        Windows::UI::Color accentColor = host->Personalization->UseSystemAccent
            ? (ref new Windows::UI::ViewManagement::UISettings())
                ->GetColorValue(Windows::UI::ViewManagement::UIColorType::Accent)
            : host->Personalization->AccentColor;
        ApplyAccentColor(accentColor);
        auto cur = this->ActualTheme;
        this->RequestedTheme = (cur != ElementTheme::Dark) ? ElementTheme::Dark : ElementTheme::Light;
        this->RequestedTheme = ElementTheme::Default;
    }

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
    if (identical) {
        // Swap object references so that in-place property updates (e.g. CurrentlyRunning from
        // the polling loop) hit the same instances the UI is bound to via m_filteredApps.
        try {
            for (unsigned int i = 0; i < vec->Size; ++i) {
                auto a = vec->GetAt(i), b = newResults->GetAt(i);
                if (a != b) {
                    b->IsSelected    = a->IsSelected;    // preserve binding state
                    b->BlurredImage  = a->BlurredImage;  // carry over computed blur
                    b->GlowImage     = a->GlowImage;     // carry over computed glow
                    if (a->Image != nullptr) b->Image = a->Image; // avoid reloading
                    if (m_selectedApp != nullptr && m_selectedApp->Id == a->Id)
                        m_selectedApp = b; // keep m_selectedApp on the current binding target
                    vec->SetAt(i, b);
                }
            }
        } catch(...) {}
        return true;
    }

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
                }
            } catch(...) {}
        }));
    } catch(...) {}

    // Restore selection to first item if the collection change cleared it.
    if (vec->Size > 0 && this->AppsGrid != nullptr && this->AppsGrid->SelectedIndex < 0)
        this->AppsGrid->SelectedIndex = 0;
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
    if (m_selectedApp != nullptr) { try { m_selectedApp->IsSelected = false; } catch(...) {} }
    m_selectedApp = nullptr;
    m_pendingCentering = false;
    m_initialFocusApplied = false;
    try { Windows::UI::Core::SystemNavigationManager::GetForCurrentView()->BackRequested -= m_back_cookie; } catch(...) {}
    try { if (this->SearchBox != nullptr) this->SearchBox->GettingFocus -= m_searchbox_gettingfocus_token; } catch(...) {}
    continueAppFetch.store(false);
    try { PollingIndicator->Visibility = Windows::UI::Xaml::Visibility::Collapsed; } catch(...) {}

    auto window = CoreApplication::MainView->CoreWindow;
    if (window != nullptr) try { window->KeyDown -= m_keydown_cookie; } catch(...) {}

    if (this->host != nullptr && this->host->Apps != nullptr) {
        auto obs = dynamic_cast<Windows::Foundation::Collections::IObservableVector<MoonlightApp^>^>(this->host->Apps);
        if (obs != nullptr) try { obs->VectorChanged -= m_apps_changed_token; } catch(...) {}
    }

    if (m_bgPanStoryboard != nullptr) { m_bgPanStoryboard->Stop(); m_bgPanStoryboard = nullptr; }
    if (this->AppsGrid != nullptr) {
        try { this->AppsGrid->SelectionChanged         -= m_appsgird_selection_token;   } catch(...) {}
        try { this->AppsGrid->ItemClick                -= m_appsgird_itemclick_token;   } catch(...) {}
        try { this->AppsGrid->RightTapped              -= m_appsgird_righttapped_token; } catch(...) {}
        try { this->AppsGrid->Loaded                   -= m_appsgird_loaded_token;      } catch(...) {}
        try { this->AppsGrid->ContainerContentChanging -= m_appsgird_ccc_token;         } catch(...) {}
        try { this->AppsGrid->LayoutUpdated            -= m_layoutUpdated_token;        } catch(...) {}
    }
}


// ── AppPage::OnBackRequested ──────────────────────────────────────────────────

void AppPage::OnBackRequested(Platform::Object^, BackRequestedEventArgs^ args) {
    auto lm = this->GetLeftMenu();
    if (lm != nullptr && lm->IsOpen) { lm->Close(); args->Handled = true; return; }

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
                    if (that == nullptr) return;
                    try {
                        bool searchHasFocus = that->SearchBox != nullptr &&
                            that->SearchBox->FocusState != Windows::UI::Xaml::FocusState::Unfocused;
                        if (searchHasFocus) {
                            // Y pressed while search is focused → return to list
                            that->m_searchIsOpen = false;
                            if (that->AppsGrid != nullptr && that->AppsGrid->SelectedItem != nullptr) {
                                auto c = dynamic_cast<ListViewItem^>(
                                    that->AppsGrid->ContainerFromItem(that->AppsGrid->SelectedItem));
                                if (c != nullptr) c->Focus(Windows::UI::Xaml::FocusState::Programmatic);
                                else that->AppsGrid->Focus(Windows::UI::Xaml::FocusState::Programmatic);
                            } else if (that->AppsGrid != nullptr) {
                                that->AppsGrid->Focus(Windows::UI::Xaml::FocusState::Programmatic);
                            }
                        } else {
                            that->m_searchIsOpen = true;
                            that->SearchBox->Focus(Windows::UI::Xaml::FocusState::Programmatic);
                        }
                    } catch (...) {}
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

        // DPad-Up from the list → SearchBox; DPad-Down from SearchBox → back to list.
        // Grid mode: GettingFocus on SearchBox handles this entirely (Direction==Up is
        // allowed, all other directions are cancelled).  List mode needs a RunAsync
        // fallback for items where XY nav may not spatially reach SearchBox.
        if (key == VirtualKey::GamepadDPadUp) {
            bool searchHasFocus = this->SearchBox != nullptr &&
                this->SearchBox->FocusState != Windows::UI::Xaml::FocusState::Unfocused;
            if (!searchHasFocus && !m_isGridLayout) {
                m_searchIsOpen = true;
                auto weakThis = WeakReference(this);
                this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                    ref new DispatchedHandler([weakThis]() {
                        auto that = weakThis.Resolve<AppPage>();
                        if (that == nullptr) return;
                        try {
                            if (that->SearchBox != nullptr &&
                                that->SearchBox->FocusState == Windows::UI::Xaml::FocusState::Unfocused)
                                that->SearchBox->Focus(Windows::UI::Xaml::FocusState::Programmatic);
                        } catch(...) {}
                    }));
            }
        }

        if (key == VirtualKey::GamepadDPadDown) {
            bool searchHasFocus = this->SearchBox != nullptr &&
                this->SearchBox->FocusState != Windows::UI::Xaml::FocusState::Unfocused;
            if (searchHasFocus) {
                m_searchIsOpen = false;
                auto weakThis = WeakReference(this);
                this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                    ref new DispatchedHandler([weakThis]() {
                        auto that = weakThis.Resolve<AppPage>();
                        if (that == nullptr) return;
                        try {
                            if (that->AppsGrid != nullptr && that->AppsGrid->SelectedItem != nullptr) {
                                auto c = dynamic_cast<ListViewItem^>(
                                    that->AppsGrid->ContainerFromItem(that->AppsGrid->SelectedItem));
                                if (c != nullptr) c->Focus(Windows::UI::Xaml::FocusState::Programmatic);
                                else that->AppsGrid->Focus(Windows::UI::Xaml::FocusState::Programmatic);
                            } else if (that->AppsGrid != nullptr) {
                                that->AppsGrid->Focus(Windows::UI::Xaml::FocusState::Programmatic);
                            }
                        } catch(...) {}
                    }));
                args->Handled = true;
            }
        }

        // DPad and thumbstick navigation within the list are handled natively by the
        // ListView's XY focus system. Adding handlers here too causes a double-skip
        // because CoreWindow::KeyDown Handled=true does not suppress XY focus navigation.

    } catch(...) {}
}

// ── AppPage::SearchBox_TextChanged ────────────────────────────────────────────

void AppPage::SearchBox_TextChanged(Platform::Object^ sender, TextChangedEventArgs^) {
    try {
        auto tb = dynamic_cast<TextBox^>(sender);
        if (tb == nullptr) return;
        ApplyAppFilter(tb->Text);
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
            // Capture selection BEFORE the panel change: Xbox XAML clears SelectedIndex
            // when ItemsPanel is reassigned, and we need the value to restore afterward.
            int savedIdx = this->AppsGrid->SelectedIndex;

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
                // Clear list-mode edge padding immediately so the first UpdateLayout pass
                // sees the full viewport width and the ItemsWrapGrid wraps correctly.
                {
                    auto padding = this->AppsGrid->Padding;
                    if (std::fabs(padding.Left) > 0.5 || std::fabs(padding.Right) > 0.5) {
                        padding.Left = 0.0;
                        padding.Right = 0.0;
                        this->AppsGrid->Padding = padding;
                    }
                }
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

            // Xbox XAML clears SelectedIndex when ItemsPanel changes. Restore it
            // synchronously so the dispatch chain below and LayoutUpdated centering
            // both see a valid selection immediately.
            if (savedIdx >= 0 && this->AppsGrid->SelectedIndex < 0
                && this->AppsGrid->Items != nullptr
                && savedIdx < (int)this->AppsGrid->Items->Size)
                try { this->AppsGrid->SelectedIndex = savedIdx; } catch(...) {}

            // For list mode: set the edge padding synchronously so item 0 is already
            // centered in the very first rendered frame. The async dispatch chain does
            // the full centering for other items and after Phase 1 template application,
            // but this eliminates the visible "item at left edge" flash for item 0.
            if (!m_isGridLayout && this->AppsGrid->SelectedItem != nullptr) {
                try {
                    this->AppsGrid->UpdateLayout();
                    if (m_scrollViewer == nullptr) m_scrollViewer = FindScrollViewer(this->AppsGrid);
                    if (m_scrollViewer != nullptr) {
                        double vp = m_scrollViewer->ViewportWidth;
                        if (vp > 0) {
                            auto c0 = dynamic_cast<ListViewItem^>(
                                this->AppsGrid->ContainerFromItem(this->AppsGrid->SelectedItem));
                            if (c0 != nullptr && c0->ActualWidth > 0) {
                                double desired = std::max(0.0, (vp - c0->ActualWidth) * 0.5);
                                auto padding = this->AppsGrid->Padding;
                                if (std::fabs(padding.Left - desired) > 0.5
                                    || std::fabs(padding.Right - desired) > 0.5) {
                                    padding.Left  = desired;
                                    padding.Right = desired;
                                    this->AppsGrid->Padding = padding;
                                    this->AppsGrid->UpdateLayout();
                                }
                            }
                        }
                    }
                } catch(...) {}
            }

            m_scrollViewer = nullptr;

            for (int i = 0; i < (int)this->Host->Apps->Size; ++i) {
                auto app = this->Host->Apps->GetAt(i);
                app->BlurredImage = nullptr;
                app->GlowImage = nullptr;
                this->Host->Apps->SetAt(i, app);
            }

            // Signal LayoutUpdated to center once the new panel has measured.
            m_pendingToggleCentering = true;
            Utils::Log("LayoutToggle: m_pendingToggleCentering set\n");

            if (this->AppsGrid->SelectedIndex >= 0) {
                auto weakThis = WeakReference(this);
                this->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                    ref new DispatchedHandler([weakThis, savedIdx]() {
                        auto that = weakThis.Resolve<AppPage>();
                        if (that == nullptr || that->AppsGrid == nullptr) return;
                        // Restore selection as a fallback in case something between the
                        // synchronous restore and this dispatch cleared it again.
                        if (that->AppsGrid->SelectedIndex < 0
                            && that->AppsGrid->Items != nullptr
                            && savedIdx >= 0 && savedIdx < (int)that->AppsGrid->Items->Size)
                            try { that->AppsGrid->SelectedIndex = savedIdx; } catch(...) {}
                        that->AppsGrid_SelectionChanged(that->AppsGrid, nullptr);
                        // Give the ListView interim focus while the inner dispatch runs;
                        // the inner dispatch focuses the selected container after centering.
                        auto lv = that->AppsGrid;
                        try { lv->Focus(Windows::UI::Xaml::FocusState::Programmatic); } catch(...) {}
                        // AppsGrid_SelectionChanged → CenterSelectedItem may have used the
                        // translate path (ScrollableWidth==0 during panel swap). One extra
                        // pump lets the ScrollViewer update its ExtentWidth so re-centering
                        // can use ChangeView with the correct ScrollableWidth.
                        {
                            auto wt2 = WeakReference(that);
                            bool isGrid = that->m_isGridLayout;
                            that->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                                ref new DispatchedHandler([wt2, isGrid]() {
                                    auto that2 = wt2.Resolve<AppPage>();
                                    if (that2 == nullptr) return;
                                    // Force a synchronous layout pass so container->ActualWidth
                                    // reflects the new panel's measurements, not the stale
                                    // grid/list width from the previous layout mode. Without this,
                                    // the centering error grows linearly with the selected index.
                                    try { that2->AppsGrid->UpdateLayout(); } catch(...) {}
                                    // Set item heights now that the new template is applied and layout
                                    // has run — avoids using stale container dimensions from the old mode.
                                    try { that2->UpdateItemHeights(); } catch(...) {}
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
                                                UIElement^ bl2  = nullptr, ^pl2 = nullptr;
                                                FindElementChildren(c2, des2, img2, nm2, bl2, pl2);
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
                                                resetCP(img2); resetCP(des2); resetCP(pl2);
                                            }
                                        }
                                    } catch(...) {}
                                    if (isGrid ? that2->m_isGridLayout : !that2->m_isGridLayout) {
                                        try { that2->CenterSelectedItem(3, true); } catch(...) {}
                                        // Phase 1 (new item template) may apply after CenterSelectedItem
                                        // "succeeds" with Phase 0 container dimensions, changing ActualWidth
                                        // without triggering re-centering. Re-arm the toggle flag so
                                        // LayoutUpdated (which fires after the Phase 1 layout pass) sets
                                        // m_pendingCentering and runs one final centering with correct dims.
                                        // Using m_pendingToggleCentering (not m_pendingCentering) means the
                                        // CenterSelectedItem retries above cannot clear it prematurely.
                                        that2->m_pendingToggleCentering = true;
                                    }
                                    // Focus the selected container so XY nav resumes from the
                                    // selected item instead of item 0. CenterSelectedItem with
                                    // immediate=true has already applied the scroll offset, so
                                    // BringIntoViewRequested is a no-op (item is fully visible).
                                    try {
                                        auto lv3 = that2->AppsGrid;
                                        if (lv3 != nullptr && lv3->SelectedItem != nullptr) {
                                            auto sel = dynamic_cast<ListViewItem^>(lv3->ContainerFromItem(lv3->SelectedItem));
                                            if (sel != nullptr)
                                                sel->Focus(Windows::UI::Xaml::FocusState::Programmatic);
                                        }
                                    } catch(...) {}
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
    } catch(...) {}
}

// ── Button click handlers ─────────────────────────────────────────────────────

void AppPage::backButton_Click(Platform::Object^, RoutedEventArgs^) {
    this->Frame->GoBack();
}

void AppPage::AppsGrid_LayoutUpdated(Platform::Object^, RoutedEventArgs^) {
    if (m_pendingToggleCentering) {
        m_pendingToggleCentering = false;
        // Only arm centering if there is a valid selection; avoids an infinite
        // LayoutUpdated spin when the panel change temporarily clears SelectedIndex.
        if (this->AppsGrid != nullptr && this->AppsGrid->SelectedIndex >= 0)
            m_pendingCentering = true;
    }
    if (m_pendingCentering) {
        if (this->AppsGrid != nullptr && this->AppsGrid->SelectedIndex >= 0)
            CenterSelectedItem(4, false);
        else
            m_pendingCentering = false;
    }
}

// ── AppPage::AppsGrid_ItemClick ───────────────────────────────────────────────

void AppPage::AppsGrid_ItemClick(Platform::Object^ sender, ItemClickEventArgs^ e) {
    MoonlightApp^ app = (MoonlightApp^)e->ClickedItem;
    this->currentApp = app;

    if (this->host != nullptr) {
        for (unsigned int i = 0; i < this->host->Apps->Size; ++i) {
            auto candidate = this->host->Apps->GetAt(i);
            if (candidate != nullptr && candidate->CurrentlyRunning && candidate->Id != app->Id) {
                this->closeAndStartButton_Click(nullptr, nullptr);
                return;
            }
        }
    }
    this->Connect(app->Id);
}

// ── AppPage::Connect ──────────────────────────────────────────────────────────

void AppPage::Connect(int appId) {
    StreamConfiguration^ config = ref new StreamConfiguration();
    config->hostname      = host->LastHostname;
    config->appID         = appId;
    config->width         = host->Resolution->Width;
    config->height        = host->Resolution->Height;
    config->bitrate       = host->Bitrate;
    config->FPS           = host->FPS;
    config->audioConfig   = host->AudioConfig;
    config->videoCodec    = host->VideoCodec;
    config->playAudioOnPC = host->PlayAudioOnPC;
    config->enableHDR     = host->EnableHDR;
    config->enableSOPS    = host->EnableSOPS;
    config->enableStats   = host->EnableStats;
    config->enableGraphs  = host->EnableGraphs;
    if (config->enableHDR) host->VideoCodec = "HEVC (H.265)";
    config->backgroundImage = (this->currentApp != nullptr) ? this->currentApp->BlurredImage : nullptr;
    config->appName         = (this->currentApp != nullptr) ? this->currentApp->Name : nullptr;
    this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(StreamPage::typeid), config, ref new Windows::UI::Xaml::Media::Animation::DrillInNavigationTransitionInfo());
}

// ── AppPage::AppsGrid_RightTapped ────────────────────────────────────────────

void AppPage::AppsGrid_RightTapped(Platform::Object^ sender, Windows::UI::Xaml::Input::RightTappedRoutedEventArgs^ e) {
    Utils::Log("AppPage::AppsGrid_RightTapped\n");

    // Determine currentApp from tapped element
    FrameworkElement^ senderElement = dynamic_cast<FrameworkElement^>(e != nullptr ? e->OriginalSource : nullptr);
    if (senderElement != nullptr) {
        if (senderElement->GetType()->FullName->Equals(ListViewItem::typeid->FullName)) {
            currentApp = (MoonlightApp^)((ListViewItem^)senderElement)->Content;
        } else {
            currentApp = dynamic_cast<MoonlightApp^>(senderElement->DataContext);
            if (currentApp == nullptr && this->AppsGrid != nullptr && this->AppsGrid->SelectedIndex >= 0)
                currentApp = (MoonlightApp^)this->AppsGrid->SelectedItem;
        }
    }

    bool anyRunning = false;
    if (this->host != nullptr) {
        for (unsigned int i = 0; i < this->host->Apps->Size; ++i) {
            auto c = this->host->Apps->GetAt(i);
            if (c != nullptr && c->CurrentlyRunning) { anyRunning = true; break; }
        }
    }

    if (this->currentApp == nullptr) {
        if (e != nullptr) e->Handled = false;
        return;
    }

    try {
        Platform::WeakReference weakThis(this);
        auto dialog = ref new AppActionsDialog();
        dialog->Configure(
            (this->currentApp->Name != nullptr) ? this->currentApp->Name : ref new Platform::String(L""),
            this->currentApp->CurrentlyRunning,
            !this->currentApp->CurrentlyRunning && anyRunning,
            !this->currentApp->CurrentlyRunning && !anyRunning,
            ref new RoutedEventHandler([weakThis](Platform::Object^, RoutedEventArgs^) {
                auto that = weakThis.Resolve<AppPage>();
                if (that != nullptr) try { that->Connect(that->currentApp->Id); } catch (...) {}
            }),
            ref new RoutedEventHandler([weakThis, dialog](Platform::Object^, RoutedEventArgs^) {
                auto that = weakThis.Resolve<AppPage>();
                if (that != nullptr) try { that->closeAppButton_Click(dialog, nullptr); } catch (...) {}
            }),
            ref new RoutedEventHandler([weakThis, dialog](Platform::Object^, RoutedEventArgs^) {
                auto that = weakThis.Resolve<AppPage>();
                if (that != nullptr) try { that->closeAndStartButton_Click(dialog, nullptr); } catch (...) {}
            }),
            ref new RoutedEventHandler([weakThis](Platform::Object^, RoutedEventArgs^) {
                auto that = weakThis.Resolve<AppPage>();
                if (that != nullptr && that->currentApp != nullptr) try { that->Connect(that->currentApp->Id); } catch (...) {}
            }),
            ref new RoutedEventHandler([weakThis](Platform::Object^, RoutedEventArgs^) {
                auto that = weakThis.Resolve<AppPage>();
                if (that != nullptr) try {
                    auto t = ref new Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionInfo();
                    t->Effect = Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionEffect::FromRight;
                    that->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightSettings::typeid), nullptr, t);
                } catch (...) {}
            }),
		    ref new RoutedEventHandler([weakThis](Platform::Object ^, RoutedEventArgs ^) {
			    auto that = weakThis.Resolve<AppPage>();
                if (that != nullptr) try {
                    auto t = ref new Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionInfo();
                    t->Effect = Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionEffect::FromRight;
                    that->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSettingsPage::typeid), that->Host, t);
                } catch (...) {}
            }));

        create_task(dialog->ShowAsync());
        if (e != nullptr) e->Handled = true;
    } catch (...) {
        if (e != nullptr) e->Handled = false;
    }
}

// ── AppPage::closeAndStartButton_Click ────────────────────────────────────────

void AppPage::closeAndStartButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
    if (this->currentApp == nullptr) return;
    if (sender != nullptr) { this->ExecuteCloseAndStart(); return; }

    Platform::String^ runningName = nullptr;
    if (this->host != nullptr) {
        for (unsigned int i = 0; i < this->host->Apps->Size; ++i) {
            auto candidate = this->host->Apps->GetAt(i);
            if (candidate != nullptr && candidate->CurrentlyRunning && candidate->Id != this->currentApp->Id) {
                runningName = candidate->Name;
                break;
            }
        }
    }

    auto startName = (this->currentApp->Name != nullptr && this->currentApp->Name->Length() > 0)
        ? std::wstring(this->currentApp->Name->Data()) : std::wstring(L"this app");
    auto closePart = (runningName != nullptr && runningName->Length() > 0)
        ? std::wstring(L"Close '") + runningName->Data() + L"'"
        : std::wstring(L"Close the currently running app");
    Platform::String^ message = ref new Platform::String((closePart + L" and start '" + startName + L"'?").c_str());

    Platform::WeakReference weakThis(this);
    auto dialog = ref new ConfirmDialog();
    dialog->Configure(
        ref new Platform::String(L"Close & Start"),
        message,
        ref new RoutedEventHandler([weakThis](Platform::Object^, RoutedEventArgs^) {
            auto that = weakThis.Resolve<AppPage>();
            if (that) that->ExecuteCloseAndStart();
        })
    );
    create_task(dialog->ShowAsync());
}

// ── AppPage::ExecuteCloseAndStart ─────────────────────────────────────────────

void AppPage::ExecuteCloseAndStart() {
    Platform::WeakReference weakThis(this);
    auto name = (this->currentApp != nullptr && this->currentApp->Name != nullptr) ? this->currentApp->Name : nullptr;
    this->ClosingOverlayText->Text = (name != nullptr && name->Length() > 0)
        ? ref new Platform::String((std::wstring(L"Closing ") + name->Data() + L"...").c_str())
        : ref new Platform::String(L"Closing...");
    this->ClosingOverlay->Visibility = Windows::UI::Xaml::Visibility::Visible;

    create_task(create_async([weakThis]() {
        try {
            auto thatLocal = weakThis.Resolve<AppPage>();
            if (thatLocal == nullptr) return;
            MoonlightClient client;
            auto ipAddr = Utils::PlatformStringToStdString(thatLocal->host->LastHostname);
            if (client.Connect(ipAddr.c_str()) == 0) { client.StopApp(); Sleep(1000); }
        } catch(...) {}
        auto thatLocal2 = weakThis.Resolve<AppPage>();
        if (thatLocal2 == nullptr) return;
        thatLocal2->Dispatcher->RunAsync(CoreDispatcherPriority::High,
            ref new DispatchedHandler([weakThis]() {
            auto thatUI = weakThis.Resolve<AppPage>();
            try {
                if (thatUI != nullptr) {
                    thatUI->ClosingOverlay->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
                    if (thatUI->currentApp != nullptr)
                        thatUI->Connect(thatUI->currentApp->Id);
                }
            } catch(...) {}
        }));
    })).then([](concurrency::task<void> t) { try { t.get(); } catch(...) {} });
}

// ── AppPage::closeAppButton_Click ─────────────────────────────────────────────

void AppPage::closeAppButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
    Platform::WeakReference weakThis(this);
    auto name = (this->currentApp != nullptr && this->currentApp->Name != nullptr) ? this->currentApp->Name : nullptr;
    this->ClosingOverlayText->Text = (name != nullptr && name->Length() > 0)
        ? ref new Platform::String((std::wstring(L"Closing ") + name->Data() + L"...").c_str())
        : ref new Platform::String(L"Closing...");
    this->ClosingOverlay->Visibility = Windows::UI::Xaml::Visibility::Visible;

    create_task(create_async([weakThis]() {
        try {
            auto thatLocal = weakThis.Resolve<AppPage>();
            if (thatLocal == nullptr) return;
            MoonlightClient client;
            auto ipAddr = Utils::PlatformStringToStdString(thatLocal->host->LastHostname);
            if (client.Connect(ipAddr.c_str()) == 0) { client.StopApp(); Sleep(1000); }
        } catch(...) {}
        auto thatLocal2 = weakThis.Resolve<AppPage>();
        if (thatLocal2 == nullptr) return;
        thatLocal2->Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
            ref new DispatchedHandler([weakThis]() {
            auto thatUI = weakThis.Resolve<AppPage>();
            try {
                if (thatUI != nullptr) {
                    thatUI->host->UpdateHostInfo(true);
                    thatUI->host->UpdateAppRunningStates();
                    thatUI->ClosingOverlay->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
                }
            } catch(...) {}
        }));
    }));
}

} // namespace moonlight_xbox_dx
