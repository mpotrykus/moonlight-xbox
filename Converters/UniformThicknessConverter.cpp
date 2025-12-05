#include "pch.h"
#include "Converters\UniformThicknessConverter.h"

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::UI::Xaml::Interop;

Object^ UniformThicknessConverter::Convert(Object^ value, TypeName targetType, Object^ parameter, String^ language)
{
    double v = 0.0;
    if (value != nullptr) {
        auto boxed = dynamic_cast<IBox<double>^>(value);
        if (boxed != nullptr) v = boxed->Value;
        else {
            try { v = std::stod(std::wstring(static_cast<String^>(value)->Data())); }
            catch (...) { v = 0.0; }
        }
    }

    // parameter is optional scale factor
    double scale = 0.05; // default 5% of input
    if (parameter != nullptr) {
        try { scale = std::stod(std::wstring(safe_cast<String^>(parameter)->Data())); }
        catch (...) { scale = 0.05; }
    }

    double thickness = v * scale;
    if (thickness < 4.0) thickness = 4.0;

    return ref new Platform::Box<Windows::UI::Xaml::Thickness>(Windows::UI::Xaml::Thickness(thickness));
}

Object^ UniformThicknessConverter::ConvertBack(Object^ value, TypeName targetType, Platform::Object^ parameter, Platform::String^ language)
{
    return nullptr;
}
