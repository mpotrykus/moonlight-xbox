//
// HostSelectorPage.xaml.cpp
// Implementation of the HostSelectorPage class
//

#include "pch.h"
#include "HostSelectorPage.xaml.h"
#include "UI\Backgrounds\DynamicBackgroundHost.xaml.h"
#include "UI\Controls\LunarPhaseControl.xaml.h"
#include "UI\Pages\AppPage\AppPage.xaml.h"
#include <State\MoonlightClient.h>
#include "UI\Pages\HostSettingsPage.xaml.h"
#include "Utils.hpp"
#include "UI\Utilities\XamlHelpers.h"
#include "UI\Utilities\ToastService.h"
#include "UI\Pages\MoonlightSettings.xaml.h"
#include "State\MDNSHandler.h"
#include "UI\Pages\MoonlightWelcome.xaml.h"
#include "UI\Modals\AlertDialog.xaml.h"
#include "UI\Modals\PairDialog.xaml.h"
#include "UI\Modals\ConfirmDialog.xaml.h"
#include "UI\Modals\HostActionsDialog.xaml.h"
#include "UI\Modals\TestConnectionResultDialog.xaml.h"
#include <string>
#include <algorithm>
#include <cmath>
#include <winsock2.h>
#include <Ws2tcpip.h>

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
using namespace Windows::UI::ViewManagement::Core;

HostSelectorPage::HostSelectorPage()
{
	state = GetApplicationState();
	InitializeComponent();
	m_hostsScrollViewer = nullptr;
}

Windows::UI::Xaml::Controls::ScrollViewer^ HostSelectorPage::FindScrollViewer(Windows::UI::Xaml::DependencyObject^ root)
{
	if (root == nullptr) return nullptr;
	if (auto sv = dynamic_cast<ScrollViewer^>(root)) return sv;

	try {
		int count = VisualTreeHelper::GetChildrenCount(root);
		for (int i = 0; i < count; ++i) {
			auto child = VisualTreeHelper::GetChild(root, i);
			auto found = FindScrollViewer(child);
			if (found != nullptr) return found;
		}
	} catch (...) {}

	return nullptr;
}

// Layout widths once LunarPhaseControl reaches its final state:
//   selected:     LunarPhaseControl.Width(160) + LunarPhase margins(24+24) + ItemRoot margins(8+8)
//   non-selected: LunarPhaseControl.Width(96)  + LunarPhase margins(24+24) + ItemRoot margins(8+8)
static const double kSelectedHostContainerWidth =    230; // 224.0;
static const double kNonSelectedHostContainerWidth = 166; // 160.0;

void HostSelectorPage::EnsureCenteringPadding(int attempts)
{
	try {
		if (m_adjustingCenterPadding) return;

		auto queueRetry = [this, attempts]() {
			if (attempts <= 0 || this->Dispatcher == nullptr) return;
			auto weakThis = WeakReference(this);
			try {
				this->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
					ref new Windows::UI::Core::DispatchedHandler([weakThis, attempts]() {
						auto that = weakThis.Resolve<HostSelectorPage>();
						if (that == nullptr) return;
						that->EnsureCenteringPadding(attempts - 1);
					}));
			} catch (...) {}
		};

		auto grid = this->HostsGrid;
		if (grid == nullptr || grid->SelectedItem == nullptr) return;
		if (m_hostsScrollViewer == nullptr) m_hostsScrollViewer = FindScrollViewer(grid);
		if (m_hostsScrollViewer == nullptr) {
			queueRetry();
			return;
		}

		auto container = dynamic_cast<ListViewItem^>(grid->ContainerFromItem(grid->SelectedItem));
		if (container == nullptr) {
			try { grid->ScrollIntoView(grid->SelectedItem); } catch (...) {}
			queueRetry();
			return;
		}

		double viewport = m_hostsScrollViewer->ViewportWidth;
		// Use the known final selected-container width rather than ActualWidth, which is
		// stale during the LunarPhaseControl width animation triggered by selection change.
		double width = kSelectedHostContainerWidth;
		if (!std::isfinite(viewport) || viewport <= 0.0) {
			queueRetry();
			return;
		}

		double desired = std::max(0.0, (viewport - width) * 0.5);
		if (!std::isfinite(desired)) return;
		if (m_lastCenterPadding >= 0.0 && std::fabs(m_lastCenterPadding - desired) < 0.5) return;

		auto p = grid->Padding;
		if (std::fabs(p.Left - desired) < 0.5 && std::fabs(p.Right - desired) < 0.5) {
			m_lastCenterPadding = desired;
			return;
		}

		m_adjustingCenterPadding = true;
		p.Left = desired;
		p.Right = desired;
		grid->Padding = p;
		m_lastCenterPadding = desired;
		m_adjustingCenterPadding = false;
	} catch (...) {
		m_adjustingCenterPadding = false;
	}
}

