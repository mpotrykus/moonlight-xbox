//
// DirectXPage.xaml.cpp
// Implementation of the DirectXPage class.
//

#include "pch.h"
#include "StreamPage.xaml.h"
#include "../Streaming/FFMpegDecoder.h"
#include <Utils.hpp>
#include <KeyboardControl.xaml.h>
#include "../Common/ModalDialog.xaml.h"

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Graphics::Display;
using namespace Windows::System::Threading;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input;
using namespace Windows::UI::ViewManagement;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace concurrency;

StreamPage::StreamPage():
	m_windowVisible(true),
	m_coreInput(nullptr)
{
	InitializeComponent();

	DisplayInformation^ currentDisplayInformation = DisplayInformation::GetForCurrentView();
	NavigationCacheMode = Windows::UI::Xaml::Navigation::NavigationCacheMode::Enabled;
	swapChainPanel->SizeChanged +=
		ref new SizeChangedEventHandler(this, &StreamPage::OnSwapChainPanelSizeChanged);
	m_deviceResources = std::make_shared<DX::DeviceResources>();
}



void StreamPage::OnBackRequested(Platform::Object^ e,Windows::UI::Core::BackRequestedEventArgs^ args)
{
	// UWP on Xbox One triggers a back request whenever the B
	// button is pressed which can result in the app being
	// suspended if unhandled
	args->Handled = true;
}

void StreamPage::Page_Loaded(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {

	this->m_progressView->Visibility = Windows::UI::Xaml::Visibility::Visible;
	this->m_progressRing->IsActive = true;

	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	m_back_cookie = navigation->BackRequested += ref new EventHandler<BackRequestedEventArgs ^>(this, &StreamPage::OnBackRequested);

	Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->SetDesiredBoundsMode(Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow);

	keyDownHandler = (Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyDown += ref new Windows::Foundation::TypedEventHandler<Windows::UI::Core::CoreWindow ^, Windows::UI::Core::KeyEventArgs ^>(this, &StreamPage::OnKeyDown));
	keyUpHandler = (Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyUp += ref new Windows::Foundation::TypedEventHandler<Windows::UI::Core::CoreWindow ^, Windows::UI::Core::KeyEventArgs ^>(this, &StreamPage::OnKeyUp));
	
	try {
		m_deviceResources->SetSwapChainPanel(swapChainPanel);
	} catch (...) {
		Utils::Log("StreamPage::Page_Loaded: SetSwapChainPanel failed\n");
	}

	// Defer heavy initialization so the handler returns and the UI can present.
	// Use a low-priority dispatch so the framework can complete the first frame.
	Platform::WeakReference weakThis(this);
	auto ignore = this->Dispatcher->RunAsync(CoreDispatcherPriority::Low, ref new DispatchedHandler([weakThis]() {
		auto that = weakThis.Resolve<StreamPage>();
		if (that == nullptr) return;
		
		auto moonlightClient = new MoonlightClient();
		
		try {
			that->m_main = std::unique_ptr<moonlight_xbox_dxMain>(new moonlight_xbox_dxMain(that->m_deviceResources, that, moonlightClient, that->configuration));
			
			DISPATCH_UI([that], {
				that->m_main->CreateDeviceDependentResources();
				that->m_main->CreateWindowSizeDependentResources();
				that->m_main->StartRenderLoop();
			});
		} catch (const std::exception &ex) {
			that->HandleStreamException(Utils::StringPrintf(ex.what()), moonlightClient);
		} catch (const std::string &string) {
			that->HandleStreamException(Utils::StringPrintf(string.c_str()), moonlightClient);
		} catch (Platform::Exception ^ e) {
			Platform::String ^ errorMsg = ref new Platform::String();
			errorMsg = errorMsg->Concat(L"Exception: ", e->Message);
			errorMsg = errorMsg->Concat(errorMsg, Utils::StringPrintf("%x", e->HResult));
			that->HandleStreamException(errorMsg, moonlightClient);
		} catch (...) {
			that->HandleStreamException(L"Generic Exception", moonlightClient);
		}
	}));
}

void StreamPage::HandleStreamException(Platform::String ^ message, MoonlightClient *moonlightClient) {
	try {
		auto dialog = ref new Windows::UI::Xaml::Controls::ContentDialog();
		dialog->Content = message;
		dialog->PrimaryButtonText = L"OK";

		Platform::WeakReference weakThat(this);

		concurrency::create_task(dialog->ShowAsync()).then([moonlightClient, weakThat](concurrency::task<Windows::UI::Xaml::Controls::ContentDialogResult> t) {
			try {
				auto result = t.get();
				if (result == Windows::UI::Xaml::Controls::ContentDialogResult::Primary) {
					auto strongThat = weakThat.Resolve<StreamPage>();

					// Preferred: if m_main exists, call its Disconnect() — it calls StopStreaming()
					// and stops the scene renderer. This is the cleanest shutdown path for
					// the running render loop instance.
					if (strongThat != nullptr && strongThat->m_main) {
						Utils::Log("[StreamPage] HandleStreamException: calling m_main->Disconnect()\n");
						strongThat->m_main->Disconnect();

						// Attempt to navigate back; if Frame can't go back, navigate to HostSelectorPage.
						strongThat->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([strongThat]() {
							                                 if (strongThat->Frame != nullptr && strongThat->Frame->CanGoBack) {
								                                 strongThat->Frame->GoBack();
							                                 } else if (strongThat->Frame != nullptr) {
								                                 strongThat->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSelectorPage::typeid));
							                                 }
						                                 }));

						return;
					}

					// If m_main isn't available (initialization case), call StopStreaming on the
					// MoonlightClient instance you already created. This forces the library to
					// teardown and will trigger the connection_terminated callback which sets
					// the same atomic flag the render loop polls.
					if (moonlightClient) {
						Utils::Log("[StreamPage] HandleStreamException: calling moonlightClient->StopStreaming()\n");
						moonlightClient->StopStreaming();
						// Also set the flag as a fallback; StopStreaming should drive termination callbacks,
						// but setting the flag directly ensures the render-loop polling sees termination.
						moonlightClient->SetConnectionTerminated();

						// Navigate back (or to HostSelectorPage) on UI thread.
						auto pageWeak = weakThat;
						Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([pageWeak]() {
							                                                                                             auto strong = pageWeak.Resolve<StreamPage>();
							                                                                                             if (!strong) return;
							                                                                                             if (strong->Frame != nullptr && strong->Frame->CanGoBack) {
								                                                                                             strong->Frame->GoBack();
							                                                                                             } else if (strong->Frame != nullptr) {
								                                                                                             strong->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSelectorPage::typeid));
							                                                                                             }
						                                                                                             }));

						return;
					}

					// Last resort: flip the termination flag (no client instance available).
					Utils::Log("[StreamPage] HandleStreamException: no client instance available to stop; setting termination flag\n");
					// global flag is set by MoonlightClient::SetConnectionTerminated(); call it only if you have a pointer.
				}
			} catch (...) {
				Utils::Log("[StreamPage] HandleStreamException: dialog continuation threw\n");
			}
		});
	} catch (const std::exception &ex) {
		Utils::Logf("[StreamPage] HandleStreamException exception while showing dialog. Exception: %s\n", ex.what());
	} catch (...) {
		Utils::Log("[StreamPage] HandleStreamException exception while showing dialog. Unknown Exception.\n");
	}
}

