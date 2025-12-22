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
        Windows::UI::Xaml::Media::Imaging::BitmapImage^ image;
        // Backing image for the blurred version used as background behind the original image
        Windows::UI::Xaml::Media::Imaging::BitmapImage^ blurredImage;
        // Backing image for the reflection
        Windows::UI::Xaml::Media::Imaging::BitmapImage^ reflectionImage;
        // Average color sampled from the image (stored as ARGB uint32)
        unsigned int averageColorArgb = 0xFF000000; // default opaque black
    public:
        //Thanks to https://phsucharee.wordpress.com/2013/06/19/data-binding-and-ccx-inotifypropertychanged/
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
                            } catch(...) {
                                // Ignore but emit a debug string if possible
                                OutputDebugStringA("MoonlightApp: failed to create BitmapImage in dispatcher\n");
                            }
                        })
                    );
            } catch (...) {
                OutputDebugStringA("MoonlightApp: exception while scheduling BitmapImage creation\n");
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

        // New property exposing the reflection image (can be null)
        property Windows::UI::Xaml::Media::Imaging::BitmapImage^ ReflectionImage
        {
            Windows::UI::Xaml::Media::Imaging::BitmapImage^ get() { return this->reflectionImage; }
            void set(Windows::UI::Xaml::Media::Imaging::BitmapImage^ value) {
                if (this->reflectionImage == value) return;
                this->reflectionImage = value;
                try { OnPropertyChanged("ReflectionImage"); } catch(...) {}
                // Diagnostic log: ReflectionImage assigned on model
                try {
                    char buf[256];
                    sprintf_s(buf, "MoonlightApp: ReflectionImage set for app id=%d, bmp_ptr=%p\n", this->id, value);
                    OutputDebugStringA(buf);
                } catch(...) {}
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