void HostSelectorPage::CenterSelectedHost(int attempts, bool immediate)
{
	try {
		auto queueRetry = [this, attempts, immediate]() {
			if (attempts <= 0 || this->Dispatcher == nullptr) return;
			auto weakThis = WeakReference(this);
			try {
				this->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
					ref new Windows::UI::Core::DispatchedHandler([weakThis, attempts, immediate]() {
						auto that = weakThis.Resolve<HostSelectorPage>();
						if (that == nullptr) return;
						that->CenterSelectedHost(attempts - 1, immediate);
					}));
			} catch (...) {}
		};

		auto grid = this->HostsGrid;
		if (grid == nullptr || grid->SelectedIndex < 0 || grid->SelectedItem == nullptr) return;

		EnsureCenteringPadding(2);

		if (m_hostsScrollViewer == nullptr) m_hostsScrollViewer = FindScrollViewer(grid);
		if (m_hostsScrollViewer == nullptr) {
			queueRetry();
			return;
		}

		auto item = grid->SelectedItem;
		ListViewItem^ container = nullptr;
		try {
			container = dynamic_cast<ListViewItem^>(grid->ContainerFromItem(item));
		} catch (...) {}
		if (container == nullptr) {
			try { grid->ScrollIntoView(item); } catch (...) {}
			queueRetry();
			return;
		}

		// Derive scroll target analytically from steady-state layout.
		// padding = (viewport - kSelectedHostContainerWidth) / 2
		// target  = padding + index * kNonSelectedHostContainerWidth
		//           + kSelectedHostContainerWidth / 2 - viewport / 2
		//         = index * kNonSelectedHostContainerWidth
		// (padding and selected-width terms cancel exactly, so no viewport read needed.)
		double scrollable = m_hostsScrollViewer->ScrollableWidth;
		double current = m_hostsScrollViewer->HorizontalOffset;
		if (!std::isfinite(scrollable) || !std::isfinite(current)) {
			queueRetry();
			return;
		}
		double target = grid->SelectedIndex * kNonSelectedHostContainerWidth;
		target = std::max(0.0, std::min(target, std::max(0.0, scrollable)));

		if (std::fabs(target - current) < 0.5) return;

		try {
			m_hostsScrollViewer->ChangeView(target, nullptr, nullptr, immediate);
		} catch (...) {}
	} catch (...) {}
}

void HostSelectorPage::HostsGrid_Loaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^)
{
	try {
		auto grid = dynamic_cast<ListViewBase^>(sender);
		if (grid == nullptr) return;
		if (m_hostsScrollViewer == nullptr) m_hostsScrollViewer = FindScrollViewer(grid);

		try {
			if (grid->Items != nullptr && grid->Items->Size > 0 && grid->SelectedIndex < 0) {
				grid->SelectedIndex = 0;
			}
			if (grid->Items != nullptr && grid->Items->Size > 0) {
				Platform::WeakReference weakThis(this);
				this->Dispatcher->RunAsync(
					Windows::UI::Core::CoreDispatcherPriority::Low,
					ref new Windows::UI::Core::DispatchedHandler([weakThis]() {
						auto that = weakThis.Resolve<HostSelectorPage>();
						if (that != nullptr) that->FocusFirstHostItem(4);
					}));
			}
		} catch (...) {}

		// Subscribe to LayoutUpdated so we update moon phases after the layout pass
		// that creates containers — more reliable than fixed-count Normal-priority retries,
		// which can all fire before the first layout pass on cold launch when SavedHosts
		// is populated from a background thread after the page is first shown.
		if (m_hostsGrid_ccc_token.Value == 0) {
			Platform::WeakReference weakThis(this);
			m_hostsGrid_ccc_token = grid->LayoutUpdated +=
				ref new EventHandler<Platform::Object^>([weakThis](Platform::Object^, Platform::Object^) {
					auto that = weakThis.Resolve<HostSelectorPage>();
					if (that == nullptr) return;
					if (that->HostsGrid == nullptr ||
						that->HostsGrid->Items == nullptr ||
						that->HostsGrid->Items->Size == 0) return;
					// Unregister before calling so a stray re-fire can't double-run.
					try { that->HostsGrid->LayoutUpdated -= that->m_hostsGrid_ccc_token; } catch(...) {}
					that->m_hostsGrid_ccc_token.Value = 0;
					that->UpdateAllMoonPhases(false, 0);
					that->FocusFirstHostItem(4);
				});
		}

		EnsureCenteringPadding(3);
		CenterSelectedHost(4, true);
		UpdateAllMoonPhases(false, 4);
	} catch (...) {}
}

void HostSelectorPage::HostsGrid_SizeChanged(Platform::Object^, Windows::UI::Xaml::SizeChangedEventArgs^)
{
	try {
		if (m_adjustingCenterPadding) return;
		EnsureCenteringPadding(2);
		CenterSelectedHost(2, true);
	} catch (...) {}
}

