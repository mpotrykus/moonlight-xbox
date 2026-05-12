//
// HostSelectorPage.xaml.h
// Declaration of the HostSelectorPage class
//

#pragma once

#include "Pages\HostSelectorPage.g.h"
#include "State\ApplicationState.h"

#include <atomic>

using namespace Windows::UI::Core;
namespace moonlight_xbox_dx
{
	[Windows::Foundation::Metadata::WebHostHidden]
	public ref class HostSelectorPage sealed
	{
	public:
		HostSelectorPage();
		property ApplicationState^ State {
			ApplicationState^ get() {
				return this->state;
			}
		}
		void OnStateLoaded();
		void Connect(MoonlightHost^ host);
	protected:
		virtual void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) override;
		virtual void OnNavigatedFrom(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) override;
	private:
		ApplicationState ^state;
		void NewHostButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void OnNewHostDialogPrimaryClick(Windows::UI::Xaml::Controls::ContentDialog^ sender, Windows::UI::Xaml::Controls::ContentDialogButtonClickEventArgs^ args);
		Windows::UI::Xaml::Controls::TextBox ^dialogHostnameTextBox;
		void GridView_ItemClick(Platform::Object^ sender, Windows::UI::Xaml::Controls::ItemClickEventArgs^ e);
		void HostsGrid_Loaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void HostsGrid_SizeChanged(Platform::Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e);
		void HostsGrid_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void CenterSelectedHost(int attempts = 4, bool immediate = false);
		void EnsureCenteringPadding(int attempts = 3);
		Windows::UI::Xaml::Controls::ScrollViewer^ FindScrollViewer(Windows::UI::Xaml::DependencyObject^ root);
		void StartPairing(MoonlightHost^ host);
		void removeHostButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void HostsGrid_RightTapped(Platform::Object^ sender, Windows::UI::Xaml::Input::RightTappedRoutedEventArgs^ e);
		MoonlightHost^ currentHost;
		Windows::UI::Xaml::Controls::ScrollViewer^ m_hostsScrollViewer;
		bool m_adjustingCenterPadding = false;
		double m_lastCenterPadding = -1.0;
		void hostSettingsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void hostDetailsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void SettingsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		std::atomic<bool> continueFetch;
		std::atomic<bool> m_isNavigatedAway;
		std::atomic<int> m_pollActiveCount;
		Windows::System::Threading::ThreadPoolTimer^ m_pollTimer;
		void OnKeyDown(Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e);
		void wakeHostButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void testConnectionButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ShowHostActions(Windows::UI::Xaml::FrameworkElement^ anchor, MoonlightHost^ host);
	};
}
