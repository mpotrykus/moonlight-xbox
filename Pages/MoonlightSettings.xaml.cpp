//
// MoonlightSettings.xaml.cpp
// Implementazione della classe MoonlightSettings
//

#include "pch.h"
#include "MoonlightSettings.xaml.h"
#include "MoonlightWelcome.xaml.h"
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

	// Restore theme preference from local settings (if present)
	auto localSettings = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
	if (localSettings->HasKey("theme")) {
		auto v = safe_cast<Platform::String^>(localSettings->Lookup("theme"));
		ApplyTheme(v);
		// set toggle initial state
		if (v->Equals("Dark")) {
			ThemeToggle->IsOn = true;
		} else {
			ThemeToggle->IsOn = false;
		}
	}
	auto item = ref new ComboBoxItem();
	item->Content = "Don't autoconnect";
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
	this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightWelcome::typeid));
}

void MoonlightSettings::LayoutSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
	ComboBoxItem^ item = (ComboBoxItem^)this->KeyboardLayoutSelector->SelectedItem;

	auto s = item->DataContext->ToString();
	state->KeyboardLayout = s;
}

void MoonlightSettings::ThemeToggle_Toggled(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto toggle = safe_cast<ToggleSwitch^>(sender);
	auto theme = toggle->IsOn ? L"Dark" : L"Light";

	// persist
	auto localSettings = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
	localSettings->Insert("theme", ref new Platform::String(theme));

	ApplyTheme(ref new Platform::String(theme));
}

void MoonlightSettings::ApplyTheme(Platform::String^ theme)
{
	// Apply theme to the root visual element so ThemeResources re-evaluate.
	// Setting Application::RequestedTheme can fail at runtime; use the root FrameworkElement's RequestedTheme (ElementTheme).
	auto content = Windows::UI::Xaml::Window::Current->Content;
	auto root = dynamic_cast<FrameworkElement^>(content);
	if (root != nullptr) {
		if (theme != nullptr && theme->Equals("Dark")) {
			root->RequestedTheme = Windows::UI::Xaml::ElementTheme::Dark;
		} else {
			root->RequestedTheme = Windows::UI::Xaml::ElementTheme::Light;
		}
	}
}

void MoonlightSettings::OnLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	m_back_cookie = navigation->BackRequested += ref new EventHandler<BackRequestedEventArgs^>(this, &MoonlightSettings::OnBackRequested);
}


void MoonlightSettings::OnUnloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	navigation->BackRequested -= m_back_cookie;
}
