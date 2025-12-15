#include "pch.h"
#include "Converters\BlurPaddingConverter.h"

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::UI::Xaml::Interop;

Object^ BlurPaddingConverter::Convert(Object^ value, TypeName targetType, Object^ parameter, String^ language)
{
    double baseValue = 0.0;
    if (value != nullptr) {
        try {
            // Common case: boxed double
            auto boxedDouble = dynamic_cast<IBox<double>^>(value);
            if (boxedDouble != nullptr) { baseValue = boxedDouble->Value; }
            else {
                // Try boxed int/float variants via IPropertyValue
                auto pv = dynamic_cast<Windows::Foundation::IPropertyValue^>(value);
                if (pv != nullptr) {
                    try {
                        if (pv->Type == Windows::Foundation::PropertyType::Double) baseValue = pv->GetDouble();
                        else if (pv->Type == Windows::Foundation::PropertyType::Single) baseValue = pv->GetSingle();
                        else if (pv->Type == Windows::Foundation::PropertyType::Int32) baseValue = pv->GetInt32();
                        else if (pv->Type == Windows::Foundation::PropertyType::UInt32) baseValue = pv->GetUInt32();
                        else if (pv->Type == Windows::Foundation::PropertyType::Int64) baseValue = (double)pv->GetInt64();
                        else if (pv->Type == Windows::Foundation::PropertyType::String) baseValue = std::stod(std::wstring(pv->GetString()->Data()));
                    } catch(...) { baseValue = 0.0; }
                } else {
                    // Fallback: if it's a string, try to parse
                    auto s = dynamic_cast<String^>(value);
                    if (s != nullptr) {
                        try { baseValue = std::stod(std::wstring(s->Data())); } catch(...) { baseValue = 0.0; }
                    } else {
                        baseValue = 0.0;
                    }
                }
            }
        } catch(...) { baseValue = 0.0; }
    }

    double blurAmount = 0.0;
    if (parameter != nullptr) {
        try {
            auto pboxed = dynamic_cast<IBox<double>^>(parameter);
            if (pboxed != nullptr) blurAmount = pboxed->Value;
            else {
                auto pv2 = dynamic_cast<Windows::Foundation::IPropertyValue^>(parameter);
                if (pv2 != nullptr) {
                    try {
                        if (pv2->Type == Windows::Foundation::PropertyType::Double) blurAmount = pv2->GetDouble();
                        else if (pv2->Type == Windows::Foundation::PropertyType::Single) blurAmount = pv2->GetSingle();
                        else if (pv2->Type == Windows::Foundation::PropertyType::Int32) blurAmount = pv2->GetInt32();
                        else if (pv2->Type == Windows::Foundation::PropertyType::UInt32) blurAmount = pv2->GetUInt32();
                        else if (pv2->Type == Windows::Foundation::PropertyType::Int64) blurAmount = (double)pv2->GetInt64();
                        else if (pv2->Type == Windows::Foundation::PropertyType::String) blurAmount = std::stod(std::wstring(pv2->GetString()->Data()));
                    } catch(...) { blurAmount = 0.0; }
                } else {
                    auto ps = dynamic_cast<String^>(parameter);
                    if (ps != nullptr) { try { blurAmount = std::stod(std::wstring(ps->Data())); } catch(...) { blurAmount = 0.0; } }
                }
            }
        } catch(...) { blurAmount = 0.0; }
    }

    // Add padding on both sides
    double result = baseValue + (blurAmount * 1.0);
    return ref new Platform::Box<double>(result);
}

Object^ BlurPaddingConverter::ConvertBack(Object^, TypeName, Object^, String^)
{
    return nullptr;
}
