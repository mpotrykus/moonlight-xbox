#include "pch.h"
#include "Converters\MultiplyConverter.h"

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::UI::Xaml::Interop;

Object^ MultiplyConverter::Convert(Object^ value, TypeName targetType, Object^ parameter, String^ language)
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

    double p = 1.0;
    if (parameter != nullptr) {
        try { p = std::stod(std::wstring(static_cast<String^>(parameter)->Data())); }
        catch (...) { p = 1.0; }
    }

    return ref new Platform::Box<double>(v * p);
}

Object^ MultiplyConverter::ConvertBack(Object^, TypeName, Object^, String^)
{
    return nullptr;
}
