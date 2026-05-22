//
// HostSettingsPage.xaml.cpp
// Implementation of the HostSettingsPage class
//

#include "pch.h"
#include "HostSettingsPage.xaml.h"
#include "UI\Backgrounds\DynamicBackgroundHost.xaml.h"
#include "UI\Backgrounds\BackgroundRegistry.h"
#include "UI\Controls\TabsLayout.xaml.h"
#include "UI\Controls\SwatchPicker.xaml.h"
#include "UI\Pages\MoonlightSettings.xaml.h"
#include "Utils.hpp"
#include <gamingdeviceinformation.h>
#include <cmath> // sqrtf, lround
using namespace Windows::UI::Core;

using namespace moonlight_xbox_dx;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Graphics::Display::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;
using namespace Windows::UI::ViewManagement;
using namespace Windows::UI::ViewManagement::Core;


HostSettingsPage::HostSettingsPage()
{
	InitializeComponent();
	Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->SetDesiredBoundsMode(Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow);
	this->Loaded += ref new Windows::UI::Xaml::RoutedEventHandler(this, &HostSettingsPage::OnLoaded);
	this->Unloaded += ref new Windows::UI::Xaml::RoutedEventHandler(this, &HostSettingsPage::OnUnloaded);
}


void HostSettingsPage::OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) {
	MoonlightHost^ mhost = dynamic_cast<MoonlightHost^>(e->Parameter);
	if (mhost == nullptr) return;
	host = mhost;

	// Save global background and apply host-specific one for preview
	m_savedGlobalBg = nullptr;
	auto localSettings = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
	if (localSettings->HasKey("background")) {
		m_savedGlobalBg = safe_cast<Platform::String^>(localSettings->Lookup("background"));
	}
	auto hostBg = host->Personalization->Background;
	if (hostBg != nullptr && !hostBg->IsEmpty()) {
		localSettings->Insert("background", hostBg);
	}

	try {
		if (BackgroundHost != nullptr) {
			BackgroundHost->Refresh();
			BackgroundHost->StartAnimations();
		}
	} catch (...) {}

	GAMING_DEVICE_MODEL_INFORMATION info = {};
	GetGamingDeviceModelInformation(&info);
	AvailableResolutions->Append(ref new ScreenResolution(1280, 720));
	AvailableResolutions->Append(ref new ScreenResolution(1920, 1080));
	//No 4K for Old Xbox One
	if (!(info.vendorId == GAMING_DEVICE_VENDOR_ID_MICROSOFT && info.deviceId == GAMING_DEVICE_DEVICE_ID_XBOX_ONE)) {
		AvailableResolutions->Append(ref new ScreenResolution(2560, 1440));
		AvailableResolutions->Append(ref new ScreenResolution(3840, 2160));
	}
	AvailableFPS->Append(30);
	AvailableFPS->Append(60);
	AvailableFPS->Append(120);
	AvailableVideoCodecs->Append("H.264");
	AvailableVideoCodecs->Append("HEVC (H.265)");
	AvailableAudioConfigs->Append("Stereo");
	AvailableAudioConfigs->Append("Surround 5.1");
	AvailableAudioConfigs->Append("Surround 7.1");
	CurrentResolutionIndex = 0;
	for (int i = 0; i < AvailableResolutions->Size; i++) {
		if (host->Resolution->Width == AvailableResolutions->GetAt(i)->Width &&
			host->Resolution->Height == AvailableResolutions->GetAt(i)->Height
			) {
			CurrentResolutionIndex = i;
			break;
		}
	}
	CurrentAppIndex = 0;
	auto item = ref new ComboBoxItem();
	item->Content = L"No App";
	AutoStartSelector->Items->Append(item);
	for (int i = 0; i < Host->Apps->Size; i++) {
		auto item = ref new ComboBoxItem();
		item->Content = Host->Apps->GetAt(i)->Name;
		AutoStartSelector->Items->Append(item);
		if (host->AutostartID == host->Apps->GetAt(i)->Id) {
			CurrentAppIndex = i + 1;
		}
	}
	AutoStartSelector->SelectedIndex = CurrentAppIndex;

	// Populate background selector
	auto hostBgKey = host->Personalization->Background;
	bool foundBg = false;
	for (int i = 0; i < kBackgroundCount; ++i) {
		auto bgItem = ref new ComboBoxItem();
		bgItem->Content = ref new Platform::String(kBackgrounds[i].displayName);
		bgItem->DataContext = ref new Platform::String(kBackgrounds[i].key);
		HostBackgroundSelector->Items->Append(bgItem);
		if (hostBgKey != nullptr && hostBgKey->Equals(ref new Platform::String(kBackgrounds[i].key))) {
			HostBackgroundSelector->SelectedIndex = i;
			foundBg = true;
		}
	}
	if (!foundBg) HostBackgroundSelector->SelectedIndex = 0;

	InitParticleSchemeSelector();
	InitParticleCustomPickers();
	UpdateParticleColorSectionVisibility();
	InitStreaksSchemeSelector();
	InitStreaksCustomPickers();
	UpdateStreakColorSectionVisibility();
	InitSpheresSchemeSelector();
	InitSpheresShapeSelector();
	InitSpheresCustomPickers();
	UpdateSpheresColorSectionVisibility();
	InitOrbsSchemeSelector();
	InitOrbsCustomPickers();
	UpdateOrbsColorSectionVisibility();
	InitBlobsSchemeSelector();
	InitBlobsCustomPickers();
	UpdateBlobsColorSectionVisibility();

	// Wire up accent color picker and restore saved selection
	AccentColorPicker->ColorChanged += ref new SwatchColorChangedHandler(
		this, &HostSettingsPage::AccentColorPicker_ColorChanged);
	AccentColorPicker->SelectColor(
		host->Personalization->AccentColor,
		host->Personalization->UseSystemAccent);

	// Apply the stored (or system) accent color so the page reflects it immediately.
	{
		Windows::UI::Color accentColor = host->Personalization->UseSystemAccent
			? (ref new UISettings())->GetColorValue(UIColorType::Accent)
			: host->Personalization->AccentColor;
		Utils::ApplyAccentColor(accentColor);
		auto cur = this->ActualTheme;
		this->RequestedTheme = (cur != ElementTheme::Dark) ? ElementTheme::Dark : ElementTheme::Light;
		this->RequestedTheme = ElementTheme::Default;
		if (rightTabs != nullptr) rightTabs->AccentColor = accentColor;
	}

	if (info.vendorId == GAMING_DEVICE_VENDOR_ID_MICROSOFT) {
		// Old Xbox One can only use H264, remove from settings everything else
		if (info.deviceId == GAMING_DEVICE_DEVICE_ID_XBOX_ONE) {
			CodecComboBox->IsEnabled = false;
			CodecComboBox->SelectedIndex = 0;
		}

		// Disable HDR if console is not set to 4K
		auto mode = HdmiDisplayInformation::GetForCurrentView()->GetCurrentDisplayMode();
		auto height = mode->ResolutionHeightInRawPixels;
		if (height < 2160) {
			EnableHDRCheckbox->IsEnabled = false;
			EnableHDRCheckbox->IsChecked = false;
			EnableHDRCheckbox->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
			HDR4KNote->Visibility = Windows::UI::Xaml::Visibility::Visible;
		} else {
			EnableHDRCheckbox->IsEnabled = true;
			EnableHDRCheckbox->Visibility = Windows::UI::Xaml::Visibility::Visible;
			HDR4KNote->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
		}
	}
}

void HostSettingsPage::OnNavigatedFrom(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) {
	// Restore global background so other pages see the correct setting
	if (m_savedGlobalBg != nullptr) {
		auto localSettings = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
		localSettings->Insert("background", m_savedGlobalBg);
		m_savedGlobalBg = nullptr;
	}
	try {
		if (BackgroundHost != nullptr) BackgroundHost->StopAnimations();
	} catch (...) {}
}

void HostSettingsPage::backButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	GetApplicationState()->UpdateFile();
	this->Frame->GoBack();
}