void HostSelectorPage::HostsGrid_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^)
{
	try {
		auto grid = dynamic_cast<ListViewBase^>(sender);
		if (grid == nullptr || grid->SelectedIndex < 0) return;
		CenterSelectedHost(4, false);
		UpdateAllMoonPhases(true);
	} catch (...) {}

	try {
		auto grid = dynamic_cast<ListViewBase^>(sender);
		auto selectedHost = (grid != nullptr && grid->SelectedItem != nullptr)
			? dynamic_cast<MoonlightHost^>(grid->SelectedItem)
			: nullptr;

		auto bgKey = (selectedHost != nullptr) ? selectedHost->Personalization->Background : nullptr;
		auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
		if (bgKey != nullptr && !bgKey->IsEmpty()) {
			ls->Insert("background", bgKey);
		} else {
			ls->Insert("background", DynamicBackgroundHost::DefaultKey);
		}

		if (BackgroundHost != nullptr) {
			auto singleHost = ref new Platform::Collections::Vector<MoonlightHost^>();
			if (selectedHost != nullptr) singleHost->Append(selectedHost);
			BackgroundHost->SetHosts(singleHost->Size > 0 ? (IVector<MoonlightHost^>^)singleHost : State->SavedHosts);
			BackgroundHost->Refresh();
			BackgroundHost->StartAnimations();
		}

		Windows::UI::Color accentColor = (selectedHost != nullptr && !selectedHost->Personalization->UseSystemAccent)
			? selectedHost->Personalization->AccentColor
			: (ref new Windows::UI::ViewManagement::UISettings())
				->GetColorValue(Windows::UI::ViewManagement::UIColorType::Accent);
		ApplyAccentColor(accentColor);
	} catch (...) {}

	// Fade host name banner: fade out → update text → fade in
	try {
		auto grid = dynamic_cast<Windows::UI::Xaml::Controls::ListViewBase^>(sender);
		auto selectedHost = (grid != nullptr && grid->SelectedItem != nullptr)
			? dynamic_cast<MoonlightHost^>(grid->SelectedItem)
			: nullptr;

		if (this->SelectedHostBanner != nullptr && this->SelectedHostText != nullptr) {
			Platform::String^ newName = (selectedHost != nullptr && selectedHost->ComputerName != nullptr)
				? selectedHost->ComputerName
				: ref new Platform::String(L"");

			Windows::UI::Xaml::Media::Animation::Storyboard^ showSb = nullptr;
			Windows::UI::Xaml::Media::Animation::Storyboard^ hideSb = nullptr;
			auto res = this->Resources;
			if (res != nullptr) {
				try { showSb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(res->Lookup(ref new Platform::String(L"ShowSelectedHostStoryboard"))); } catch (...) {}
				try { hideSb = dynamic_cast<Windows::UI::Xaml::Media::Animation::Storyboard^>(res->Lookup(ref new Platform::String(L"HideSelectedHostStoryboard"))); } catch (...) {}
			}

			bool alreadyVisible = this->SelectedHostBanner->Opacity > 0.01;
			if (selectedHost == nullptr) {
				// No valid selection — hide if visible, don't show again
				if (alreadyVisible && hideSb != nullptr) hideSb->Begin();
				this->SelectedHostText->Text = ref new Platform::String(L"");
			} else if (alreadyVisible && hideSb != nullptr && showSb != nullptr) {
				const unsigned int animVer = ++m_hostTextAnimVersion;
				Platform::WeakReference weakThis(this);
				auto capturedName = newName;
				auto capturedShow = showSb;
				auto token = std::make_shared<Windows::Foundation::EventRegistrationToken>();
				*token = hideSb->Completed += ref new Windows::Foundation::EventHandler<Platform::Object^>(
					[weakThis, capturedName, hideSb, capturedShow, token, animVer](Platform::Object^, Platform::Object^) mutable {
						try { hideSb->Completed -= *token; } catch (...) {}
						auto that = weakThis.Resolve<HostSelectorPage>();
						if (that == nullptr || that->m_hostTextAnimVersion != animVer) return;
						try {
							if (that->SelectedHostText != nullptr) that->SelectedHostText->Text = capturedName;
							if (capturedShow != nullptr) capturedShow->Begin();
						} catch (...) {}
					});
				hideSb->Begin();
			} else {
				this->SelectedHostText->Text = newName;
				if (showSb != nullptr) showSb->Begin();
			}
		}
	} catch (...) {}
}

