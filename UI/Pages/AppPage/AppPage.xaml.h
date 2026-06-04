// AppPage.Xaml.h - single clean header

#pragma once

#include "UI\Pages\AppPage\AppPage.g.h"
#include "State\MoonlightApp.h"
#include "UI\Models\ViewModels\AppPageViewModel.h"
#include "UI\Utilities\XamlHelpers.h"
#include <atomic>
#include <ppltasks.h>
#include <unordered_set>

namespace moonlight_xbox_dx
{

// ── Constants ────────────────────────────────────────────────────────────────

static constexpr float  kBackgroundSaturation         = 1.25f;
static constexpr int    kAnimationDurationMs          =   500;
static constexpr int    kBgPanDurationSec             =    10;
static constexpr float  kBlurAmountBackground         =  2.0f;
static constexpr float  kBlurGlowPaddingDip           = 60.0f;

    namespace Controls { ref class SlidingMenu; }
    [Windows::Foundation::Metadata::WebHostHidden]
    public ref class AppPage sealed
    {
    private:
        // Cached SlidingMenu created on demand (not part of the visual tree)
        moonlight_xbox_dx::Controls::SlidingMenu^ m_leftMenu = nullptr;
        moonlight_xbox_dx::Controls::SlidingMenu^ GetLeftMenu();
        AppPageViewModel^ m_viewModel = nullptr;
        MoonlightHost^ host;
        MoonlightApp^ currentApp;
        Platform::Collections::Vector<MoonlightApp^>^ m_filteredApps;
        bool ApplyAppFilter(Platform::String^ filter);
        Windows::Foundation::EventRegistrationToken m_apps_changed_token;
        void OnHostAppsChanged(Windows::Foundation::Collections::IObservableVector<MoonlightApp^>^ sender, Windows::Foundation::Collections::IVectorChangedEventArgs^ args);
        Windows::Foundation::EventRegistrationToken m_back_cookie;
        std::atomic<bool> continueAppFetch{ false };
        std::atomic<bool> wasConnected{ false };
        unsigned int m_centeringAnimationVersion = 0;
        MoonlightApp^ m_selectedApp = nullptr;
    protected:
        virtual void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) override;
        void Connect(int app);
    public:
        AppPage();

        property Windows::Foundation::Collections::IObservableVector<MoonlightApp^>^ FilteredApps {
            Windows::Foundation::Collections::IObservableVector<MoonlightApp^>^ get() {
                if (m_filteredApps == nullptr) m_filteredApps = ref new Platform::Collections::Vector<MoonlightApp^>();
                return m_filteredApps;
            }
        }
        property MoonlightHost^ Host {
            MoonlightHost^ get() { return this->host; }
        }
        property AppPageViewModel^ ViewModel {
            AppPageViewModel^ get() {
                if (m_viewModel == nullptr) m_viewModel = ref new AppPageViewModel();
                return m_viewModel;
            }
        }
        void OnBackRequested(Platform::Object^ e, Windows::UI::Core::BackRequestedEventArgs^ args);

    private:
        void AppsGrid_ItemClick(Platform::Object^ sender, Windows::UI::Xaml::Controls::ItemClickEventArgs^ e);
	    void BlurAppImage(MoonlightApp ^ selApp);
        void AppsGrid_Loaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void AppsGrid_LayoutUpdated(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void SearchBox_TextChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::TextChangedEventArgs^ e);
        void AppsGrid_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
        void ApplySelectionVisuals(MoonlightApp^ app, bool animate);
        void AppsGrid_RightTapped(Platform::Object^ sender, Windows::UI::Xaml::Input::RightTappedRoutedEventArgs^ e);
        void AppsGrid_ContainerContentChanging(Windows::UI::Xaml::Controls::ListViewBase^ sender, Windows::UI::Xaml::Controls::ContainerContentChangingEventArgs^ args);
        void closeAppButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void closeAndStartButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void ExecuteCloseAndStart();
        void backButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void OnLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void OnUnloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void UpdateItemHeights();

        Windows::UI::Xaml::Controls::ScrollViewer^ m_scrollViewer;
        Windows::UI::Xaml::Media::Animation::Storyboard^ m_bgPanStoryboard = nullptr;
        void StartBgPanAnimation();
        void StartBannerSlideInAnimation();

        bool m_isGridLayout = false;

        void LayoutToggleButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void OnGamepadKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ args);
        void CenterSelectedItem(int attempts, bool immediate = false);

        Windows::Foundation::EventRegistrationToken m_keydown_cookie;

        Windows::Foundation::EventRegistrationToken m_layoutUpdated_token;
        Windows::Foundation::EventRegistrationToken m_appsgird_selection_token;
        Windows::Foundation::EventRegistrationToken m_appsgird_itemclick_token;
        Windows::Foundation::EventRegistrationToken m_appsgird_righttapped_token;
        Windows::Foundation::EventRegistrationToken m_appsgird_loaded_token;
        Windows::Foundation::EventRegistrationToken m_appsgird_ccc_token;
        Windows::Foundation::EventRegistrationToken m_searchbox_gettingfocus_token;
        bool m_searchIsOpen = false;
        std::unordered_set<int> m_blurInProgressIds;

        bool m_pendingToggleCentering = false;
        bool m_pendingCentering = false;
        bool m_initialFocusApplied = false;
        void DoGridCentering();
        unsigned int m_appTextAnimVersion = 0;

        concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> ApplyBlur(MoonlightApp^ app, float blurDip, float padDip = 0.0f);

        void FadeInBlurIfSelected(MoonlightApp^ app, Windows::UI::Xaml::Media::Imaging::BitmapImage^ img);
        void FadeInPollingIndicator();
        void FadeOutPollingIndicator();
    };
}