void HostSettingsPage::OnBackRequested(Platform::Object^ e, Windows::UI::Core::BackRequestedEventArgs^ args)
{
	// UWP on Xbox One triggers a back request whenever the B
	// button is pressed which can result in the app being
	// suspended if unhandled
	GetApplicationState()->UpdateFile();
	this->Frame->GoBack();
	args->Handled = true;

}

void HostSettingsPage::ResolutionSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
	auto selectedResolution = AvailableResolutions->GetAt(this->ResolutionSelector->SelectedIndex);

	// Default to a new bitrate if a new resolution was chosen
	if (selectedResolution->Width != host->Resolution->Width) {
		host->Bitrate = getDefaultBitrate(selectedResolution->Width, selectedResolution->Height, host->FPS);
	}

	host->Resolution = selectedResolution;
}

void HostSettingsPage::FPSSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
	if (e->AddedItems->Size == 0) return;
	int selectedFPS = (int)e->AddedItems->GetAt(0);

	// Default to a new bitrate if a new FPS was chosen
	if (selectedFPS != host->FPS) {
		host->Bitrate = getDefaultBitrate(host->Resolution->Width, host->Resolution->Height, selectedFPS);
	}

	host->FPS = selectedFPS;
}

void HostSettingsPage::AutoStartSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
	int index = AutoStartSelector->SelectedIndex - 1;
	if (index >= 0 && host->Apps->Size > index) {
		host->AutostartID = host->Apps->GetAt(index)->Id;
	}
	else {
		host->AutostartID = -1;
	}
}


void HostSettingsPage::BackgroundSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
	if (host == nullptr) return;
	auto item = dynamic_cast<ComboBoxItem^>(HostBackgroundSelector->SelectedItem);
	if (item == nullptr) return;
	auto key = item->DataContext->ToString();
	host->Personalization->Background = key;

	// Update LocalSettings so BackgroundHost preview reflects the selection
	auto localSettings = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
	if (key != nullptr && !key->IsEmpty()) {
		localSettings->Insert("background", key);
	}
	try {
		if (BackgroundHost != nullptr) {
			BackgroundHost->Refresh();
			BackgroundHost->StartAnimations();
		}
	} catch (...) {}
	UpdateParticleColorSectionVisibility();
	UpdateStreakColorSectionVisibility();
	UpdateSpheresColorSectionVisibility();
	UpdateOrbsColorSectionVisibility();
	UpdateBlobsColorSectionVisibility();
}

void HostSettingsPage::AccentColorPicker_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent)
{
	if (host == nullptr) return;
	host->Personalization->AccentColor = color;
	host->Personalization->UseSystemAccent = useSystemAccent;

	Windows::UI::Color effective = useSystemAccent
		? (ref new UISettings())->GetColorValue(UIColorType::Accent)
		: color;
	Utils::ApplyAccentColor(effective);

	// Force {ThemeResource} bindings to re-evaluate with the new color.
	auto cur = this->ActualTheme;
	this->RequestedTheme = (cur != ElementTheme::Dark) ? ElementTheme::Dark : ElementTheme::Light;
	this->RequestedTheme = ElementTheme::Default;

	if (rightTabs != nullptr) rightTabs->AccentColor = effective;
}

void HostSettingsPage::GlobalSettingsOption_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	this->Frame->Navigate(Windows::UI::Xaml::Interop::TypeName(MoonlightSettings::typeid));
}


void HostSettingsPage::BitrateInput_KeyDown(Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e)
{
	if (e->Key == Windows::System::VirtualKey::Enter) {
		CoreInputView::GetForCurrentView()->TryHide();
	}
}

void HostSettingsPage::OnLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	m_back_cookie = navigation->BackRequested += ref new EventHandler<BackRequestedEventArgs^>(this, &HostSettingsPage::OnBackRequested);

	// Ensure Display tab is visible by default
	if (this->DisplayPanel != nullptr)
		this->DisplayPanel->Visibility = Windows::UI::Xaml::Visibility::Visible;
	if (this->AudioPanel != nullptr)
		this->AudioPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	if (this->StreamPanel != nullptr)
		this->StreamPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
	if (this->DetailsPanel != nullptr)
		this->DetailsPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;

	// If TabsLayout buttons exist, set their direct TargetPanel references so TabsLayout can map them without name lookup
	if (this->rightTabs != nullptr) {
		// Try to find the buttons inside the TabsLayout TabsContent presenter
		auto tabsContent = this->rightTabs->TabsContent;
		if (tabsContent != nullptr) {
			auto panel = dynamic_cast<Windows::UI::Xaml::Controls::Panel^>(safe_cast<Windows::UI::Xaml::UIElement^>(tabsContent));
			if (panel != nullptr) {
				for (unsigned int i = 0; i < panel->Children->Size; i++) {
					auto child = panel->Children->GetAt(i);
					auto btn = dynamic_cast<Windows::UI::Xaml::Controls::Button^>(child);
					if (btn != nullptr) {
						auto content = btn->Content;
						auto text = dynamic_cast<Platform::Object^>(content);
						// Match by name in the button content or use existing TargetPanelName
						auto name = moonlight_xbox_dx::TabsLayout::GetTargetPanelName(btn);
						if (name != nullptr) {
							if (name == "DisplayPanel") {
								moonlight_xbox_dx::TabsLayout::SetTargetPanel(btn, this->DisplayPanel);
							}
							else if (name == "AudioPanel") {
								moonlight_xbox_dx::TabsLayout::SetTargetPanel(btn, this->AudioPanel);
							}
							else if (name == "StreamPanel") {
								moonlight_xbox_dx::TabsLayout::SetTargetPanel(btn, this->StreamPanel);
							}
							else if (name == "DetailsPanel") {
								moonlight_xbox_dx::TabsLayout::SetTargetPanel(btn, this->DetailsPanel);
							}
							else if (name == "PersonalizationPanel") {
								moonlight_xbox_dx::TabsLayout::SetTargetPanel(btn, this->PersonalizationPanel);
							}
						}
					}
				}
			}
		}
	}
}

// Tab activation is handled by TabsLayout (it hooks Click and GotFocus automatically)

void HostSettingsPage::OnUnloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	auto navigation = Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
	navigation->BackRequested -= m_back_cookie;
}

int HostSettingsPage::getDefaultBitrate(int width, int height, int fps)
{
    // Don't scale bitrate linearly beyond 60 FPS. It's definitely not a linear
    // bitrate increase for frame rate once we get to values that high.
    float frameRateFactor = (fps <= 60 ? fps : (std::sqrtf(fps / 60.f) * 60.f)) / 30.f;

    // TODO: Collect some empirical data to see if these defaults make sense.
    // We're just using the values that the Shield used, as we have for years.
    static const struct resTable {
        int pixels;
        float factor;
    } resTable[] {
        { 1280 * 720, 5.0f },
        { 1920 * 1080, 10.0f },
        { 2560 * 1440, 20.0f },
        { 3840 * 2160, 40.0f },
        { -1, -1.0f },
    };

    // Calculate the resolution factor by linear interpolation of the resolution table
    float resolutionFactor;
    int pixels = width * height;
    for (int i = 0;; i++) {
        if (pixels == resTable[i].pixels) {
            // We can bail immediately for exact matches
            resolutionFactor = resTable[i].factor;
            break;
        }
        else if (pixels < resTable[i].pixels) {
            if (i == 0) {
                // Never go below the lowest resolution entry
                resolutionFactor = resTable[i].factor;
            }
            else {
                // Interpolate between the entry greater than the chosen resolution (i) and the entry less than the chosen resolution (i-1)
                resolutionFactor = ((float)(pixels - resTable[i-1].pixels) / (resTable[i].pixels - resTable[i-1].pixels)) * (resTable[i].factor - resTable[i-1].factor) + resTable[i-1].factor;
            }
            break;
        }
        else if (resTable[i].pixels == -1) {
            // Never go above the highest resolution entry
            resolutionFactor = resTable[i-1].factor;
            break;
        }
    }

    return std::lround(resolutionFactor * frameRateFactor) * 1000;
}

// ─── Streak color personalization ──────────────────────────────────────────