void StreamPage::Page_Unloaded(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	navigation->BackRequested -= m_back_cookie;

	if (this->m_main) {

		Utils::Log("StreamPage::Page_Unloaded stopping m_main render loop\n");

		try {
			this->m_main->StopRenderLoop();
		} catch (...) {
			Utils::Log("StreamPage::Page_Unloaded StopRenderLoop threw an exception\n");
		}

		this->m_main.reset();
		Utils::Log("StreamPage::Page_Unloaded m_main reset\n");
	}

	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyDown -= keyDownHandler;
	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyUp -= keyUpHandler;
}

StreamPage::~StreamPage()
{
}

void StreamPage::OnSwapChainPanelSizeChanged(Object^ sender, Windows::UI::Xaml::SizeChangedEventArgs^ e)
{
	if (m_main == nullptr || m_deviceResources == nullptr)return;
	Utils::Logf("StreamPage::OnSwapChainPanelSizeChanged( NewSize: %f x %f )\n", e->NewSize.Width, e->NewSize.Height);
	critical_section::scoped_lock lock(m_main->GetCriticalSection());
	m_deviceResources->SetLogicalSize(e->NewSize);
	m_main->CreateDeviceDependentResources();
	m_main->CreateWindowSizeDependentResources();
}


void StreamPage::flyoutButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	Windows::UI::Xaml::Controls::Flyout::ShowAttachedFlyout((FrameworkElement^)sender);
	m_main->SetFlyoutOpened(true);
}


void StreamPage::ActionsFlyout_Closed(Platform::Object^ sender, Platform::Object^ e)
{
	if(m_main != nullptr) m_main->SetFlyoutOpened(false);
}


void StreamPage::toggleMouseButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	m_main->mouseMode = !m_main->mouseMode;
	this->toggleMouseButton->Text = m_main->mouseMode ? "Exit Mouse Mode" : "Toggle Mouse Mode";
}