void HostSelectorPage::NewHostButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto dialog = ref new AddHostDialog();
	Platform::WeakReference weakThis(this);

	dialog->Configure(
		ref new Windows::UI::Xaml::RoutedEventHandler([weakThis, dialog](Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^) {
			auto that = weakThis.Resolve<HostSelectorPage>();
			if (that == nullptr) return;
			Platform::String^ hostname = dialog->GetHostname();
			if (hostname == nullptr || hostname->Length() == 0) return;
			dialog->SetAddButtonEnabled(false);
			Concurrency::create_task([that, dialog, hostname]() {
				bool status = that->state->AddHost(hostname);
				Platform::WeakReference weakPage(that);
				Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
					Windows::UI::Core::CoreDispatcherPriority::High,
					ref new Windows::UI::Core::DispatchedHandler([weakPage, dialog, status, hostname]() {
						if (!status) {
							dialog->ShowError("Failed to connect to " + hostname);
							dialog->SetAddButtonEnabled(true);
						} else {
							try { dialog->Hide(); } catch (...) {}
							auto page = weakPage.Resolve<HostSelectorPage>();
							if (page != nullptr) {
								try {
									if (page->HostsGrid != nullptr && page->HostsGrid->SelectedIndex < 0 &&
										page->HostsGrid->Items != nullptr && page->HostsGrid->Items->Size > 0) {
										page->HostsGrid->SelectedIndex = 0;
									}
								} catch (...) {}
							}
						}
					}));
			});
		}),
		nullptr
	);

	dialog->SetRecentHostnames(state->RecentHostnames);
	try { dialog->XamlRoot = this->XamlRoot; } catch (...) {}
	concurrency::create_task(dialog->ShowAsync());
}

void HostSelectorPage::GridView_ItemClick(Platform::Object^ sender, Windows::UI::Xaml::Controls::ItemClickEventArgs^ e)
{
	MoonlightHost^ host = (MoonlightHost^)e->ClickedItem;

	if (host->Connected && host->Paired) {
		this->Connect(host);
		return;
	}

	if (host->Connected && !host->Paired) {
		this->StartPairing(host);
		return;
	}

	this->ShowHostActions(host);
}

void HostSelectorPage::ShowHostActions(MoonlightHost^ host)
{
    currentHost = host;
    if (currentHost == nullptr) return;

    bool showWake = !(currentHost->Connected || currentHost->WolPolling);
    bool showTest = !showWake;

    auto dialog = ref new HostActionsDialog();
    m_hostActionsDialog = dialog;

    Platform::WeakReference weakThis(this);

    dialog->Configure(
        currentHost->ComputerName,
        showWake,
        showTest,
        ref new Windows::UI::Xaml::RoutedEventHandler([weakThis](Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^) {
            auto that = weakThis.Resolve<HostSelectorPage>();
            if (that == nullptr || that->currentHost == nullptr) return;
            auto t = ref new Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionInfo();
            t->Effect = Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionEffect::FromRight;
            that->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSettingsPage::typeid), that->currentHost, t);
        }),
        ref new Windows::UI::Xaml::RoutedEventHandler([weakThis](Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^) {
            auto that = weakThis.Resolve<HostSelectorPage>();
            if (that == nullptr || that->currentHost == nullptr) return;
            that->wakeHostButton_Click(nullptr, nullptr);
        }),
        ref new Windows::UI::Xaml::RoutedEventHandler([weakThis](Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^) {
            auto that = weakThis.Resolve<HostSelectorPage>();
            if (that == nullptr || that->currentHost == nullptr) return;
            that->testConnectionButton_Click(nullptr, nullptr);
        }),
        ref new Windows::UI::Xaml::RoutedEventHandler([weakThis](Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^) {
            auto that = weakThis.Resolve<HostSelectorPage>();
            if (that == nullptr || that->currentHost == nullptr) return;
            auto confirmDialog = ref new ConfirmDialog();
            Platform::String^ hostName = that->currentHost->ComputerName;
            Platform::String^ message = "Remove '" + hostName + "' from your saved hosts?";
            confirmDialog->Configure(
                "Remove Host",
		        message,
		        L"\xE74D",
                "Remove",
                "Cancel",
                ref new Windows::UI::Xaml::RoutedEventHandler([weakThis](Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^) {
                    auto that = weakThis.Resolve<HostSelectorPage>();
                    if (that == nullptr || that->currentHost == nullptr) return;
                    int removedIdx = that->HostsGrid->SelectedIndex;
                    that->State->RemoveHost(that->currentHost);
                    that->currentHost = nullptr;
                    int newSize = (int)that->State->SavedHosts->Size;
                    if (newSize > 0) {
                        that->HostsGrid->SelectedIndex = removedIdx < newSize ? removedIdx : newSize - 1;
                    } else {
                        if (that->BackgroundHost != nullptr)
                            that->BackgroundHost->ResetBackground();
                    }
                })
            );
            try { confirmDialog->XamlRoot = that->XamlRoot; } catch (...) {}
            concurrency::create_task(confirmDialog->ShowAsync());
        }),
        ref new Windows::UI::Xaml::RoutedEventHandler([weakThis](Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^) {
            auto that = weakThis.Resolve<HostSelectorPage>();
            if (that == nullptr) return;
            auto t = ref new Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionInfo();
            t->Effect = Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionEffect::FromRight;
            that->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightSettings::typeid), nullptr, t);
        })
    );

    try {
        dialog->XamlRoot = this->XamlRoot;
    } catch (...) {}

    concurrency::create_task(dialog->ShowAsync());
}