static Windows::UI::Color kNeonScheme[]   = { {255,255,0,0},{255,0,60,255},{255,255,0,220},{255,140,0,255},{255,0,220,255} };
static Windows::UI::Color kOceanScheme[]  = { {255,0,80,255},{255,0,200,200},{255,0,229,255},{255,0,184,122},{255,26,58,255} };
static Windows::UI::Color kSunsetScheme[] = { {255,255,106,0},{255,255,45,120},{255,255,26,26},{255,255,194,0},{255,160,32,240} };
static Windows::UI::Color kWarmScheme[]   = { {255,255,224,0},{255,255,140,0},{255,255,173,0},{255,255,215,0},{255,255,69,0} };
static Windows::UI::Color kMonoScheme[]   = { {255,224,224,224},{255,255,255,255},{255,191,191,191},{255,160,160,160},{255,207,207,207} };

static Windows::UI::Color kStreakSwatches[] = {
    {255,255,  0,  0}, {255,255,106,  0}, {255,255,224,  0}, {255,128,255,  0},
    {255,  0,192, 96}, {255,  0,220,255}, {255,  0, 60,255}, {255,140,  0,255},
    {255,255,  0,220}, {255,255, 45,120}, {255,255,255,255}, {255,128,128,128},
};
static const int kStreakSwatchCount = 12;

static const wchar_t* kCustomColorKeys[] = {
    L"streaks.custom.0", L"streaks.custom.1", L"streaks.custom.2",
    L"streaks.custom.3", L"streaks.custom.4"
};

static Platform::String^ ColorToHex(Windows::UI::Color c)
{
    wchar_t buf[7];
    swprintf_s(buf, L"%02X%02X%02X", (unsigned)c.R, (unsigned)c.G, (unsigned)c.B);
    return ref new Platform::String(buf);
}

static Windows::UI::Color HexToColor(Platform::String^ s, Windows::UI::Color fallback)
{
    if (s == nullptr || s->Length() != 6) return fallback;
    const wchar_t* p = s->Data();
    wchar_t buf[7]; wcsncpy_s(buf, p, 6); buf[6] = L'\0';
    wchar_t* end = nullptr;
    unsigned long v = wcstoul(buf, &end, 16);
    if (end != buf + 6) return fallback;
    Windows::UI::Color c;
    c.A = 255;
    c.R = (uint8_t)((v >> 16) & 0xFF);
    c.G = (uint8_t)((v >>  8) & 0xFF);
    c.B = (uint8_t)( v        & 0xFF);
    return c;
}

void HostSettingsPage::UpdateStreakColorSectionVisibility()
{
    if (StreakColorsPanel == nullptr) return;
    Platform::String^ key = (host != nullptr) ? host->Personalization->Background : nullptr;
    if (key == nullptr || key->IsEmpty()) {
        auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
        key = ls->HasKey("background") ? safe_cast<Platform::String^>(ls->Lookup("background")) : L"";
    }
    bool show = (key != nullptr && key->Equals(L"streaks"));
    StreakColorsPanel->Visibility = show ? Windows::UI::Xaml::Visibility::Visible
                                         : Windows::UI::Xaml::Visibility::Collapsed;
}

void HostSettingsPage::UpdateStreaksCustomPanelVisibility()
{
    if (StreaksCustomPanel == nullptr || StreaksSchemeSelector == nullptr) return;
    auto item = dynamic_cast<Windows::UI::Xaml::Controls::ComboBoxItem^>(StreaksSchemeSelector->SelectedItem);
    bool isCustom = (item != nullptr && item->DataContext != nullptr &&
                     item->DataContext->ToString()->Equals(L"custom"));
    StreaksCustomPanel->Visibility = isCustom ? Windows::UI::Xaml::Visibility::Visible
                                              : Windows::UI::Xaml::Visibility::Collapsed;
}

void HostSettingsPage::InitStreaksSchemeSelector()
{
    if (StreaksSchemeSelector == nullptr) return;

    struct { const wchar_t* key; const wchar_t* label; } schemes[] = {
        { L"neon",   L"Neon (Default)" },
        { L"ocean",  L"Ocean"          },
        { L"sunset", L"Sunset"         },
        { L"warm",   L"Warm"           },
        { L"mono",   L"Monochrome"     },
        { L"custom", L"Custom"         },
    };

    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    Platform::String^ saved = L"neon";
    if (ls->HasKey("streaks.scheme"))
        saved = safe_cast<Platform::String^>(ls->Lookup("streaks.scheme"));

    int selectedIdx = 0;
    for (int i = 0; i < 6; ++i) {
        auto item = ref new Windows::UI::Xaml::Controls::ComboBoxItem();
        item->Content  = ref new Platform::String(schemes[i].label);
        item->DataContext = ref new Platform::String(schemes[i].key);
        StreaksSchemeSelector->Items->Append(item);
        if (saved->Equals(ref new Platform::String(schemes[i].key))) selectedIdx = i;
    }
    StreaksSchemeSelector->SelectedIndex = selectedIdx;
    UpdateStreaksCustomPanelVisibility();
}

void HostSettingsPage::InitStreaksCustomPickers()
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    Platform::String^ scheme = L"neon";
    if (ls->HasKey("streaks.scheme"))
        scheme = safe_cast<Platform::String^>(ls->Lookup("streaks.scheme"));

    // Determine default palette for the current scheme (for non-custom restore)
    Windows::UI::Color* defaults = kNeonScheme;
    if      (scheme->Equals(L"ocean"))  defaults = kOceanScheme;
    else if (scheme->Equals(L"sunset")) defaults = kSunsetScheme;
    else if (scheme->Equals(L"warm"))   defaults = kWarmScheme;
    else if (scheme->Equals(L"mono"))   defaults = kMonoScheme;

    SwatchPicker^ pickers[] = { StreaksColor0, StreaksColor1, StreaksColor2, StreaksColor3, StreaksColor4 };

    for (int i = 0; i < 5; ++i) {
        if (pickers[i] == nullptr) continue;
        pickers[i]->SetSwatches(kStreakSwatches, kStreakSwatchCount);

        Windows::UI::Color restoreColor = defaults[i];
        if (scheme->Equals(L"custom")) {
            auto key = ref new Platform::String(kCustomColorKeys[i]);
            if (ls->HasKey(key))
                restoreColor = HexToColor(safe_cast<Platform::String^>(ls->Lookup(key)), defaults[i]);
        }
        pickers[i]->SelectColor(restoreColor, false);
    }

    // Wire ColorChanged events
    StreaksColor0->ColorChanged += ref new SwatchColorChangedHandler(this, &HostSettingsPage::StreaksColor0_ColorChanged);
    StreaksColor1->ColorChanged += ref new SwatchColorChangedHandler(this, &HostSettingsPage::StreaksColor1_ColorChanged);
    StreaksColor2->ColorChanged += ref new SwatchColorChangedHandler(this, &HostSettingsPage::StreaksColor2_ColorChanged);
    StreaksColor3->ColorChanged += ref new SwatchColorChangedHandler(this, &HostSettingsPage::StreaksColor3_ColorChanged);
    StreaksColor4->ColorChanged += ref new SwatchColorChangedHandler(this, &HostSettingsPage::StreaksColor4_ColorChanged);

    m_streaksInitialized = true;
}

void HostSettingsPage::StreaksSchemeSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
    if (!m_streaksInitialized) return;
    auto item = dynamic_cast<Windows::UI::Xaml::Controls::ComboBoxItem^>(StreaksSchemeSelector->SelectedItem);
    if (item == nullptr) return;
    auto key = item->DataContext->ToString();

    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Insert("streaks.scheme", key);

    // Update pickers to reflect the new scheme's colors
    Windows::UI::Color* defaults = kNeonScheme;
    if      (key->Equals(L"ocean"))  defaults = kOceanScheme;
    else if (key->Equals(L"sunset")) defaults = kSunsetScheme;
    else if (key->Equals(L"warm"))   defaults = kWarmScheme;
    else if (key->Equals(L"mono"))   defaults = kMonoScheme;

    if (!key->Equals(L"custom")) {
        SwatchPicker^ pickers[] = { StreaksColor0, StreaksColor1, StreaksColor2, StreaksColor3, StreaksColor4 };
        for (int i = 0; i < 5; ++i) {
            if (pickers[i] != nullptr) pickers[i]->SelectColor(defaults[i], false);
        }
    }

    UpdateStreaksCustomPanelVisibility();

    try {
        if (BackgroundHost != nullptr) BackgroundHost->ReloadBackgroundColors();
    } catch (...) {}
}