void StreamPage::toggleLogsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	bool isVisible = m_main->ToggleLogs();
	this->toggleLogsButton->Text = isVisible ? "Hide Logs" : "Show Logs";
}

void StreamPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) {
	configuration = dynamic_cast<StreamConfiguration^>(e->Parameter);
	SetStreamConfig(configuration);

	if (configuration == nullptr)return;
}

void StreamPage::toggleStatsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	bool isVisible = m_main->ToggleStats();
	this->toggleStatsButton->Text = isVisible ? "Hide Stats" : "Show Stats";
}

void StreamPage::disonnectButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyDown -= keyDownHandler;
	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyUp -= keyUpHandler;

	// trigger the server disconnected flow
	this->m_main->moonlightClient->SetConnectionTerminated();
}

void StreamPage::OnKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ e)
{
	//Ignore Gamepad input
	if (e->VirtualKey >= Windows::System::VirtualKey::GamepadA && e->VirtualKey <= Windows::System::VirtualKey::GamepadRightThumbstickLeft) {
		return;
	}
	char modifiers = 0;
	modifiers |= CoreWindow::GetForCurrentThread()->GetKeyState(Windows::System::VirtualKey::Control) == (CoreVirtualKeyStates::Down) ? MODIFIER_CTRL : 0;
	modifiers |= CoreWindow::GetForCurrentThread()->GetKeyState(Windows::System::VirtualKey::Menu) == (CoreVirtualKeyStates::Down) ? MODIFIER_ALT : 0;
	modifiers |= CoreWindow::GetForCurrentThread()->GetKeyState(Windows::System::VirtualKey::Shift) == (CoreVirtualKeyStates::Down) ? MODIFIER_SHIFT : 0;
	this->m_main->OnKeyDown((unsigned short)e->VirtualKey,modifiers);
}


void StreamPage::OnKeyUp(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ e)
{
	//Ignore Gamepad input
	if (e->VirtualKey >= Windows::System::VirtualKey::GamepadA && e->VirtualKey <= Windows::System::VirtualKey::GamepadRightThumbstickLeft) {
		return;
	}
	char modifiers = 0;
	modifiers |= CoreWindow::GetForCurrentThread()->GetKeyState(Windows::System::VirtualKey::Control) == (CoreVirtualKeyStates::Down) ? MODIFIER_CTRL : 0;
	modifiers |= CoreWindow::GetForCurrentThread()->GetKeyState(Windows::System::VirtualKey::Menu) == (CoreVirtualKeyStates::Down) ? MODIFIER_ALT : 0;
	modifiers |= CoreWindow::GetForCurrentThread()->GetKeyState(Windows::System::VirtualKey::Shift) == (CoreVirtualKeyStates::Down) ? MODIFIER_SHIFT : 0;
	this->m_main->OnKeyUp((unsigned short) e->VirtualKey, modifiers);

}

void StreamPage::disconnectAndCloseButton_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyDown -= keyDownHandler;
	Windows::UI::Core::CoreWindow::GetForCurrentThread()->KeyUp -= keyUpHandler;
	if (this->m_main) {
		// trigger the server disconnected flow which will cleanly exit the loop and call StopRenderLoop()
		this->m_main->moonlightClient->SetConnectionTerminated();
	}

	auto that = this;

	auto progressToken = ::moonlight_xbox_dx::ModalDialog::ShowProgressDialogToken(nullptr, Utils::StringFromStdString("Closing..."));

	concurrency::create_task(concurrency::create_async([that, progressToken]() {
		try {
			if (that->m_main) {
				that->m_main->CloseApp();
			}
		} catch (...) {
		}
	})).then([that, progressToken](concurrency::task<void> t) {
		try {
			t.get();

			// UI is sent back to HostSelectorPage in StartRenderLoop(), after the loop exits
			// All we need to do is close the progress dialog

			DISPATCH_UI([progressToken], {
				::moonlight_xbox_dx::ModalDialog::HideDialogByToken(progressToken);
			});
		} catch (...) {
		}
	});
}

void StreamPage::Keyboard_OnKeyDown(KeyboardControl^ sender, KeyEvent^ e)
{
	this->m_main->OnKeyDown(e->VirtualKey, e->Modifiers);
}


void StreamPage::Keyboard_OnKeyUp(KeyboardControl^ sender, KeyEvent^ e)
{
	this->m_main->OnKeyUp(e->VirtualKey, e->Modifiers);
}


void StreamPage::guideButtonShort_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	this->m_main->SendGuideButton(500);
}


void StreamPage::guideButtonLong_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	this->m_main->SendGuideButton(3000);
}


void StreamPage::toggleHDR_WinAltB_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	this->m_main->SendWinAltB();
}