void HostSelectorPage::StartPairing(MoonlightHost^ host) {
	MoonlightClient* client = new MoonlightClient();
	char ipAddressStr[2048];
	wcstombs_s(NULL, ipAddressStr, host->LastHostname->Data(), 2047);
	int status = client->Connect(ipAddressStr);
	if (status != 0)return;
	char* pin = client->GeneratePIN();
	auto dialog = ref new ::moonlight_xbox_dx::PairDialog();
	wchar_t wpin[32];
	mbstowcs_s(NULL, wpin, pin, 31);
	dialog->Configure(ref new Platform::String(wpin));
	try { dialog->XamlRoot = this->XamlRoot; } catch (...) {}
	concurrency::create_task(dialog->ShowAsync());
	Concurrency::create_task([dialog, host, client, pin]() {
			int a = client->Pair();
		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([a, dialog, host]()
			{
				if (a == 0) {
						try { dialog->Hide(); } catch (...) {}
				}
				else {
				}
					host->UpdateHostInfo(true);
			}
			));
		}) .then([](concurrency::task<void> t) {
			try {
				t.get();
			}
			catch (const std::exception &e) {
				Utils::Logf("HostSelectorPage StartPairing task exception: %s", e.what());
			}
			catch (...) {
				Utils::Log("HostSelectorPage StartPairing task unknown exception");
			}
		});
}

void HostSelectorPage::HostsGrid_RightTapped(Platform::Object^ sender, Windows::UI::Xaml::Input::RightTappedRoutedEventArgs^ e)
{
	FrameworkElement^ senderElement = dynamic_cast<FrameworkElement^>(e->OriginalSource);
	if (senderElement == nullptr) return;

	auto itemContainer = dynamic_cast<SelectorItem^>(senderElement);
	MoonlightHost^ host = nullptr;
	if (itemContainer != nullptr) {
		host = dynamic_cast<MoonlightHost^>(itemContainer->Content);
	}
	else {
		host = dynamic_cast<MoonlightHost^>(senderElement->DataContext);
	}

	this->ShowHostActions(host);
}

void HostSelectorPage::SettingsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto slideForward = ref new Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionInfo();
	slideForward->Effect = Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionEffect::FromRight;
	this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightSettings::typeid), nullptr, slideForward);
}

void HostSelectorPage::OnStateLoaded() {
	if (GetApplicationState()->FirstTime) {
		this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightWelcome::typeid), nullptr, ref new Windows::UI::Xaml::Media::Animation::EntranceNavigationTransitionInfo());
		return;
	}

	// Items are now populated. Dispatch at Low priority so the layout pass
	// that creates item containers completes before we try to update phases.
	{
		auto weakThis = WeakReference(this);
		try {
			this->Dispatcher->RunAsync(
				Windows::UI::Core::CoreDispatcherPriority::Low,
				ref new Windows::UI::Core::DispatchedHandler([weakThis]() {
					auto that = weakThis.Resolve<HostSelectorPage>();
					if (that != nullptr) that->UpdateAllMoonPhases(true, 4);
				}));
		} catch (...) {}
	}

	// Focus the first host item — items were empty at Loaded time so we do it here.
	{
		auto weakThis = WeakReference(this);
		try {
			this->Dispatcher->RunAsync(
				Windows::UI::Core::CoreDispatcherPriority::Low,
				ref new Windows::UI::Core::DispatchedHandler([weakThis]() {
					auto that = weakThis.Resolve<HostSelectorPage>();
					if (that != nullptr) that->FocusFirstHostItem(4);
				}));
		} catch (...) {}
	}

	Concurrency::create_task([this]() {
		for (auto a : GetApplicationState()->SavedHosts) {
			a->UpdateHostInfo(false);
		}
	}).then([this]() {
		if (GetApplicationState()->autostartInstance.size() > 0) {
			auto pii = Utils::StringFromStdString(GetApplicationState()->autostartInstance);
			for (unsigned int i = 0; i < GetApplicationState()->SavedHosts->Size; i++) {
				auto host = GetApplicationState()->SavedHosts->GetAt(i);
				if (host->InstanceId->Equals(pii)) {
					auto that = this;
					Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
						Windows::UI::Core::CoreDispatcherPriority::High,
						ref new Windows::UI::Core::DispatchedHandler([that, host]() {
							that->Connect(host);
						})
					);
					break;
				}
			}
		}
	}).then([this](concurrency::task<void> t) {
		try {
			t.get();
		}
		catch (const std::exception &e) {
			Utils::Logf("HostSelectorPage OnStateLoaded task exception: %s", e.what());
		}
		catch (...) {
			Utils::Log("HostSelectorPage OnStateLoaded task unknown exception");
		}
	});
}