static void SaveStreakColor(int slot, Windows::UI::Color color)
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    auto key = ref new Platform::String(kCustomColorKeys[slot]);
    ls->Insert(key, ColorToHex(color));
}

void HostSettingsPage::StreaksColor0_ColorChanged(Platform::Object^, Windows::UI::Color color, bool) { SaveStreakColor(0, color); try { if (BackgroundHost) BackgroundHost->ReloadBackgroundColors(); } catch (...) {} }
void HostSettingsPage::StreaksColor1_ColorChanged(Platform::Object^, Windows::UI::Color color, bool) { SaveStreakColor(1, color); try { if (BackgroundHost) BackgroundHost->ReloadBackgroundColors(); } catch (...) {} }
void HostSettingsPage::StreaksColor2_ColorChanged(Platform::Object^, Windows::UI::Color color, bool) { SaveStreakColor(2, color); try { if (BackgroundHost) BackgroundHost->ReloadBackgroundColors(); } catch (...) {} }
void HostSettingsPage::StreaksColor3_ColorChanged(Platform::Object^, Windows::UI::Color color, bool) { SaveStreakColor(3, color); try { if (BackgroundHost) BackgroundHost->ReloadBackgroundColors(); } catch (...) {} }
void HostSettingsPage::StreaksColor4_ColorChanged(Platform::Object^, Windows::UI::Color color, bool) { SaveStreakColor(4, color); try { if (BackgroundHost) BackgroundHost->ReloadBackgroundColors(); } catch (...) {} }

void HostSettingsPage::StreaksResetButton_Click(Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^)
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Remove("streaks.scheme");
    for (int i = 0; i < 5; ++i)
        ls->Remove(ref new Platform::String(kCustomColorKeys[i]));

    // Reset scheme selector to Neon (index 0)
    StreaksSchemeSelector->SelectedIndex = 0;

    // Reset pickers to neon defaults
    SwatchPicker^ pickers[] = { StreaksColor0, StreaksColor1, StreaksColor2, StreaksColor3, StreaksColor4 };
    for (int i = 0; i < 5; ++i) {
        if (pickers[i] != nullptr) pickers[i]->SelectColor(kNeonScheme[i], false);
    }

    StreaksCustomPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;

    try {
        if (BackgroundHost != nullptr) BackgroundHost->ReloadBackgroundColors();
    } catch (...) {}
}

// ─── Particle color personalization ────────────────────────────────────────

// [0] = particle color, [1] = gradient color
static Windows::UI::Color kParticleChampagneScheme[] = { {255,210,165, 75}, {255, 50, 30,120} };
static Windows::UI::Color kParticleEmberScheme[]     = { {255,255,120, 40}, {255, 15, 80, 90} };
static Windows::UI::Color kParticleAuroraScheme[]    = { {255, 80,215,190}, {255, 90, 15,150} };
static Windows::UI::Color kParticleNebulaScheme[]    = { {255,180, 90,230}, {255, 10, 20,100} };
static Windows::UI::Color kParticleBlossomScheme[]   = { {255,235,110,150}, {255, 15, 85, 60} };

static Windows::UI::Color kParticleSwatches[] = {
    {255,210,165, 75}, {255,230,120,140}, {255, 80,200,180},
    {255,160,100,220}, {255,160,200,255}, {255,255,100, 80},
    {255,100,200,100}, {255,255,180, 80}, {255, 80,120,255},
    {255,220,180,255}, {255,255,255,255}, {255,180,180,180},
};
static const int kParticleSwatchCount = 12;

static const wchar_t* kParticleCustomKey  = L"particles.custom.0";
static const wchar_t* kParticleCustomKey1 = L"particles.custom.1";

void HostSettingsPage::UpdateParticleColorSectionVisibility()
{
    if (ParticleColorsPanel == nullptr) return;
    Platform::String^ key = (host != nullptr) ? host->Personalization->Background : nullptr;
    if (key == nullptr || key->IsEmpty()) {
        auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
        key = ls->HasKey("background") ? safe_cast<Platform::String^>(ls->Lookup("background")) : L"";
    }
    bool show = (key != nullptr && key->Equals(L"particles"));
    ParticleColorsPanel->Visibility = show ? Windows::UI::Xaml::Visibility::Visible
                                           : Windows::UI::Xaml::Visibility::Collapsed;
}

void HostSettingsPage::UpdateParticleCustomPanelVisibility()
{
    if (ParticleCustomPanel == nullptr || ParticleSchemeSelector == nullptr) return;
    auto item = dynamic_cast<Windows::UI::Xaml::Controls::ComboBoxItem^>(ParticleSchemeSelector->SelectedItem);
    bool isCustom = (item != nullptr && item->DataContext != nullptr &&
                     item->DataContext->ToString()->Equals(L"custom"));
    ParticleCustomPanel->Visibility = isCustom ? Windows::UI::Xaml::Visibility::Visible
                                               : Windows::UI::Xaml::Visibility::Collapsed;
}

void HostSettingsPage::InitParticleSchemeSelector()
{
    if (ParticleSchemeSelector == nullptr) return;

    struct { const wchar_t* key; const wchar_t* label; } schemes[] = {
        { L"champagne", L"Champagne (Default)" },
        { L"ember",     L"Ember"               },
        { L"aurora",    L"Aurora"              },
        { L"nebula",    L"Nebula"              },
        { L"blossom",   L"Blossom"             },
        { L"custom",    L"Custom"              },
    };

    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    Platform::String^ saved = L"champagne";
    if (ls->HasKey("particles.scheme"))
        saved = safe_cast<Platform::String^>(ls->Lookup("particles.scheme"));

    int selectedIdx = 0;
    for (int i = 0; i < 6; ++i) {
        auto item = ref new Windows::UI::Xaml::Controls::ComboBoxItem();
        item->Content   = ref new Platform::String(schemes[i].label);
        item->DataContext = ref new Platform::String(schemes[i].key);
        ParticleSchemeSelector->Items->Append(item);
        if (saved->Equals(ref new Platform::String(schemes[i].key))) selectedIdx = i;
    }
    ParticleSchemeSelector->SelectedIndex = selectedIdx;
    UpdateParticleCustomPanelVisibility();
}

void HostSettingsPage::InitParticleCustomPickers()
{
    if (ParticleColor0 == nullptr || ParticleColor1 == nullptr) return;

    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    Platform::String^ scheme = L"champagne";
    if (ls->HasKey("particles.scheme"))
        scheme = safe_cast<Platform::String^>(ls->Lookup("particles.scheme"));

    Windows::UI::Color* defaults = kParticleChampagneScheme;
    if      (scheme->Equals(L"ember"))   defaults = kParticleEmberScheme;
    else if (scheme->Equals(L"aurora"))  defaults = kParticleAuroraScheme;
    else if (scheme->Equals(L"nebula"))  defaults = kParticleNebulaScheme;
    else if (scheme->Equals(L"blossom")) defaults = kParticleBlossomScheme;

    ParticleColor0->SetSwatches(kParticleSwatches, kParticleSwatchCount);
    ParticleColor1->SetSwatches(kParticleSwatches, kParticleSwatchCount);

    Windows::UI::Color restore0 = defaults[0];
    Windows::UI::Color restore1 = defaults[1];
    if (scheme->Equals(L"custom")) {
        auto k0 = ref new Platform::String(kParticleCustomKey);
        auto k1 = ref new Platform::String(kParticleCustomKey1);
        if (ls->HasKey(k0)) restore0 = HexToColor(safe_cast<Platform::String^>(ls->Lookup(k0)), defaults[0]);
        if (ls->HasKey(k1)) restore1 = HexToColor(safe_cast<Platform::String^>(ls->Lookup(k1)), defaults[1]);
    }
    ParticleColor0->SelectColor(restore0, false);
    ParticleColor1->SelectColor(restore1, false);

    ParticleColor0->ColorChanged += ref new SwatchColorChangedHandler(this, &HostSettingsPage::ParticleColor0_ColorChanged);
    ParticleColor1->ColorChanged += ref new SwatchColorChangedHandler(this, &HostSettingsPage::ParticleColor1_ColorChanged);

    m_particleInitialized = true;
}

