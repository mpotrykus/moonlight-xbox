#pragma once

#include "pch.h"

namespace moonlight_xbox_dx {

    public ref class BlurPaddingConverter sealed : Windows::UI::Xaml::Data::IValueConverter
    {
    public:
        // parameter: blur amount in device-independent pixels (double)
        virtual Platform::Object^ Convert(Platform::Object^ value, Windows::UI::Xaml::Interop::TypeName targetType, Platform::Object^ parameter, Platform::String^ language);
        virtual Platform::Object^ ConvertBack(Platform::Object^ value, Windows::UI::Xaml::Interop::TypeName targetType, Platform::Object^ parameter, Platform::String^ language);
    };

}
