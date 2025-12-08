// ImageHelpers.h - small middleware for loading/encoding SoftwareBitmap
#pragma once
#include "pch.h"
#include <ppltasks.h>
#include <windows.storage.streams.h>
#include <windows.graphics.imaging.h>

namespace ImageHelpers {

concurrency::task<Windows::Graphics::Imaging::SoftwareBitmap^> LoadSoftwareBitmapFromUriOrPathAsync(Platform::String^ path);

concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> EncodeSoftwareBitmapToPngStreamAsync(Windows::Graphics::Imaging::SoftwareBitmap^ bitmap);

Windows::Graphics::Imaging::SoftwareBitmap^ EnsureBgra8Premultiplied(Windows::Graphics::Imaging::SoftwareBitmap^ bitmap);

// Composite a mask over a base image with a feather radius (pixels).
// The mask should be same dimensions as base. The returned SoftwareBitmap
// is BGRA8 Premultiplied where alpha comes from the blurred mask and RGB
// comes from the base multiplied by that alpha.
Windows::Graphics::Imaging::SoftwareBitmap^ CompositeWithMask(Windows::Graphics::Imaging::SoftwareBitmap^ base, Windows::Graphics::Imaging::SoftwareBitmap^ mask, int featherRadius);

}