void HostSettingsPage::ParticleSchemeSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
    if (!m_particleInitialized) return;
    auto item = dynamic_cast<Windows::UI::Xaml::Controls::ComboBoxItem^>(ParticleSchemeSelector->SelectedItem);
    if (item == nullptr) return;
    auto key = item->DataContext->ToString();

    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Insert("particles.scheme", key);

    if (!key->Equals(L"custom")) {
        Windows::UI::Color* defaults = kParticleChampagneScheme;
        if      (key->Equals(L"ember"))   defaults = kParticleEmberScheme;
        else if (key->Equals(L"aurora"))  defaults = kParticleAuroraScheme;
        else if (key->Equals(L"nebula"))  defaults = kParticleNebulaScheme;
        else if (key->Equals(L"blossom")) defaults = kParticleBlossomScheme;
        if (ParticleColor0 != nullptr) ParticleColor0->SelectColor(defaults[0], false);
        if (ParticleColor1 != nullptr) ParticleColor1->SelectColor(defaults[1], false);
    }

    UpdateParticleCustomPanelVisibility();

    try {
        if (BackgroundHost != nullptr) BackgroundHost->ReloadBackgroundColors();
    } catch (...) {}
}

void HostSettingsPage::ParticleColor0_ColorChanged(Platform::Object^, Windows::UI::Color color, bool)
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Insert(ref new Platform::String(kParticleCustomKey), ColorToHex(color));
    try { if (BackgroundHost) BackgroundHost->ReloadBackgroundColors(); } catch (...) {}
}

void HostSettingsPage::ParticleColor1_ColorChanged(Platform::Object^, Windows::UI::Color color, bool)
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Insert(ref new Platform::String(kParticleCustomKey1), ColorToHex(color));
    try { if (BackgroundHost) BackgroundHost->ReloadBackgroundColors(); } catch (...) {}
}

void HostSettingsPage::ParticleResetButton_Click(Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^)
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Remove("particles.scheme");
    ls->Remove(ref new Platform::String(kParticleCustomKey));
    ls->Remove(ref new Platform::String(kParticleCustomKey1));

    ParticleSchemeSelector->SelectedIndex = 0;
    if (ParticleColor0 != nullptr) ParticleColor0->SelectColor(kParticleChampagneScheme[0], false);
    if (ParticleColor1 != nullptr) ParticleColor1->SelectColor(kParticleChampagneScheme[1], false);
    ParticleCustomPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;

    try {
        if (BackgroundHost != nullptr) BackgroundHost->ReloadBackgroundColors();
    } catch (...) {}
}

// ─── Spheres (Bouncing Bubbles) color + shape personalization ────────────────

// [0] = shape color, [1] = gradient-top (used as bg starting point for custom pickers)
static Windows::UI::Color kSpheresClassicUI[] = { {255,255,255,255}, {255,  0, 68,170} };
static Windows::UI::Color kSpheresNeonUI[]    = { {255,  0,238,255}, {255,  0, 32,128} };
static Windows::UI::Color kSpheresSunsetUI[]  = { {255,255,112, 64}, {255,139, 21,  0} };
static Windows::UI::Color kSpheresOceanUI[]   = { {255,  0,200,176}, {255,  0, 64, 96} };
static Windows::UI::Color kSpheresNebulaUI[]  = { {255,192, 96,255}, {255, 64,  0,128} };

static Windows::UI::Color kSpheresShapeSwatches[] = {
    {255,255,255,255}, {255,  0,238,255}, {255,255,112, 64}, {255,  0,200,176},
    {255,192, 96,255}, {255,255,224,  0}, {255,  0,255,128}, {255,255, 80,120},
    {255,100,200,255}, {255,220,180,255}, {255,255,200,100}, {255,180,255,180},
};
static const int kSpheresShapeSwatchCount = 12;

static Windows::UI::Color kSpheresBgSwatches[] = {
    {255,  0, 68,170}, {255,  0, 32,128}, {255,139, 21,  0}, {255,  0, 64, 96},
    {255, 64,  0,128}, {255,  0,100, 60}, {255,120, 60,  0}, {255, 80, 20, 80},
};
static const int kSpheresBgSwatchCount = 8;

static const wchar_t* kSpheresCustomColorKeys[] = { L"spheres.custom.0", L"spheres.custom.1" };

void HostSettingsPage::UpdateSpheresColorSectionVisibility()
{
    if (SpheresColorsPanel == nullptr) return;
    Platform::String^ key = (host != nullptr) ? host->Personalization->Background : nullptr;
    if (key == nullptr || key->IsEmpty()) {
        auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
        key = ls->HasKey("background") ? safe_cast<Platform::String^>(ls->Lookup("background")) : L"";
    }
    bool show = (key != nullptr && key->Equals(L"spheres"));
    SpheresColorsPanel->Visibility = show ? Windows::UI::Xaml::Visibility::Visible
                                          : Windows::UI::Xaml::Visibility::Collapsed;
}

void HostSettingsPage::UpdateSpheresCustomPanelVisibility()
{
    if (SpheresCustomPanel == nullptr || SpheresSchemeSelector == nullptr) return;
    auto item = dynamic_cast<Windows::UI::Xaml::Controls::ComboBoxItem^>(SpheresSchemeSelector->SelectedItem);
    bool isCustom = (item != nullptr && item->DataContext != nullptr &&
                     item->DataContext->ToString()->Equals(L"custom"));
    SpheresCustomPanel->Visibility = isCustom ? Windows::UI::Xaml::Visibility::Visible
                                              : Windows::UI::Xaml::Visibility::Collapsed;
}

void HostSettingsPage::InitSpheresSchemeSelector()
{
    if (SpheresSchemeSelector == nullptr) return;

    struct { const wchar_t* key; const wchar_t* label; } schemes[] = {
        { L"classic", L"Classic (Default)" },
        { L"neon",    L"Neon"              },
        { L"sunset",  L"Sunset"            },
        { L"ocean",   L"Ocean"             },
        { L"nebula",  L"Nebula"            },
        { L"custom",  L"Custom"            },
    };

    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    Platform::String^ saved = L"classic";
    if (ls->HasKey("spheres.scheme"))
        saved = safe_cast<Platform::String^>(ls->Lookup("spheres.scheme"));

    int selectedIdx = 0;
    for (int i = 0; i < 6; ++i) {
        auto item = ref new Windows::UI::Xaml::Controls::ComboBoxItem();
        item->Content   = ref new Platform::String(schemes[i].label);
        item->DataContext = ref new Platform::String(schemes[i].key);
        SpheresSchemeSelector->Items->Append(item);
        if (saved->Equals(ref new Platform::String(schemes[i].key))) selectedIdx = i;
    }
    SpheresSchemeSelector->SelectedIndex = selectedIdx;
    UpdateSpheresCustomPanelVisibility();
}

void HostSettingsPage::InitSpheresShapeSelector()
{
    if (SpheresShapeSelector == nullptr) return;

    struct { const wchar_t* key; const wchar_t* label; } shapes[] = {
        { L"circles",   L"Circles (Default)" },
        { L"squares",   L"Squares"           },
        { L"triangles", L"Triangles"         },
        { L"all",       L"All Shapes"        },
    };

    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    Platform::String^ saved = L"circles";
    if (ls->HasKey("spheres.shape"))
        saved = safe_cast<Platform::String^>(ls->Lookup("spheres.shape"));

    int selectedIdx = 0;
    for (int i = 0; i < 4; ++i) {
        auto item = ref new Windows::UI::Xaml::Controls::ComboBoxItem();
        item->Content    = ref new Platform::String(shapes[i].label);
        item->DataContext = ref new Platform::String(shapes[i].key);
        SpheresShapeSelector->Items->Append(item);
        if (saved->Equals(ref new Platform::String(shapes[i].key))) selectedIdx = i;
    }
    SpheresShapeSelector->SelectedIndex = selectedIdx;
}

