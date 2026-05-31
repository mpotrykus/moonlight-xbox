//
// MoonlightSettings.xaml.cpp
// Implementazione della classe MoonlightSettings
//

#include "pch.h"
#include "UI\Pages\MoonlightSettings.xaml.h"
#include "UI\Backgrounds\DynamicBackgroundHost.xaml.h"
#include "UI\Pages\MoonlightWelcome.xaml.h"
#include "Utils.hpp"
#include "Keyboard/KeyboardCommon.h"
using namespace Windows::UI::Core;

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;

// Il modello di elemento Pagina vuota è documentato all'indirizzo https://go.microsoft.com/fwlink/?LinkId=234238

MoonlightSettings::MoonlightSettings()
{
	InitializeComponent();
	state = GetApplicationState();

	auto item = ref new ComboBoxItem();
	item->Content = "Do not auto-connect";
	item->DataContext = "";
	HostSelector->Items->Append(item);
	
	Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->SetDesiredBoundsMode(Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow);
	auto iid = Utils::StringFromStdString(state->autostartInstance);
	for (int i = 0; i < state->SavedHosts->Size;i++) {
		auto host = state->SavedHosts->GetAt(i);
		auto item = ref new ComboBoxItem();
		item->Content = host->LastHostname;
		item->DataContext = host->InstanceId;
		HostSelector->Items->Append(item);
		if (host->InstanceId->Equals(iid)) {
			HostSelector->SelectedIndex = i+1;
		}
	}
	int k = 0;
	for (auto l : keyboardLayouts) {
		auto item = ref new ComboBoxItem();
		auto s = Utils::StringFromStdString(l.first);
		item->Content = s;
		item->DataContext = s;
		KeyboardLayoutSelector->Items->Append(item);
		if (state->KeyboardLayout->Equals(s)) {
			KeyboardLayoutSelector->SelectedIndex = k;
		}
		k++;
	}
	this->Loaded += ref new Windows::UI::Xaml::RoutedEventHandler(this, &MoonlightSettings::OnLoaded);
	this->Unloaded += ref new Windows::UI::Xaml::RoutedEventHandler(this, &MoonlightSettings::OnUnloaded);
}

void MoonlightSettings::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) {
	try {
		if (BackgroundHost != nullptr) {
			BackgroundHost->SetHosts(state->SavedHosts);
			BackgroundHost->Refresh();
			BackgroundHost->StartAnimations();
		}
	} catch (...) {}
}

void MoonlightSettings::OnNavigatedFrom(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) {
	try {
		if (BackgroundHost != nullptr) BackgroundHost->StopAnimations();
	} catch (...) {}
}

void MoonlightSettings::backButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	GetApplicationState()->UpdateFile();
	this->Frame->GoBack();
}

void MoonlightSettings::HostSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
	ComboBoxItem^ item = (ComboBoxItem^)this->HostSelector->SelectedItem;

	auto s = Utils::PlatformStringToStdString(item->DataContext->ToString());
	state->autostartInstance = s;

}

void MoonlightSettings::OnBackRequested(Platform::Object^ e, Windows::UI::Core::BackRequestedEventArgs^ args)
{
	// UWP on Xbox One triggers a back request whenever the B
	// button is pressed which can result in the app being
	// suspended if unhandled
	GetApplicationState()->UpdateFile();
	this->Frame->GoBack();
	args->Handled = true;

}

void MoonlightSettings::WelcomeButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto slideForward = ref new Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionInfo();
	slideForward->Effect = Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionEffect::FromRight;
	this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightWelcome::typeid), nullptr, slideForward);
}

void MoonlightSettings::LayoutSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
	ComboBoxItem^ item = (ComboBoxItem^)this->KeyboardLayoutSelector->SelectedItem;

	auto s = item->DataContext->ToString();
	state->KeyboardLayout = s;
}

void MoonlightSettings::OnLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	m_back_cookie = navigation->BackRequested += ref new EventHandler<BackRequestedEventArgs^>(this, &MoonlightSettings::OnBackRequested);
	ScreenMarginPreviewBorder->Margin = state->ScreenMargin;
}

void MoonlightSettings::OnMarginSliderChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs^ e)
{
	ScreenMarginPreviewBorder->Margin = state->ScreenMargin;
}

void MoonlightSettings::OnMarginSliderGotFocus(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	m_marginSliderFocused = true;
	ShowMarginPreview->Begin();
}

void MoonlightSettings::OnMarginSliderLostFocus(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	m_marginSliderFocused = false;
	this->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Low,
		ref new Windows::UI::Core::DispatchedHandler([this]() {
			if (!m_marginSliderFocused) HideMarginPreview->Begin();
		}));
}


void MoonlightSettings::OnUnloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	navigation->BackRequested -= m_back_cookie;
}

