//
// App.xaml.h
// Declaration of the App class.
//

#pragma once

#include "App.g.h"
#include "Converters\MultiplyConverter.h"
#include "Converters\UniformThicknessConverter.h"
#include "Converters\BlurPaddingConverter.h"
#include <Pages\HostSelectorPage.xaml.h>
#include "Common\MenuItem.h"
#include <collection.h>
#include <vector>
#include <ppltasks.h>
#include <collection.h>
#include <windows.foundation.collections.h>

namespace moonlight_xbox_dx
{
		/// <summary>
	/// Provides application-specific behavior to supplement the default Application class.
	/// </summary>
	ref class App sealed
	{
	public:
		App();

		property Windows::Foundation::Collections::IObservableVector<moonlight_xbox_dx::MenuItem^>^ GlobalMenuItems;
		virtual void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs^ e) override;
		void OnStateLoaded();
	private:
		void OnSuspending(Platform::Object^ sender, Windows::ApplicationModel::SuspendingEventArgs^ e);
		void OnResuming(Platform::Object ^sender, Platform::Object ^args);
		void OnNavigationFailed(Platform::Object ^sender, Windows::UI::Xaml::Navigation::NavigationFailedEventArgs ^e);
		HostSelectorPage^ m_menuPage;
		Windows::System::Display::DisplayRequest^ displayRequest;
	};
}
