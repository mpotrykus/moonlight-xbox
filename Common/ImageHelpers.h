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

// Resize a SoftwareBitmap to target dimensions. Returns a new BGRA8 Premultiplied bitmap
// or nullptr on failure.
Windows::Graphics::Imaging::SoftwareBitmap^ ResizeSoftwareBitmap(Windows::Graphics::Imaging::SoftwareBitmap^ src, unsigned int width, unsigned int height);

// Resize while preserving aspect ratio using UniformToFill (center-crop then scale).
Windows::Graphics::Imaging::SoftwareBitmap^ ResizeSoftwareBitmapUniformToFill(Windows::Graphics::Imaging::SoftwareBitmap^ src, unsigned int width, unsigned int height);

// Create a rounded-rect alpha mask as a BGRA8 premultiplied SoftwareBitmap.
// The returned bitmap has solid white RGB and alpha representing the rounded rect
// (0 outside, 255 inside). Radius is in pixel units.
Windows::Graphics::Imaging::SoftwareBitmap^ CreateRoundedRectMask(unsigned int width, unsigned int height, float radiusPx);

}