void HostSettingsPage::InitSpheresCustomPickers()
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    Platform::String^ scheme = L"classic";
    if (ls->HasKey("spheres.scheme"))
        scheme = safe_cast<Platform::String^>(ls->Lookup("spheres.scheme"));

    Windows::UI::Color* defaults = kSpheresClassicUI;
    if      (scheme->Equals(L"neon"))   defaults = kSpheresNeonUI;
    else if (scheme->Equals(L"sunset")) defaults = kSpheresSunsetUI;
    else if (scheme->Equals(L"ocean"))  defaults = kSpheresOceanUI;
    else if (scheme->Equals(L"nebula")) defaults = kSpheresNebulaUI;

    if (SpheresColor0 != nullptr) {
        SpheresColor0->SetSwatches(kSpheresShapeSwatches, kSpheresShapeSwatchCount);
        Windows::UI::Color c0 = defaults[0];
        if (scheme->Equals(L"custom")) {
            auto k = ref new Platform::String(kSpheresCustomColorKeys[0]);
            if (ls->HasKey(k)) c0 = HexToColor(safe_cast<Platform::String^>(ls->Lookup(k)), c0);
        }
        SpheresColor0->SelectColor(c0, false);
        SpheresColor0->ColorChanged += ref new SwatchColorChangedHandler(
            this, &HostSettingsPage::SpheresColor0_ColorChanged);
    }

    if (SpheresColor1 != nullptr) {
        SpheresColor1->SetSwatches(kSpheresBgSwatches, kSpheresBgSwatchCount);
        Windows::UI::Color c1 = defaults[1];
        if (scheme->Equals(L"custom")) {
            auto k = ref new Platform::String(kSpheresCustomColorKeys[1]);
            if (ls->HasKey(k)) c1 = HexToColor(safe_cast<Platform::String^>(ls->Lookup(k)), c1);
        }
        SpheresColor1->SelectColor(c1, false);
        SpheresColor1->ColorChanged += ref new SwatchColorChangedHandler(
            this, &HostSettingsPage::SpheresColor1_ColorChanged);
    }

    m_spheresInitialized = true;
}

void HostSettingsPage::SpheresSchemeSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
    if (!m_spheresInitialized) return;
    auto item = dynamic_cast<Windows::UI::Xaml::Controls::ComboBoxItem^>(SpheresSchemeSelector->SelectedItem);
    if (item == nullptr) return;
    auto key = item->DataContext->ToString();

    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Insert("spheres.scheme", key);

    if (!key->Equals(L"custom")) {
        Windows::UI::Color* defaults = kSpheresClassicUI;
        if      (key->Equals(L"neon"))   defaults = kSpheresNeonUI;
        else if (key->Equals(L"sunset")) defaults = kSpheresSunsetUI;
        else if (key->Equals(L"ocean"))  defaults = kSpheresOceanUI;
        else if (key->Equals(L"nebula")) defaults = kSpheresNebulaUI;
        if (SpheresColor0 != nullptr) SpheresColor0->SelectColor(defaults[0], false);
        if (SpheresColor1 != nullptr) SpheresColor1->SelectColor(defaults[1], false);
    }

    UpdateSpheresCustomPanelVisibility();

    try {
        if (BackgroundHost != nullptr) BackgroundHost->ReloadBackgroundColors();
    } catch (...) {}
}

void HostSettingsPage::SpheresShapeSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
    if (!m_spheresInitialized) return;
    auto item = dynamic_cast<Windows::UI::Xaml::Controls::ComboBoxItem^>(SpheresShapeSelector->SelectedItem);
    if (item == nullptr) return;
    auto key = item->DataContext->ToString();

    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Insert("spheres.shape", key);

    try {
        if (BackgroundHost != nullptr) BackgroundHost->ReloadBackgroundColors();
    } catch (...) {}
}

void HostSettingsPage::SpheresColor0_ColorChanged(Platform::Object^, Windows::UI::Color color, bool)
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Insert(ref new Platform::String(kSpheresCustomColorKeys[0]), ColorToHex(color));
    try { if (BackgroundHost) BackgroundHost->ReloadBackgroundColors(); } catch (...) {}
}

void HostSettingsPage::SpheresColor1_ColorChanged(Platform::Object^, Windows::UI::Color color, bool)
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Insert(ref new Platform::String(kSpheresCustomColorKeys[1]), ColorToHex(color));
    try { if (BackgroundHost) BackgroundHost->ReloadBackgroundColors(); } catch (...) {}
}

void HostSettingsPage::SpheresResetButton_Click(Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^)
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Remove("spheres.scheme");
    ls->Remove("spheres.shape");
    ls->Remove(ref new Platform::String(kSpheresCustomColorKeys[0]));
    ls->Remove(ref new Platform::String(kSpheresCustomColorKeys[1]));

    SpheresSchemeSelector->SelectedIndex = 0;
    SpheresShapeSelector->SelectedIndex  = 0;
    if (SpheresColor0 != nullptr) SpheresColor0->SelectColor(kSpheresClassicUI[0], false);
    if (SpheresColor1 != nullptr) SpheresColor1->SelectColor(kSpheresClassicUI[1], false);
    SpheresCustomPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;

    try {
        if (BackgroundHost != nullptr) BackgroundHost->ReloadBackgroundColors();
    } catch (...) {}
}

// ─── Orbs (Dancing Orbs) color personalization ──────────────────────────────

// [0] = glow color, [1] = background color
static Windows::UI::Color kOrbsElectricUI[] = { {255, 30,160,255}, {255,  0,  8, 20} };
static Windows::UI::Color kOrbsAuroraUI[]   = { {255,  0,220,150}, {255,  0, 10, 15} };
static Windows::UI::Color kOrbsSolarUI[]    = { {255,255,140,  0}, {255, 12,  5,  0} };
static Windows::UI::Color kOrbsNebulaUI[]   = { {255,200, 80,255}, {255,  8,  0, 20} };
static Windows::UI::Color kOrbsRoseUI[]     = { {255,255, 60,140}, {255, 15,  0, 12} };

static Windows::UI::Color kOrbsGlowSwatches[] = {
    {255, 30,160,255}, {255,  0,220,150}, {255,255,140,  0}, {255,200, 80,255},
    {255,255, 60,140}, {255,  0,240,240}, {255,255,  0,  0}, {255,  0,255,128},
    {255,255,255,  0}, {255,255,128,  0}, {255,128,  0,255}, {255,255,255,255},
};
static const int kOrbsGlowSwatchCount = 12;

static Windows::UI::Color kOrbsBgSwatches[] = {
    {255,  0,  8, 20}, {255,  0, 10, 15}, {255, 12,  5,  0}, {255,  8,  0, 20},
    {255, 15,  0, 12}, {255,  0,  0,  0}, {255,  5, 10,  0}, {255, 10,  5,  5},
};
static const int kOrbsBgSwatchCount = 8;

static const wchar_t* kOrbsCustomColorKeys[] = { L"orbs.custom.0", L"orbs.custom.1" };

void HostSettingsPage::UpdateOrbsColorSectionVisibility()
{
    if (OrbsColorsPanel == nullptr) return;
    Platform::String^ key = (host != nullptr) ? host->Personalization->Background : nullptr;
    if (key == nullptr || key->IsEmpty()) {
        auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
        key = ls->HasKey("background") ? safe_cast<Platform::String^>(ls->Lookup("background")) : L"";
    }
    bool show = (key != nullptr && key->Equals(L"orbs"));
    OrbsColorsPanel->Visibility = show ? Windows::UI::Xaml::Visibility::Visible
                                       : Windows::UI::Xaml::Visibility::Collapsed;
}

void HostSettingsPage::UpdateOrbsCustomPanelVisibility()
{
    if (OrbsCustomPanel == nullptr || OrbsSchemeSelector == nullptr) return;
    auto item = dynamic_cast<Windows::UI::Xaml::Controls::ComboBoxItem^>(OrbsSchemeSelector->SelectedItem);
    bool isCustom = (item != nullptr && item->DataContext != nullptr &&
                     item->DataContext->ToString()->Equals(L"custom"));
    OrbsCustomPanel->Visibility = isCustom ? Windows::UI::Xaml::Visibility::Visible
                                           : Windows::UI::Xaml::Visibility::Collapsed;
}

