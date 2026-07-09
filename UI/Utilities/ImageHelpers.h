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

// Resize a SoftwareBitmap to target dimensions. Returns a new BGRA8 Premultiplied bitmap
// or nullptr on failure.
Windows::Graphics::Imaging::SoftwareBitmap^ ResizeSoftwareBitmap(Windows::Graphics::Imaging::SoftwareBitmap^ src, unsigned int width, unsigned int height);

// Resize while preserving aspect ratio using UniformToFill (center-crop then scale).
Windows::Graphics::Imaging::SoftwareBitmap^ ResizeSoftwareBitmapUniformToFill(Windows::Graphics::Imaging::SoftwareBitmap^ src, unsigned int width, unsigned int height);

// Adjust the saturation of a BGRA8 premultiplied SoftwareBitmap in-place.
// `saturation` is a multiplier where 1.0 = no change, 0.0 = fully desaturated (grayscale).
// Returns true on success.
bool AdjustSaturation(Windows::Graphics::Imaging::SoftwareBitmap^ bmp, float saturation);

// High-level helper: resize+blur pipeline and return an encoded PNG stream.
// Parameters:
// - src: SoftwareBitmap source image (may be resized by the helper)
// - targetW/targetH: desired output pixel dimensions (0 to use source)
// - dpi: display dpi used to convert DIP->px for blur radius
// - blurDip: blur radius in DIP units
// Returns an in-memory PNG stream or null on failure.
concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> CreateMaskedBlurredPngStreamAsync(
	Windows::Graphics::Imaging::SoftwareBitmap^ src,
	unsigned int targetW,
	unsigned int targetH,
	double dpi,
	float blurDip);

}
