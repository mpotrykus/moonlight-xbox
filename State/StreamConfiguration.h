#pragma once
#include <string>
namespace moonlight_xbox_dx
{
    ref class MoonlightHost;

    ref class StreamConfiguration {
	public:
		property Platform::String^ hostname;
		// Live reference to the connected host, so settings changed during a stream
		// (e.g. picture adjustments) can be written back and persisted.
		property MoonlightHost^ host;
		property int appID;
	    property Platform::String^ appName;
		property int width;
		property int height;
		property int bitrate;
		property int FPS;
		property bool supportsHevc;
		property Platform::String^ audioConfig;
		property Platform::String^ videoCodec;
		property Platform::String^ framePacing;
		property bool enableHDR;
		property bool playAudioOnPC;
		property bool enableVsync;
		property bool enableSOPS;
		property bool enableAutoBitrate;
		property bool enableStats;
		property bool enableGraphs;
		property int contrast;
		property int blackLevel;
		property int whiteLevel;
		property int gamma;
		property int saturation;
		property Windows::UI::Xaml::Media::Imaging::BitmapImage^ backgroundImage;
	};

	moonlight_xbox_dx::StreamConfiguration^ GetStreamConfig();
	void SetStreamConfig(StreamConfiguration^ config);

}