void HostSettingsPage::InitOrbsSchemeSelector()
{
    if (OrbsSchemeSelector == nullptr) return;

    struct { const wchar_t* key; const wchar_t* label; } schemes[] = {
        { L"electric", L"Electric (Default)" },
        { L"aurora",   L"Aurora"             },
        { L"solar",    L"Solar"              },
        { L"nebula",   L"Nebula"             },
        { L"rose",     L"Rose"               },
        { L"custom",   L"Custom"             },
    };

    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    Platform::String^ saved = L"electric";
    if (ls->HasKey("orbs.scheme"))
        saved = safe_cast<Platform::String^>(ls->Lookup("orbs.scheme"));

    int selectedIdx = 0;
    for (int i = 0; i < 6; ++i) {
        auto item = ref new Windows::UI::Xaml::Controls::ComboBoxItem();
        item->Content   = ref new Platform::String(schemes[i].label);
        item->DataContext = ref new Platform::String(schemes[i].key);
        OrbsSchemeSelector->Items->Append(item);
        if (saved->Equals(ref new Platform::String(schemes[i].key))) selectedIdx = i;
    }
    OrbsSchemeSelector->SelectedIndex = selectedIdx;
    UpdateOrbsCustomPanelVisibility();
}

void HostSettingsPage::InitOrbsCustomPickers()
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    Platform::String^ scheme = L"electric";
    if (ls->HasKey("orbs.scheme"))
        scheme = safe_cast<Platform::String^>(ls->Lookup("orbs.scheme"));

    Windows::UI::Color* defaults = kOrbsElectricUI;
    if      (scheme->Equals(L"aurora")) defaults = kOrbsAuroraUI;
    else if (scheme->Equals(L"solar"))  defaults = kOrbsSolarUI;
    else if (scheme->Equals(L"nebula")) defaults = kOrbsNebulaUI;
    else if (scheme->Equals(L"rose"))   defaults = kOrbsRoseUI;

    if (OrbsColor0 != nullptr) {
        OrbsColor0->SetSwatches(kOrbsGlowSwatches, kOrbsGlowSwatchCount);
        Windows::UI::Color c0 = defaults[0];
        if (scheme->Equals(L"custom")) {
            auto k = ref new Platform::String(kOrbsCustomColorKeys[0]);
            if (ls->HasKey(k)) c0 = HexToColor(safe_cast<Platform::String^>(ls->Lookup(k)), c0);
        }
        OrbsColor0->SelectColor(c0, false);
        OrbsColor0->ColorChanged += ref new SwatchColorChangedHandler(
            this, &HostSettingsPage::OrbsColor0_ColorChanged);
    }

    if (OrbsColor1 != nullptr) {
        OrbsColor1->SetSwatches(kOrbsBgSwatches, kOrbsBgSwatchCount);
        Windows::UI::Color c1 = defaults[1];
        if (scheme->Equals(L"custom")) {
            auto k = ref new Platform::String(kOrbsCustomColorKeys[1]);
            if (ls->HasKey(k)) c1 = HexToColor(safe_cast<Platform::String^>(ls->Lookup(k)), c1);
        }
        OrbsColor1->SelectColor(c1, false);
        OrbsColor1->ColorChanged += ref new SwatchColorChangedHandler(
            this, &HostSettingsPage::OrbsColor1_ColorChanged);
    }

    m_orbsInitialized = true;
}

void HostSettingsPage::OrbsSchemeSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
    if (!m_orbsInitialized) return;
    auto item = dynamic_cast<Windows::UI::Xaml::Controls::ComboBoxItem^>(OrbsSchemeSelector->SelectedItem);
    if (item == nullptr) return;
    auto key = item->DataContext->ToString();

    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Insert("orbs.scheme", key);

    if (!key->Equals(L"custom")) {
        Windows::UI::Color* defaults = kOrbsElectricUI;
        if      (key->Equals(L"aurora")) defaults = kOrbsAuroraUI;
        else if (key->Equals(L"solar"))  defaults = kOrbsSolarUI;
        else if (key->Equals(L"nebula")) defaults = kOrbsNebulaUI;
        else if (key->Equals(L"rose"))   defaults = kOrbsRoseUI;
        if (OrbsColor0 != nullptr) OrbsColor0->SelectColor(defaults[0], false);
        if (OrbsColor1 != nullptr) OrbsColor1->SelectColor(defaults[1], false);
    }

    UpdateOrbsCustomPanelVisibility();

    try {
        if (BackgroundHost != nullptr) BackgroundHost->ReloadBackgroundColors();
    } catch (...) {}
}

void HostSettingsPage::OrbsColor0_ColorChanged(Platform::Object^, Windows::UI::Color color, bool)
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Insert(ref new Platform::String(kOrbsCustomColorKeys[0]), ColorToHex(color));
    try { if (BackgroundHost) BackgroundHost->ReloadBackgroundColors(); } catch (...) {}
}

void HostSettingsPage::OrbsColor1_ColorChanged(Platform::Object^, Windows::UI::Color color, bool)
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Insert(ref new Platform::String(kOrbsCustomColorKeys[1]), ColorToHex(color));
    try { if (BackgroundHost) BackgroundHost->ReloadBackgroundColors(); } catch (...) {}
}

void HostSettingsPage::OrbsResetButton_Click(Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^)
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Remove("orbs.scheme");
    ls->Remove(ref new Platform::String(kOrbsCustomColorKeys[0]));
    ls->Remove(ref new Platform::String(kOrbsCustomColorKeys[1]));

    OrbsSchemeSelector->SelectedIndex = 0;
    if (OrbsColor0 != nullptr) OrbsColor0->SelectColor(kOrbsElectricUI[0], false);
    if (OrbsColor1 != nullptr) OrbsColor1->SelectColor(kOrbsElectricUI[1], false);
    OrbsCustomPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;

    try {
        if (BackgroundHost != nullptr) BackgroundHost->ReloadBackgroundColors();
    } catch (...) {}
}

// ─── Blobs (Morphing Blobs) color personalization ────────────────────────────

// [0-2] blob colors, [3] background
static Windows::UI::Color kBlobsCrimsonUI[] = { {170,210,10,55},{170,185,0,185},{170,100,0,200},{255,8,0,16} };
static Windows::UI::Color kBlobsOceanUI[]   = { {170,0,180,200},{170,0,100,220},{170,0,210,150},{255,0,6,20} };
static Windows::UI::Color kBlobsAuroraUI[]  = { {170,0,200,120},{170,60,190,255},{170,140,40,230},{255,2,4,10} };
static Windows::UI::Color kBlobsEmberUI[]   = { {170,255,70,0},{170,210,20,20},{170,255,140,20},{255,12,3,0} };
static Windows::UI::Color kBlobsNebulaUI[]  = { {170,170,40,230},{170,255,60,180},{170,40,90,255},{255,5,0,18} };

static Windows::UI::Color kBlobsColorSwatches[] = {
    {255,210, 10, 55}, {255,185,  0,185}, {255,100,  0,200},
    {255,  0,180,200}, {255,  0,210,150}, {255,  0,100,220},
    {255,  0,200,120}, {255,140, 40,230}, {255,255, 70,  0},
    {255,255,140, 20}, {255,255, 60,180}, {255,255,255,255},
};
static const int kBlobsColorSwatchCount = 12;

static Windows::UI::Color kBlobsBgSwatches[] = {
    {255,  8,  0, 16}, {255,  0,  6, 20}, {255,  2,  4, 10},
    {255, 12,  3,  0}, {255,  5,  0, 18}, {255,  0,  0,  0},
    {255,  5,  8,  0}, {255, 10,  0,  5},
};
static const int kBlobsBgSwatchCount = 8;

static const wchar_t* kBlobsCustomColorKeys[] = {
    L"blobs.custom.0", L"blobs.custom.1", L"blobs.custom.2", L"blobs.custom.3"
};

void HostSettingsPage::UpdateBlobsColorSectionVisibility()
{
    if (BlobsColorsPanel == nullptr) return;
    Platform::String^ key = (host != nullptr) ? host->Personalization->Background : nullptr;
    if (key == nullptr || key->IsEmpty()) {
        auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
        key = ls->HasKey("background") ? safe_cast<Platform::String^>(ls->Lookup("background")) : L"";
    }
    bool show = (key != nullptr && key->Equals(L"blobs"));
    BlobsColorsPanel->Visibility = show ? Windows::UI::Xaml::Visibility::Visible
                                        : Windows::UI::Xaml::Visibility::Collapsed;
}

