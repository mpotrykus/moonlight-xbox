#include "pch.h"
#include "Converters\BooleanToVisibilityConverter.h"
#include "Converters\BoolToVisibilityConverter.h"

using namespace moonlight_xbox_dx;
using namespace Platform;
using namespace Windows::UI::Xaml;

Object^ BooleanToVisibilityConverter::Convert(
    Object^ value,
    Windows::UI::Xaml::Interop::TypeName targetType,
    Object^ parameter,
    String^ language)
{
    // Reuse BoolToVisibilityConverter implementation
    return BoolToVisibilityConverter::Convert(value, targetType, parameter, language);
}

Object^ BooleanToVisibilityConverter::ConvertBack(
    Object^ value,
    Windows::UI::Xaml::Interop::TypeName targetType,
    Object^ parameter,
    String^ language)
{
    return BoolToVisibilityConverter::ConvertBack(value, targetType, parameter, language);
}
