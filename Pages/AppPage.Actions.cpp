#include "pch.h"
#include "AppPage.xaml.h"
#include "AppPage.Helpers.h"
#include "Controls\SlidingMenu.xaml.h"
#include "Common\ModalDialog.xaml.h"
#include "HostSettingsPage.xaml.h"
#include "State\MoonlightClient.h"
#include "StreamPage.xaml.h"
#include "Utils.hpp"

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
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
    this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(StreamPage::typeid), config);
}

// ── AppPage::AppsGrid_RightTapped ────────────────────────────────────────────

void AppPage::AppsGrid_RightTapped(Platform::Object^ sender, Windows::UI::Xaml::Input::RightTappedRoutedEventArgs^ e) {
    Utils::Log("AppPage::AppsGrid_RightTapped\n");

    // Close sliding menu if open
    try {
        auto lm = this->GetLeftMenu();
        if (lm != nullptr && lm->IsOpen) { lm->Close(); return; }
    } catch(...) {}

    bool wasMenuOpen = false;
    try {
        auto lm = this->GetLeftMenu();
        if (lm != nullptr) { try { wasMenuOpen = lm->IsOpen; } catch(...) {} }
    } catch(...) {}

    FrameworkElement^ senderElement = dynamic_cast<FrameworkElement^>(e->OriginalSource);
    FrameworkElement^ anchor = senderElement;

    if (senderElement != nullptr && senderElement->GetType()->FullName->Equals(ListViewItem::typeid->FullName)) {
        auto gi = (ListViewItem^)senderElement;
        currentApp = (MoonlightApp^)(gi->Content);
        anchor = gi;
    } else {
        if (senderElement != nullptr) currentApp = (MoonlightApp^)(senderElement->DataContext);
        if (currentApp == nullptr && this->AppsGrid != nullptr && this->AppsGrid->SelectedIndex >= 0) {
            currentApp = (MoonlightApp^)this->AppsGrid->SelectedItem;
            auto container = (ListViewItem^)this->AppsGrid->ContainerFromIndex(this->AppsGrid->SelectedIndex);
            anchor = container != nullptr ? (FrameworkElement^)container : (FrameworkElement^)this->AppsGrid;
        }
    }

    bool anyRunning = false;
    MoonlightApp^ runningApp = nullptr;
    if (this->host != nullptr) {
        for (unsigned int i = 0; i < this->host->Apps->Size; ++i) {
            auto c = this->host->Apps->GetAt(i);
            if (c != nullptr && c->CurrentlyRunning) { anyRunning = true; runningApp = c; break; }
        }
    }

    try {
        auto lm = this->GetLeftMenu();
        if (lm == nullptr) {
            if (anchor != nullptr) this->ActionsFlyout->ShowAt(anchor);
            else this->ActionsFlyout->ShowAt(this->AppsGrid);
            return;
        }

        lm->ClearPageItems();
        Platform::WeakReference weakThis(this);

        if (this->currentApp != nullptr && this->currentApp->CurrentlyRunning) {
            lm->AddPageItem(ref new MenuItem(
                ref new Platform::String(L"Resume App"), ref new Platform::String(L""),
                ref new EventHandler<Platform::Object^>([weakThis](Platform::Object^, Platform::Object^) {
                    auto that = weakThis.Resolve<AppPage>(); if (that) try { that->resumeAppButton_Click(nullptr, nullptr); } catch(...) {}
                })));
            lm->AddPageItem(ref new MenuItem(
                ref new Platform::String(L"Close App"), ref new Platform::String(L""),
                ref new EventHandler<Platform::Object^>([weakThis](Platform::Object^, Platform::Object^) {
                    auto that = weakThis.Resolve<AppPage>(); if (that) try { that->closeAppButton_Click(nullptr, nullptr); } catch(...) {}
                })));
        } else if (anyRunning && runningApp != nullptr && this->currentApp != nullptr && !this->currentApp->CurrentlyRunning) {
            lm->AddPageItem(ref new MenuItem(
                ref new Platform::String(L"Close and Start App"), ref new Platform::String(L""),
                ref new EventHandler<Platform::Object^>([weakThis](Platform::Object^, Platform::Object^) {
                    auto that = weakThis.Resolve<AppPage>(); if (that) try { that->closeAndStartButton_Click(nullptr, nullptr); } catch(...) {}
                })));
        } else {
            lm->AddPageItem(ref new MenuItem(
                ref new Platform::String(L"Start App"), ref new Platform::String(L""),
                ref new EventHandler<Platform::Object^>([weakThis](Platform::Object^, Platform::Object^) {
                    auto that = weakThis.Resolve<AppPage>(); if (that) try { that->resumeAppButton_Click(nullptr, nullptr); } catch(...) {}
                })));
        }

        lm->AddPageItem(ref new MenuItem(
            ref new Platform::String(L"Host Settings"), ref new Platform::String(L""),
            ref new EventHandler<Platform::Object^>([](Platform::Object^, Platform::Object^) {
                try {
                    auto rootFrame = dynamic_cast<Frame^>(Windows::UI::Xaml::Window::Current->Content);
                    if (rootFrame != nullptr) rootFrame->Navigate(Windows::UI::Xaml::Interop::TypeName(HostSettingsPage::typeid));
                } catch(...) {}
            })));

        try {
            lm->Title = (this->currentApp != nullptr && this->currentApp->Name != nullptr)
                ? this->currentApp->Name : ref new Platform::String(L"");
        } catch(...) {}

        if (!wasMenuOpen) lm->Open();
    } catch(...) {
        if (anchor != nullptr) this->ActionsFlyout->ShowAt(anchor);
        else this->ActionsFlyout->ShowAt(this->AppsGrid);
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
    auto progressToken = ModalDialog::ShowProgressDialogToken(
        nullptr, Utils::StringFromStdString("Closing app..."));

    create_task(create_async([weakThis, progressToken]() {
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
            ref new DispatchedHandler([weakThis, progressToken]() {
            auto thatUI = weakThis.Resolve<AppPage>();
            try {
                if (thatUI != nullptr && thatUI->currentApp != nullptr)
                    thatUI->Connect(thatUI->currentApp->Id);
                ModalDialog::HideDialogByToken(progressToken);
            } catch(...) {}
        }));
    })).then([](concurrency::task<void> t) { try { t.get(); } catch(...) {} });
}

// ── AppPage::closeAppButton_Click ─────────────────────────────────────────────

void AppPage::closeAppButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e) {
    Platform::WeakReference weakThis(this);
    auto progressToken = ModalDialog::ShowProgressDialogToken(
        Utils::StringFromStdString("Closing"), Utils::StringFromStdString("Closing app..."));

    create_task(create_async([weakThis, progressToken]() {
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
            ref new DispatchedHandler([weakThis, progressToken]() {
            auto thatUI = weakThis.Resolve<AppPage>();
            try {
                if (thatUI != nullptr) {
                    thatUI->host->UpdateHostInfo(true);
                    thatUI->host->UpdateAppRunningStates();
                }
            } catch(...) {}
            ModalDialog::HideDialogByToken(progressToken);
        }));
    }));
}

} // namespace moonlight_xbox_dx