void HostSelectorPage::Connect(MoonlightHost^ host) {
	if (!host->Connected)return;
	if (!host->Paired) {
		StartPairing(host);
		return;
	}
	state->shouldAutoConnect = true;
	continueFetch.store(false);

	// Re-verify connectivity with a quick TCP check before navigating — faster than a full
	// UpdateHostInfo handshake, so the dialog appears in ~1 s instead of the full GFE timeout.
	Platform::WeakReference weakThis(this);
	std::string hostOnly = Utils::PlatformStringToStdString(host->LastHostname);
	auto colonPos = hostOnly.find(':');
	if (colonPos != std::string::npos) hostOnly = hostOnly.substr(0, colonPos);

	Concurrency::create_task([weakThis, host, hostOnly]() {
		bool reachable = false;
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
			struct addrinfo hints = {}, *res = nullptr;
			hints.ai_family   = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;
			if (getaddrinfo(hostOnly.c_str(), "47989", &hints, &res) == 0) {
				SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
				if (s != INVALID_SOCKET) {
					u_long mode = 1;
					ioctlsocket(s, FIONBIO, &mode);
					connect(s, res->ai_addr, (int)res->ai_addrlen);
					fd_set writeSet; FD_ZERO(&writeSet); FD_SET(s, &writeSet);
					timeval tv; tv.tv_sec = 1; tv.tv_usec = 0;
					reachable = (select(0, NULL, &writeSet, NULL, &tv) > 0 && FD_ISSET(s, &writeSet));
					closesocket(s);
				}
				freeaddrinfo(res);
			}
			WSACleanup();
		}

		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
			Windows::UI::Core::CoreDispatcherPriority::High,
			ref new Windows::UI::Core::DispatchedHandler([weakThis, host, reachable]() {
				auto that = weakThis.Resolve<HostSelectorPage>();
				if (that == nullptr) return;
				if (!reachable) {
					that->continueFetch.store(true);
					auto dialog = ref new ::moonlight_xbox_dx::AlertDialog();
					dialog->Configure(L"Host Offline", L"The host is not reachable. It may have gone offline.");
					try { dialog->XamlRoot = that->XamlRoot; } catch (...) {}
					concurrency::create_task(dialog->ShowAsync());
					return;
				}
				auto slideForward = ref new Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionInfo();
				slideForward->Effect = Windows::UI::Xaml::Media::Animation::SlideNavigationTransitionEffect::FromRight;
				that->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(AppPage::typeid), host, slideForward);
			}));
	});
}

void HostSelectorPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) {
	Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->SetDesiredBoundsMode(Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow);
	continueFetch.store(true);
	m_isNavigatedAway.store(false);

	try {
		if (BackgroundHost != nullptr) {
			BackgroundHost->SetHosts(State->SavedHosts);
			BackgroundHost->Refresh();
			BackgroundHost->StartAnimations();
		}
	} catch (...) {}

	using namespace Windows::System::Threading;
	using namespace Windows::Foundation;

	try {
		TimeSpan period;
		period.Duration = 5000 * 10000LL;
		Platform::WeakReference weakThis(this);
		auto callback = ref new TimerElapsedHandler([weakThis](ThreadPoolTimer^ timer) {
			auto that = weakThis.Resolve<HostSelectorPage>();
			if (that == nullptr) {
				try {
					if (timer != nullptr) timer->Cancel();
				} catch (...) {
				}
				return;
			}
			if (that->m_isNavigatedAway.load()) {
				try {
					if (timer != nullptr) timer->Cancel();
				} catch (...) {
				}
				return;
			}

			that->m_pollActiveCount.fetch_add(1);
			if (!that->continueFetch.load()) return;
			try {
				try {
					mdns_send_query();
				} catch (...) {
				}
				query_mdns();
				// Snapshot to avoid out-of-bounds if a host is deleted mid-iteration
				std::vector<MoonlightHost^> hostsSnapshot;
				{
					auto savedHosts = GetApplicationState()->SavedHosts;
					for (unsigned int i = 0; i < savedHosts->Size; i++) {
						try {
							hostsSnapshot.push_back(savedHosts->GetAt(i));
						} catch (...) {
							break;
						}
					}
				}
				for (auto a : hostsSnapshot) {
					try {
						a->UpdateHostInfo(true);
					} catch (...) {
					}
				}
			} catch (const std::exception &ex) {
				Utils::Logf("HostSelectorPage poll exception: %s", ex.what());
			} catch (...) {
				Utils::Log("HostSelectorPage poll unknown exception");
			}
			that->m_pollActiveCount.fetch_sub(1);
		});

		m_pollTimer = ThreadPoolTimer::CreatePeriodicTimer(callback, period);
	} catch (...) {
		Utils::Log("HostSelectorPage failed to start poll timer");
	}
}

