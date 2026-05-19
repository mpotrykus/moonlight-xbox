#include "pch.h"
#include "AppPage.xaml.h"
#include "AppPage.Helpers.h"
#include "Controls\SlidingMenu.xaml.h"
#include "Common\ModalDialog.xaml.h"
#include "HostSettingsPage.xaml.h"
#include "MoonlightSettings.xaml.h"
#include "Pages\AppActionsDialog.xaml.h"
#include "State\MoonlightClient.h"
#include "StreamPage.xaml.h"
#include "Utils.hpp"

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media;
using namespace concurrency;

namespace moonlight_xbox_dx {

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
    this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(StreamPage::typeid), config);
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
                if (that != nullptr) try { that->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightSettings::typeid)); } catch (...) {}
            }),
		    ref new RoutedEventHandler([weakThis](Platform::Object ^, RoutedEventArgs ^) {
			    auto that = weakThis.Resolve<AppPage>();
                if (that != nullptr) try { that->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSettingsPage::typeid), that->Host); } catch (...) {}
            }));

        create_task(dialog->ShowAsync());
        if (e != nullptr) e->Handled = true;
    } catch (...) {
        if (e != nullptr) e->Handled = false;
    }
}

// ── AppPage::resumeAppButton_Click ────────────────────────────────────────────

void AppPage::resumeAppButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
    this->Connect(this->currentApp->Id);
}

// ── AppPage::closeAndStartButton_Click ────────────────────────────────────────

void AppPage::closeAndStartButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
    if (this->currentApp == nullptr) return;
    if (sender != nullptr) { this->ExecuteCloseAndStart(); return; }

    auto dialog = ref new ContentDialog();
    dialog->Title           = Utils::StringFromStdString("Confirm");
    dialog->Content         = Utils::StringFromStdString("Close currently running app and connect?");
    dialog->PrimaryButtonText = Utils::StringFromStdString("Yes");
    dialog->CloseButtonText   = Utils::StringFromStdString("Cancel");

    Platform::WeakReference weakThis(this);
    create_task(ModalDialog::ShowOnceAsync(dialog))
        .then([weakThis](ContentDialogResult result) {
        try {
            if (result != ContentDialogResult::Primary) return;
            auto that = weakThis.Resolve<AppPage>();
            if (that) that->ExecuteCloseAndStart();
        } catch(...) {}
    });
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

// ── AppPage::moonlightSettingsButton_Click ────────────────────────────────────

void AppPage::moonlightSettingsButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
    try {
        this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightSettings::typeid));
    } catch(...) {}
}

// ── AppPage::hostSettingsFlyoutButton_Click ───────────────────────────────────

void AppPage::hostSettingsFlyoutButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
    try {
        this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSettingsPage::typeid), this->Host);
    } catch(...) {}
}

} // namespace moonlight_xbox_dx