void HostSettingsPage::UpdateBlobsCustomPanelVisibility()
{
    if (BlobsCustomPanel == nullptr || BlobsSchemeSelector == nullptr) return;
    auto item = dynamic_cast<Windows::UI::Xaml::Controls::ComboBoxItem^>(BlobsSchemeSelector->SelectedItem);
    bool isCustom = (item != nullptr && item->DataContext != nullptr &&
                     item->DataContext->ToString()->Equals(L"custom"));
    BlobsCustomPanel->Visibility = isCustom ? Windows::UI::Xaml::Visibility::Visible
                                            : Windows::UI::Xaml::Visibility::Collapsed;
}

void HostSettingsPage::InitBlobsSchemeSelector()
{
    if (BlobsSchemeSelector == nullptr) return;

    struct { const wchar_t* key; const wchar_t* label; } schemes[] = {
        { L"crimson", L"Crimson (Default)" },
        { L"ocean",   L"Ocean"             },
        { L"aurora",  L"Aurora"            },
        { L"ember",   L"Ember"             },
        { L"nebula",  L"Nebula"            },
        { L"custom",  L"Custom"            },
    };

    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    Platform::String^ saved = L"crimson";
    if (ls->HasKey("blobs.scheme"))
        saved = safe_cast<Platform::String^>(ls->Lookup("blobs.scheme"));

    int selectedIdx = 0;
    for (int i = 0; i < 6; ++i) {
        auto item = ref new Windows::UI::Xaml::Controls::ComboBoxItem();
        item->Content   = ref new Platform::String(schemes[i].label);
        item->DataContext = ref new Platform::String(schemes[i].key);
        BlobsSchemeSelector->Items->Append(item);
        if (saved->Equals(ref new Platform::String(schemes[i].key))) selectedIdx = i;
    }
    BlobsSchemeSelector->SelectedIndex = selectedIdx;
    UpdateBlobsCustomPanelVisibility();
}

void HostSettingsPage::InitBlobsCustomPickers()
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    Platform::String^ scheme = L"crimson";
    if (ls->HasKey("blobs.scheme"))
        scheme = safe_cast<Platform::String^>(ls->Lookup("blobs.scheme"));

    Windows::UI::Color* defaults = kBlobsCrimsonUI;
    if      (scheme->Equals(L"ocean"))  defaults = kBlobsOceanUI;
    else if (scheme->Equals(L"aurora")) defaults = kBlobsAuroraUI;
    else if (scheme->Equals(L"ember"))  defaults = kBlobsEmberUI;
    else if (scheme->Equals(L"nebula")) defaults = kBlobsNebulaUI;

    SwatchPicker^ blobPickers[] = { BlobsColor0, BlobsColor1, BlobsColor2 };
    for (int i = 0; i < 3; ++i) {
        if (blobPickers[i] == nullptr) continue;
        blobPickers[i]->SetSwatches(kBlobsColorSwatches, kBlobsColorSwatchCount);
        Windows::UI::Color c = defaults[i];
        if (scheme->Equals(L"custom")) {
            auto k = ref new Platform::String(kBlobsCustomColorKeys[i]);
            if (ls->HasKey(k)) c = HexToColor(safe_cast<Platform::String^>(ls->Lookup(k)), c);
        }
        blobPickers[i]->SelectColor(c, false);
    }

    if (BlobsColor3 != nullptr) {
        BlobsColor3->SetSwatches(kBlobsBgSwatches, kBlobsBgSwatchCount);
        Windows::UI::Color c3 = defaults[3];
        if (scheme->Equals(L"custom")) {
            auto k = ref new Platform::String(kBlobsCustomColorKeys[3]);
            if (ls->HasKey(k)) c3 = HexToColor(safe_cast<Platform::String^>(ls->Lookup(k)), c3);
        }
        BlobsColor3->SelectColor(c3, false);
    }

    BlobsColor0->ColorChanged += ref new SwatchColorChangedHandler(this, &HostSettingsPage::BlobsColor0_ColorChanged);
    BlobsColor1->ColorChanged += ref new SwatchColorChangedHandler(this, &HostSettingsPage::BlobsColor1_ColorChanged);
    BlobsColor2->ColorChanged += ref new SwatchColorChangedHandler(this, &HostSettingsPage::BlobsColor2_ColorChanged);
    BlobsColor3->ColorChanged += ref new SwatchColorChangedHandler(this, &HostSettingsPage::BlobsColor3_ColorChanged);

    m_blobsInitialized = true;
}

void HostSettingsPage::BlobsSchemeSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e)
{
    if (!m_blobsInitialized) return;
    auto item = dynamic_cast<Windows::UI::Xaml::Controls::ComboBoxItem^>(BlobsSchemeSelector->SelectedItem);
    if (item == nullptr) return;
    auto key = item->DataContext->ToString();

    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Insert("blobs.scheme", key);

    if (!key->Equals(L"custom")) {
        Windows::UI::Color* defaults = kBlobsCrimsonUI;
        if      (key->Equals(L"ocean"))  defaults = kBlobsOceanUI;
        else if (key->Equals(L"aurora")) defaults = kBlobsAuroraUI;
        else if (key->Equals(L"ember"))  defaults = kBlobsEmberUI;
        else if (key->Equals(L"nebula")) defaults = kBlobsNebulaUI;
        SwatchPicker^ pickers[] = { BlobsColor0, BlobsColor1, BlobsColor2, BlobsColor3 };
        for (int i = 0; i < 4; ++i) {
            if (pickers[i] != nullptr) pickers[i]->SelectColor(defaults[i], false);
        }
    }

    UpdateBlobsCustomPanelVisibility();

    try {
        if (BackgroundHost != nullptr) BackgroundHost->ReloadBackgroundColors();
    } catch (...) {}
}

static void SaveBlobColor(int slot, Windows::UI::Color color)
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    auto key = ref new Platform::String(kBlobsCustomColorKeys[slot]);
    ls->Insert(key, ColorToHex(color));
}

void HostSettingsPage::BlobsColor0_ColorChanged(Platform::Object^, Windows::UI::Color color, bool) { SaveBlobColor(0, color); try { if (BackgroundHost) BackgroundHost->ReloadBackgroundColors(); } catch (...) {} }
void HostSettingsPage::BlobsColor1_ColorChanged(Platform::Object^, Windows::UI::Color color, bool) { SaveBlobColor(1, color); try { if (BackgroundHost) BackgroundHost->ReloadBackgroundColors(); } catch (...) {} }
void HostSettingsPage::BlobsColor2_ColorChanged(Platform::Object^, Windows::UI::Color color, bool) { SaveBlobColor(2, color); try { if (BackgroundHost) BackgroundHost->ReloadBackgroundColors(); } catch (...) {} }
void HostSettingsPage::BlobsColor3_ColorChanged(Platform::Object^, Windows::UI::Color color, bool) { SaveBlobColor(3, color); try { if (BackgroundHost) BackgroundHost->ReloadBackgroundColors(); } catch (...) {} }

void HostSettingsPage::BlobsResetButton_Click(Platform::Object^, Windows::UI::Xaml::RoutedEventArgs^)
{
    auto ls = Windows::Storage::ApplicationData::Current->LocalSettings->Values;
    ls->Remove("blobs.scheme");
    for (int i = 0; i < 4; ++i)
        ls->Remove(ref new Platform::String(kBlobsCustomColorKeys[i]));

    BlobsSchemeSelector->SelectedIndex = 0;
    SwatchPicker^ pickers[] = { BlobsColor0, BlobsColor1, BlobsColor2, BlobsColor3 };
    for (int i = 0; i < 4; ++i) {
        if (pickers[i] != nullptr) pickers[i]->SelectColor(kBlobsCrimsonUI[i], false);
    }
    BlobsCustomPanel->Visibility = Windows::UI::Xaml::Visibility::Collapsed;

    try {
        if (BackgroundHost != nullptr) BackgroundHost->ReloadBackgroundColors();
    } catch (...) {}
}