void HostSelectorPage::OnNavigatedFrom(Windows::UI::Xaml::Navigation::NavigationEventArgs ^ e) {
	try {
		if (BackgroundHost != nullptr) BackgroundHost->StopAnimations();
	} catch (...) {}

	try {
		if (m_hostsGrid_ccc_token.Value != 0 && HostsGrid != nullptr) {
			HostsGrid->LayoutUpdated -= m_hostsGrid_ccc_token;
			m_hostsGrid_ccc_token.Value = 0;
		}
	} catch (...) {}

	m_isNavigatedAway.store(true);
	continueFetch.store(false);
	try {
		if (m_pollTimer != nullptr) {
			m_pollTimer->Cancel();
			m_pollTimer = nullptr;
		}
	} catch (...) {
	}

	for (int i = 0; i < 50 && m_pollActiveCount.load() > 0; ++i) {
		Sleep(20);
	}

	try {
		__super::OnNavigatedFrom(e);
	} catch (...) {
	}
}

void HostSelectorPage::OnKeyDown(Platform::Object ^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs ^ e) {
	if (e->Key == Windows::System::VirtualKey::Enter) {
		CoreInputView::GetForCurrentView()->TryHide();
	}
}

void moonlight_xbox_dx::HostSelectorPage::wakeHostButton_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	if (currentHost == nullptr) {
		return;
	}

	try {
		bool success = State->WakeHost(currentHost);
		if (!success) {
			auto fail = ref new ::moonlight_xbox_dx::AlertDialog();
			fail->Configure("Wake Host Failed", "Failed to send Wake-on-LAN packet.\n\nPlease check if Wake-on-LAN is enabled on the host.");
			try { fail->XamlRoot = this->XamlRoot; } catch (...) {}
			concurrency::create_task(fail->ShowAsync());
		}

		if (success) {
			ShowToast(L"Wake on LAN sent");
			auto host = currentHost;
			host->WolPolling = true;
			concurrency::create_task(concurrency::create_async([host]() {
				int consecutiveSuccess = 0;
				for (int i = 0; i < 240; ++i) {
					try {
						host->UpdateHostInfo(false);
						if (host->Connected) {
							consecutiveSuccess++;
							if (consecutiveSuccess >= 2) {
								host->WolPolling = false;
								break;
							}
						} else {
							consecutiveSuccess = 0;
						}
					} catch (...) {
						consecutiveSuccess = 0;
					}
					Sleep(250);
				}
			})).then([host](concurrency::task<void> t) {
				try {
					t.get();
				} catch (...) {
				}
				Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([host]() {
					                                                                                             host->WolPolling = false;
				                                                                                             }));
			});
		}
	} catch (std::exception ex) {
		auto errDlg = ref new ::moonlight_xbox_dx::AlertDialog();
		errDlg->Configure("Wake Host Error", "An error occurred while trying to wake the host:\n\n" + Utils::StringFromChars((char *)ex.what()));
		try { errDlg->XamlRoot = this->XamlRoot; } catch (...) {}
		concurrency::create_task(errDlg->ShowAsync());
	}
}

LunarPhaseControl^ HostSelectorPage::FindLunarControl(Windows::UI::Xaml::DependencyObject^ root)
{
	if (root == nullptr) return nullptr;
	if (auto ctrl = dynamic_cast<LunarPhaseControl^>(root)) return ctrl;
	try {
		int count = VisualTreeHelper::GetChildrenCount(root);
		for (int i = 0; i < count; ++i) {
			auto found = FindLunarControl(VisualTreeHelper::GetChild(root, i));
			if (found != nullptr) return found;
		}
	} catch (...) {}
	return nullptr;
}

void HostSelectorPage::FocusFirstHostItem(int attempts)
{
	try {
		auto grid = HostsGrid;
		if (grid == nullptr || grid->Items == nullptr || grid->Items->Size == 0) return;
		if (grid->SelectedIndex < 0) grid->SelectedIndex = 0;
		int idx = grid->SelectedIndex >= 0 ? grid->SelectedIndex : 0;
		auto container = dynamic_cast<Windows::UI::Xaml::Controls::Control^>(grid->ContainerFromIndex(idx));
		if (container != nullptr) {
			container->Focus(Windows::UI::Xaml::FocusState::Programmatic);
			return;
		}
		if (attempts <= 0 || Dispatcher == nullptr) return;
		Platform::WeakReference weakThis(this);
		try {
			Dispatcher->RunAsync(
				Windows::UI::Core::CoreDispatcherPriority::Normal,
				ref new Windows::UI::Core::DispatchedHandler([weakThis, attempts]() {
					auto that = weakThis.Resolve<HostSelectorPage>();
					if (that != nullptr) that->FocusFirstHostItem(attempts - 1);
				}));
		} catch (...) {}
	} catch (...) {}
}

