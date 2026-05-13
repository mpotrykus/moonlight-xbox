#include "pch.h"
#include "Pages\HostActionsDialog.xaml.h"

using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;

namespace moonlight_xbox_dx
{

namespace {
    void HideIfOpen(ContentDialog^ dialog) {
        try { if (dialog != nullptr) dialog->Hide(); } catch (...) {}
    }
}

HostActionsDialog::HostActionsDialog()
{
    InitializeComponent();

    try {
        this->HostSettingsButton->Click += ref new RoutedEventHandler([this](Platform::Object^ sender, RoutedEventArgs^ args) {
            HideIfOpen(this);
            if (m_onHostSettings != nullptr) m_onHostSettings->Invoke(sender, args);
        });
        this->HostDetailsButton->Click += ref new RoutedEventHandler([this](Platform::Object^ sender, RoutedEventArgs^ args) {
            HideIfOpen(this);
            if (m_onHostDetails != nullptr) m_onHostDetails->Invoke(sender, args);
        });
        this->WakeHostButton->Click += ref new RoutedEventHandler([this](Platform::Object^ sender, RoutedEventArgs^ args) {
            HideIfOpen(this);
            if (m_onWakeHost != nullptr) m_onWakeHost->Invoke(sender, args);
        });
        this->TestConnectionButton->Click += ref new RoutedEventHandler([this](Platform::Object^ sender, RoutedEventArgs^ args) {
            HideIfOpen(this);
            if (m_onTestConnection != nullptr) m_onTestConnection->Invoke(sender, args);
        });
        this->RemoveHostButton->Click += ref new RoutedEventHandler([this](Platform::Object^ sender, RoutedEventArgs^ args) {
            HideIfOpen(this);
            if (m_onRemoveHost != nullptr) m_onRemoveHost->Invoke(sender, args);
        });
        this->MoonlightSettingsButton->Click += ref new RoutedEventHandler([this](Platform::Object^ sender, RoutedEventArgs^ args) {
            HideIfOpen(this);
            if (m_onMoonlightSettings != nullptr) m_onMoonlightSettings->Invoke(sender, args);
        });
    } catch (...) {}
}

void HostActionsDialog::Configure(
    Platform::String^ hostName,
    bool showWake,
    bool showTestConnection,
    RoutedEventHandler^ onHostSettings,
    RoutedEventHandler^ onHostDetails,
    RoutedEventHandler^ onWakeHost,
    RoutedEventHandler^ onTestConnection,
    RoutedEventHandler^ onRemoveHost,
    RoutedEventHandler^ onMoonlightSettings)
{
    m_onHostSettings = onHostSettings;
    m_onHostDetails = onHostDetails;
    m_onWakeHost = onWakeHost;
    m_onTestConnection = onTestConnection;
    m_onRemoveHost = onRemoveHost;
    m_onMoonlightSettings = onMoonlightSettings;

    try {
        if (this->HostNameHeader != nullptr) this->HostNameHeader->Text = hostName;

        if (this->WakeHostButton != nullptr) {
            bool spans = showWake && !showTestConnection;
			this->WakeHostButton->Visibility = showWake ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
            Grid::SetColumn(this->WakeHostButton, 0);
            Grid::SetColumnSpan(this->WakeHostButton, spans ? 2 : 1);
            this->WakeHostButton->HorizontalAlignment = spans ? Windows::UI::Xaml::HorizontalAlignment::Left : Windows::UI::Xaml::HorizontalAlignment::Stretch;
        }
        if (this->TestConnectionButton != nullptr) {
            bool spans = showTestConnection && !showWake;
            this->TestConnectionButton->Visibility = showTestConnection ? Windows::UI::Xaml::Visibility::Visible : Windows::UI::Xaml::Visibility::Collapsed;
            Grid::SetColumn(this->TestConnectionButton, showWake ? 1 : 0);
            Grid::SetColumnSpan(this->TestConnectionButton, spans ? 2 : 1);
            this->TestConnectionButton->HorizontalAlignment = spans ? Windows::UI::Xaml::HorizontalAlignment::Left : Windows::UI::Xaml::HorizontalAlignment::Stretch;
        }
    } catch (...) {}
}

}
