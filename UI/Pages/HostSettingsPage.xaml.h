//
// HostSettingsPage.xaml.h
// Declaration of the HostSettingsPage class
//

#pragma once

#include "UI\Pages\HostSettingsPage.g.h"
#include "State\ScreenResolution.h"
#include "UI\Controls\SwatchPicker.xaml.h"

namespace moonlight_xbox_dx
{
	/// <summary>
	/// An empty page that can be used on its own or navigated to within a Frame.
	/// </summary>
	[Windows::Foundation::Metadata::WebHostHidden]
		public ref class HostSettingsPage sealed
	{
	private:
		MoonlightHost^ host;
		Platform::String^ m_savedGlobalBg;
		Windows::Foundation::Collections::IVector<ScreenResolution^>^ availableResolutions;
		Windows::Foundation::Collections::IVector<int>^ availableFps;
		Windows::Foundation::Collections::IVector<Platform::String^>^ availableAudioConfigs;
		Windows::Foundation::Collections::IVector<Platform::String^>^ availableVideoCodecs;
		int currentResolutionIndex = 0;
		int currentAppIndex = 0;
		Windows::Foundation::EventRegistrationToken m_back_cookie;
	protected:
		virtual void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) override;
		virtual void OnNavigatedFrom(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) override;
	public:
		HostSettingsPage();
		property MoonlightHost^ Host {
			MoonlightHost^ get() {
				return this->host;
			}
		}
		void OnBackRequested(Platform::Object^ e, Windows::UI::Core::BackRequestedEventArgs^ args);

		property Windows::Foundation::Collections::IVector<ScreenResolution^>^ AvailableResolutions {
			Windows::Foundation::Collections::IVector<ScreenResolution^>^ get() {
				if (this->availableResolutions == nullptr)
				{
					this->availableResolutions = ref new Platform::Collections::Vector<ScreenResolution^>();
				}
				return this->availableResolutions;
			}
		}

		property Windows::Foundation::Collections::IVector<int>^ AvailableFPS {
			Windows::Foundation::Collections::IVector<int>^ get() {
				if (this->availableFps == nullptr)
				{
					this->availableFps = ref new Platform::Collections::Vector<int>();
				}
				return this->availableFps;
			}
		}

		property Windows::Foundation::Collections::IVector<Platform::String^>^ AvailableAudioConfigs {
			Windows::Foundation::Collections::IVector<Platform::String^>^ get() {
				if (this->availableAudioConfigs == nullptr)
				{
					this->availableAudioConfigs = ref new Platform::Collections::Vector<Platform::String^>();
				}
				return this->availableAudioConfigs;
			}
		}

		property Windows::Foundation::Collections::IVector<Platform::String^>^ AvailableVideoCodecs {
			Windows::Foundation::Collections::IVector<Platform::String^>^ get() {
				if (this->availableVideoCodecs == nullptr)
				{
					this->availableVideoCodecs = ref new Platform::Collections::Vector<Platform::String^>();
				}
				return this->availableVideoCodecs;
			}
		}


		property int CurrentResolutionIndex
		{
			int get() { return this->currentResolutionIndex; }
			void set(int value) {
				this->currentResolutionIndex = value;
			}
		}

		property int CurrentAppIndex
		{
			int get() { return this->currentAppIndex; }
			void set(int value) {
				this->currentAppIndex = value;
			}
		}

	private:
		void backButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void ResolutionSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void FPSSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void BitrateInput_TextChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::TextChangedEventArgs^ e);
		void AutoStartSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void BackgroundSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void AccentColorPicker_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void GlobalSettingsOption_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void DisplayTab_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void AudioTab_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void StreamTab_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void DisplayTab_GotFocus(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void AudioTab_GotFocus(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void StreamTab_GotFocus(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void BitrateInput_KeyDown(Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e);
		void OnLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void OnUnloaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		int getDefaultBitrate(int width, int height, int fps);

		bool m_streaksInitialized = false;
		bool m_particleInitialized = false;
		bool m_spheresInitialized = false;
		bool m_orbsInitialized = false;
		bool m_blobsInitialized = false;

		// Particle color personalization
		void ParticleSchemeSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void ParticleColor0_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void ParticleColor1_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void ParticleResetButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void InitParticleSchemeSelector();
		void InitParticleCustomPickers();
		void UpdateParticleColorSectionVisibility();
		void UpdateParticleCustomPanelVisibility();

		// Streak color personalization
		void StreaksSchemeSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void StreaksColor0_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void StreaksColor1_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void StreaksColor2_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void StreaksColor3_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void StreaksColor4_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void StreaksResetButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void InitStreaksSchemeSelector();
		void InitStreaksCustomPickers();
		void UpdateStreakColorSectionVisibility();
		void UpdateStreaksCustomPanelVisibility();

		// Orbs (Dancing Orbs) color personalization
		void OrbsSchemeSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void OrbsColor0_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void OrbsColor1_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void OrbsResetButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void InitOrbsSchemeSelector();
		void InitOrbsCustomPickers();
		void UpdateOrbsColorSectionVisibility();
		void UpdateOrbsCustomPanelVisibility();

		// Blobs (Morphing Blobs) color personalization
		void BlobsSchemeSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void BlobsColor0_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void BlobsColor1_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void BlobsColor2_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void BlobsColor3_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void BlobsResetButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void InitBlobsSchemeSelector();
		void InitBlobsCustomPickers();
		void UpdateBlobsColorSectionVisibility();
		void UpdateBlobsCustomPanelVisibility();

		// Spheres (Bouncing Bubbles) color + shape personalization
		void SpheresSchemeSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void SpheresShapeSelector_SelectionChanged(Platform::Object^ sender, Windows::UI::Xaml::Controls::SelectionChangedEventArgs^ e);
		void SpheresColor0_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void SpheresColor1_ColorChanged(Platform::Object^ sender, Windows::UI::Color color, bool useSystemAccent);
		void SpheresResetButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void InitSpheresSchemeSelector();
		void InitSpheresShapeSelector();
		void InitSpheresCustomPickers();
		void UpdateSpheresColorSectionVisibility();
		void UpdateSpheresCustomPanelVisibility();

		void ClearAppImageCacheButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
	};
}
