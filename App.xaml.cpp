//
// App.xaml.cpp
// Implementation of the App class.
//

#include "pch.h"
#include <Utils.hpp>
#include "MoonlightWelcome.xaml.h"
#include "Pages\MoonlightSettings.xaml.h"
#include <windows.h>

static LONG WINAPI MoonlightUnhandledExceptionFilter(EXCEPTION_POINTERS* ep) {
	if (ep == nullptr) return EXCEPTION_CONTINUE_SEARCH;
	char buf[512];
	int len = sprintf_s(buf, sizeof(buf), "Unhandled native exception: code=0x%08X at address=%p\r\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
	OutputDebugStringA(buf);
	return EXCEPTION_CONTINUE_SEARCH;
}

static LONG CALLBACK MoonlightVectoredExceptionHandler(PEXCEPTION_POINTERS ep) {
	if (ep == nullptr) return EXCEPTION_CONTINUE_SEARCH;
	char buf[1024];
	auto code = ep->ExceptionRecord->ExceptionCode;
	void* addr = ep->ExceptionRecord->ExceptionAddress;
	auto ctx = ep->ContextRecord;
	int len = 0;
	len = sprintf_s(buf, sizeof(buf), "Vectored exception: code=0x%08X addr=%p thread=%u\r\n", code, addr, GetCurrentThreadId());
	OutputDebugStringA(buf);
	if (ctx) {
#if defined(_M_X64) || defined(__x86_64__)
		len = sprintf_s(buf, sizeof(buf), "RIP=%llx RSP=%llx RBP=%llx RAX=%llx RBX=%llx RCX=%llx RDX=%llx\r\n", (unsigned long long)ctx->Rip, (unsigned long long)ctx->Rsp, (unsigned long long)ctx->Rbp, (unsigned long long)ctx->Rax, (unsigned long long)ctx->Rbx, (unsigned long long)ctx->Rcx, (unsigned long long)ctx->Rdx);
		OutputDebugStringA(buf);
		len = sprintf_s(buf, sizeof(buf), "RSI=%llx RDI=%llx R8=%llx R9=%llx R10=%llx R11=%llx R12=%llx\r\n", (unsigned long long)ctx->Rsi, (unsigned long long)ctx->Rdi, (unsigned long long)ctx->R8, (unsigned long long)ctx->R9, (unsigned long long)ctx->R10, (unsigned long long)ctx->R11, (unsigned long long)ctx->R12);
		OutputDebugStringA(buf);
#else
		len = sprintf_s(buf, sizeof(buf), "Eip=%08x Esp=%08x Ebp=%08x Eax=%08x Ebx=%08x Ecx=%08x Edx=%08x\r\n", ctx->Eip, ctx->Esp, ctx->Ebp, ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx);
		OutputDebugStringA(buf);
#endif
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Storage;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Interop;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
/// <summary>
/// Initializes the singleton application object.  This is the first line of authored code
/// executed, and as such is the logical equivalent of main() or WinMain().
/// </summary>
App::App()
{
	InitializeComponent();
	RequiresPointerMode = Windows::UI::Xaml::ApplicationRequiresPointerMode::WhenRequested;
	Suspending += ref new SuspendingEventHandler(this, &App::OnSuspending);
	Resuming += ref new EventHandler<Object^>(this, &App::OnResuming);
	displayRequest = ref new Windows::System::Display::DisplayRequest();

	// Initialize global menu items
	GlobalMenuItems = ref new Platform::Collections::Vector<moonlight_xbox_dx::MenuItem^>();
	GlobalMenuItems->Append(ref new moonlight_xbox_dx::MenuItem(
		ref new Platform::String(L"Settings"),
		ref new Platform::String(L"\uE713"),
		ref new Windows::Foundation::EventHandler<Platform::Object^>([](Platform::Object^ s, Platform::Object^ e) {
			try {
				auto rootFrame = dynamic_cast<Windows::UI::Xaml::Controls::Frame^>(Windows::UI::Xaml::Window::Current->Content);
				if (rootFrame != nullptr) {
					rootFrame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightSettings::typeid));
				}
			} catch(...) {}
		})
	));
}

/// <summary>
/// Invoked when the application is launched normally by the end user.  Other entry points
/// will be used when the application is launched to open a specific file, to display
/// search results, and so forth.
/// </summary>
/// <param name="e">Details about the launch request and process.</param>
void App::OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs^ e)
{
// #if _DEBUG
// 	if (IsDebuggerPresent())
// 	{
// 		DebugSettings->EnableFrameRateCounter = true;
// 	}
// #endif
	moonlight_xbox_dx::Utils::Log("Hello from Moonlight!\n");
	// Register a native unhandled exception filter that writes directly to disk
	SetUnhandledExceptionFilter(MoonlightUnhandledExceptionFilter);

	// Also register a vectored exception handler to capture first-chance crashes and registers.
	// Note: AddVectoredExceptionHandler and direct file I/O are not available in all UWP
	// configurations; we use a debug-output based handler so this compiles across builds.
	// If AddVectoredExceptionHandler is available on the platform, the call can be
	// re-enabled conditionally.
	auto rootFrame = dynamic_cast<Frame^>(Window::Current->Content);

	// Do not repeat app initialization when the Window already has content,
	// just ensure that the window is active
	if (rootFrame == nullptr)
	{
		// Create a Frame to act as the navigation context and associate it with
		// a SuspensionManager key
		rootFrame = ref new Frame();

		rootFrame->NavigationFailed += ref new Windows::UI::Xaml::Navigation::NavigationFailedEventHandler(this, &App::OnNavigationFailed);

		// Place the frame in the current Window
		Window::Current->Content = rootFrame;
	}

	if (rootFrame->Content == nullptr)
	{
		// When the navigation stack isn't restored navigate to the first page,
		// configuring the new page by passing required information as a navigation
		// parameter
		rootFrame->Navigate(TypeName(HostSelectorPage::typeid), e->Arguments);
	}

	if (m_menuPage == nullptr)
	{
		m_menuPage = dynamic_cast<HostSelectorPage^>(rootFrame->Content);
	}
	// Ensure the current window is active
	Window::Current->Activate();
	//Start the state
	auto state = GetApplicationState();
	auto that = this;
	state->Init().then([that](){
		that->m_menuPage->OnStateLoaded();
	});
	displayRequest->RequestActive();
}
/// <summary>
/// Invoked when application execution is being suspended.  Application state is saved
/// without knowing whether the application will be terminated or resumed with the contents
/// of memory still intact.
/// </summary>
/// <param name="sender">The source of the suspend request.</param>
/// <param name="e">Details about the suspend request.</param>
void App::OnSuspending(Object^ sender, SuspendingEventArgs^ e)
{
	(void) sender;	// Unused parameter
	(void) e;	// Unused parameter
	displayRequest->RequestRelease();
}

/// <summary>
/// Invoked when application execution is being resumed.
/// </summary>
/// <param name="sender">The source of the resume request.</param>
/// <param name="args">Details about the resume request.</param>
void App::OnResuming(Object ^sender, Object ^args)
{
	(void) sender; // Unused parameter
	(void) args; // Unused parameter
	displayRequest->RequestActive();
}

/// <summary>
/// Invoked when Navigation to a certain page fails
/// </summary>
/// <param name="sender">The Frame which failed navigation</param>
/// <param name="e">Details about the navigation failure</param>
void App::OnNavigationFailed(Platform::Object ^sender, Windows::UI::Xaml::Navigation::NavigationFailedEventArgs ^e)
{
	e->Handled = true;
	Windows::UI::Xaml::Controls::ContentDialog^ dialog = ref new Windows::UI::Xaml::Controls::ContentDialog();
	dialog->Content = e->Exception.ToString();
	dialog->CloseButtonText = L"OK";
	dialog->ShowAsync();
	//throw ref new FailureException("Failed to load Page " + e->SourcePageType.Name);
}

