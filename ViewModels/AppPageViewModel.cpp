#include "pch.h"
#include "AppPageViewModel.h"

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Media::Animation;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace moonlight_xbox_dx;

AppPageViewModel::AppPageViewModel()
    : currentBackgroundImage(nullptr), pageBackgroundBorder(nullptr)
{
}

void AppPageViewModel::OnPropertyChanged(Platform::String^ propertyName)
{
    PropertyChanged(this, ref new PropertyChangedEventArgs(propertyName));
}

void AppPageViewModel::SetPageBackgroundBorder(Border^ border)
{
    this->pageBackgroundBorder = border;
}

void AppPageViewModel::TransitionToBlurredImage(BitmapImage^ newImage)
{
    if (newImage == nullptr || this->pageBackgroundBorder == nullptr) return;

    try {
        // Create fade-out animation
        auto fadeOutAnimation = ref new DoubleAnimation();
        fadeOutAnimation->To = 0.0;
        fadeOutAnimation->Duration = Duration(
            Windows::Foundation::TimeSpan{ 2500000LL }); // 0.25s in 100ns units
        
        Storyboard::SetTarget(fadeOutAnimation, this->pageBackgroundBorder);
        Storyboard::SetTargetProperty(fadeOutAnimation, "Opacity");
        
        auto fadeOutSB = ref new Storyboard();
        fadeOutSB->Children->Append(fadeOutAnimation);
        
        Platform::WeakReference weakThis(this);
        fadeOutSB->Completed += ref new EventHandler<Platform::Object^>(
            [weakThis, newImage](Platform::Object^ sender, Platform::Object^ e) {
            try {
                auto that = weakThis.Resolve<AppPageViewModel>();
                if (that == nullptr) return;
                
                // Update the background image source after fade-out completes
                try {
                    auto brush = dynamic_cast<ImageBrush^>(that->pageBackgroundBorder->Background);
                    if (brush != nullptr) {
                        brush->ImageSource = newImage;
                    }
                } catch(...) {}
                
                // Update the property
                that->currentBackgroundImage = newImage;
                that->OnPropertyChanged("CurrentBackgroundImage");
                
                // Create and start fade-in animation
                auto fadeInAnimation = ref new DoubleAnimation();
                fadeInAnimation->From = 0.0;
                fadeInAnimation->To = 0.05; // kBackgroundOpacity
                fadeInAnimation->Duration = Duration(
                    Windows::Foundation::TimeSpan{ 2500000LL }); // 0.25s in 100ns units
                
                Storyboard::SetTarget(fadeInAnimation, that->pageBackgroundBorder);
                Storyboard::SetTargetProperty(fadeInAnimation, "Opacity");
                
                auto fadeInSB = ref new Storyboard();
                fadeInSB->Children->Append(fadeInAnimation);
                fadeInSB->Begin();
            } catch(...) {}
        });
        
        fadeOutSB->Begin();
    } catch(...) {}
}
