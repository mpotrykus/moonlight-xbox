#pragma once
#include <windows.ui.xaml.media.imaging.h>
namespace moonlight_xbox_dx {

    [Windows::UI::Xaml::Data::Bindable]
    public ref class MoonlightApp sealed : Windows::UI::Xaml::Data::INotifyPropertyChanged
    {
    private:
        Platform::String^ name;
        Platform::String^ imagePath = "ms-appx:///Assets/gamepad.svg";
        int id;
        bool currentlyRunning;
        bool isGridLayout = false;
        Windows::UI::Xaml::Media::Imaging::BitmapImage^ image;
        // Backing image for the blurred version used as background behind the original image
        Windows::UI::Xaml::Media::Imaging::BitmapImage^ blurredImage;
        // Backing image for the per-item glow (heavily blurred, list mode only)
        Windows::UI::Xaml::Media::Imaging::BitmapImage^ glowImage;
        // Average color sampled from the image (stored as ARGB uint32)
        unsigned int averageColorArgb = 0xFF000000; // default opaque black
    public:
        //Thanks to https://phsucharee.wordpress.com/2013/06/19/data-binding-and-ccx-inotifypropertychanged/
        virtual event Windows::UI::Xaml::Data::PropertyChangedEventHandler^ PropertyChanged;

        void OnPropertyChanged(Platform::String^ propertyName);
        property Platform::String^ Name
        {
            Platform::String^ get() { return this->name; }
            void set(Platform::String^ value) {
                this->name = value;
                OnPropertyChanged("Name");
            }
        }

        property Platform::String^ ImagePath
        {
            Platform::String^ get() { 
                return imagePath;
            }
            void set(Platform::String^ path) {
                this->imagePath = path;
                OnPropertyChanged("ImagePath");
                // Load BitmapImage (only if Image hasn't already been set/blitted)
                if (path != nullptr && this->image == nullptr) {
				try {
                    auto uri = ref new Windows::Foundation::Uri(path);
                    Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
                        Windows::UI::Core::CoreDispatcherPriority::High,
                        ref new Windows::UI::Core::DispatchedHandler([this, uri]() {
                            try {
                                auto bitmap = ref new Windows::UI::Xaml::Media::Imaging::BitmapImage();
                                bitmap->UriSource = uri;
                                bitmap->DecodePixelWidth = 1024;
                                bitmap->CreateOptions = Windows::UI::Xaml::Media::Imaging::BitmapCreateOptions::IgnoreImageCache;
                                this->image = bitmap;
                                OnPropertyChanged("Image");
                            } catch(...) {}
                        })
                    );
            } catch (...) {
            }
                }
            }
        }
        
        property int Id
        {
            int get() { return this->id; }
            void set(int value) {
                this->id = value;
                OnPropertyChanged("Id");
            }
        }

        property bool CurrentlyRunning
        {
            bool get() { return this->currentlyRunning; }
            void set(bool value) {
                this->currentlyRunning = value;
                OnPropertyChanged("CurrentlyRunning");
            }
        }

        property bool IsGridLayout
        {
            bool get() { return this->isGridLayout; }
            void set(bool value) {
                if (this->isGridLayout == value) return;
                this->isGridLayout = value;
                OnPropertyChanged("IsGridLayout");
            }
        }

        property Windows::UI::Xaml::Media::Imaging::BitmapImage^ Image
        {
            Windows::UI::Xaml::Media::Imaging::BitmapImage^ get() { return this->image; }
            void set(Windows::UI::Xaml::Media::Imaging::BitmapImage^ value) {
                if (this->image == value) return;
                this->image = value;
                try { OnPropertyChanged("Image"); } catch(...) {}
            }
        }

        // New property exposing the blurred image (can be null)
        property Windows::UI::Xaml::Media::Imaging::BitmapImage^ BlurredImage
        {
            Windows::UI::Xaml::Media::Imaging::BitmapImage^ get() { return this->blurredImage; }
            void set(Windows::UI::Xaml::Media::Imaging::BitmapImage^ value) {
                if (this->blurredImage == value) return;
                this->blurredImage = value;
                try { OnPropertyChanged("BlurredImage"); } catch(...) {}
            }
        }

        // Heavily-blurred image for the per-item glow (list mode only, null in grid mode)
        property Windows::UI::Xaml::Media::Imaging::BitmapImage^ GlowImage
        {
            Windows::UI::Xaml::Media::Imaging::BitmapImage^ get() { return this->glowImage; }
            void set(Windows::UI::Xaml::Media::Imaging::BitmapImage^ value) {
                if (this->glowImage == value) return;
                this->glowImage = value;
                try { OnPropertyChanged("GlowImage"); } catch(...) {}
            }
        }

        // Expose average color as a simple uint property (ARGB)
        property unsigned int AverageColorArgb
        {
            unsigned int get() { return this->averageColorArgb; }
            void set(unsigned int v) {
                if (this->averageColorArgb == v) return;
                this->averageColorArgb = v;
                try { OnPropertyChanged("AverageColorArgb"); } catch(...) {}
            }
        }

        // Expose a SolidColorBrush instance that can be bound to XAML elements.
        // Update the brush's Color instead of replacing the brush instance so bindings stay valid.
        property Windows::UI::Xaml::Media::SolidColorBrush^ AverageBrush
        {
            Windows::UI::Xaml::Media::SolidColorBrush^ get() {
                if (this->averageBrush == nullptr) {
                    this->averageBrush = ref new Windows::UI::Xaml::Media::SolidColorBrush(Windows::UI::Colors::Black);
                }
                return this->averageBrush;
            }
            void set(Windows::UI::Xaml::Media::SolidColorBrush^ v) {
                if (this->averageBrush == v) return;
                this->averageBrush = v;
                try { OnPropertyChanged("AverageBrush"); } catch(...) {}
            }
        }

    private:
        Windows::UI::Xaml::Media::SolidColorBrush^ averageBrush = nullptr;
    };
}