void HostSelectorPage::UpdateAllMoonPhases(bool animated, int attempts)
{
	try {
		auto grid = HostsGrid;
		if (grid == nullptr) return;
		int selectedIdx = grid->SelectedIndex;
		if (selectedIdx < 0) selectedIdx = 0;
		unsigned int count = grid->Items->Size;
		bool anyMissing = false;
		for (unsigned int i = 0; i < count; ++i) {
			try {
				auto container = dynamic_cast<Windows::UI::Xaml::DependencyObject^>(
					grid->ContainerFromIndex(i));
				if (container == nullptr) { anyMissing = true; continue; }
				auto ctrl = FindLunarControl(container);
				if (ctrl == nullptr) { anyMissing = true; continue; }
				int dist = (int)i - selectedIdx;
				double fillAmount = dist < 0 ? -dist * 0.4 : dist * 0.4;
				if (fillAmount > 1.0) fillAmount = 1.0;
				int side = dist < 0 ? 1 : dist > 0 ? -1 : 0;
				ctrl->UpdatePhase(fillAmount, side, animated);
				ctrl->SetSelected(i == (unsigned int)selectedIdx, animated);
			} catch (...) {}
		}
		if (anyMissing && attempts > 0) {
			Platform::WeakReference weakThis(this);
			bool capturedAnimated = animated;
			int next = attempts - 1;
			try {
				this->Dispatcher->RunAsync(
					Windows::UI::Core::CoreDispatcherPriority::Normal,
					ref new Windows::UI::Core::DispatchedHandler([weakThis, capturedAnimated, next]() {
						auto that = weakThis.Resolve<HostSelectorPage>();
						if (that != nullptr) that->UpdateAllMoonPhases(capturedAnimated, next);
					}));
			} catch (...) {}
		}
	} catch (...) {}
}

void HostSelectorPage::testConnectionButton_Click(Platform::Object ^ sender, Windows::UI::Xaml::RoutedEventArgs ^ e) {
	if (currentHost == nullptr) return;

	std::string hostname = Utils::PlatformStringToStdString(currentHost->LastHostname);
	auto pos = hostname.find(':');
	std::string hostOnly = (pos == std::string::npos) ? hostname : hostname.substr(0, pos);

	Platform::WeakReference weakThis(this);
	concurrency::create_task([hostOnly, weakThis]() {
		WSADATA wsaData;
		std::string resultMsg = "Unknown";
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			resultMsg = "WSAStartup failed";
		} else {
			struct addrinfo hints;
			struct addrinfo *res = nullptr;
			ZeroMemory(&hints, sizeof(hints));
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;

			int gai = getaddrinfo(hostOnly.c_str(), "47989", &hints, &res);
			if (gai != 0) {
				resultMsg = std::string("DNS lookup failed: ") + std::to_string(gai);
			} else {
				bool ok = false;
				double bestRttMs = -1.0;
				for (struct addrinfo *p = res; p != nullptr; p = p->ai_next) {
					SOCKET s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
					if (s == INVALID_SOCKET) continue;
					u_long mode = 1;
					ioctlsocket(s, FIONBIO, &mode);

					using namespace std::chrono;
					auto start = high_resolution_clock::now();
					int rc = connect(s, p->ai_addr, (int)p->ai_addrlen);
					if (rc == 0) {
						auto end = high_resolution_clock::now();
						double ms = duration_cast<microseconds>(end - start).count() / 1000.0;
						if (bestRttMs < 0 || ms < bestRttMs) bestRttMs = ms;
						ok = true;
						closesocket(s);
						break;
					}
					fd_set writeSet;
					FD_ZERO(&writeSet);
					FD_SET(s, &writeSet);
                    timeval tv; tv.tv_sec = 3; tv.tv_usec = 0;
					int sel = select(0, NULL, &writeSet, NULL, &tv);
					if (sel > 0 && FD_ISSET(s, &writeSet)) {
						auto end = high_resolution_clock::now();
						double ms = duration_cast<microseconds>(end - start).count() / 1000.0;
						if (bestRttMs < 0 || ms < bestRttMs) bestRttMs = ms;
						ok = true;
						closesocket(s);
						break;
					}
					closesocket(s);
				}
				freeaddrinfo(res);
				if (ok) {
					if (bestRttMs >= 0) {
						char buf[64];
						snprintf(buf, sizeof(buf), "Connection OK (RTT: %.1f ms)", bestRttMs);
						resultMsg = buf;
					} else {
						resultMsg = "Connection OK";
					}
				} else {
					resultMsg = "Connection failed";
				}
			}
			WSACleanup();
		}

		Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([resultMsg, hostOnly, weakThis]() {
			auto dialog = ref new TestConnectionResultDialog();
			dialog->Configure(
				Utils::StringFromStdString(hostOnly),
				Utils::StringFromStdString(resultMsg)
			);
			auto page = weakThis.Resolve<HostSelectorPage>();
			if (page != nullptr) {
				try { dialog->XamlRoot = page->XamlRoot; } catch (...) {}
			}
			concurrency::create_task(dialog->ShowAsync());
		}));
	});
}
