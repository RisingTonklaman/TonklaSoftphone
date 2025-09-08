#pragma once
#include <string>
#include <gst/gst.h>


class Recorder {
public:
Recorder();
~Recorder();


// Start recording H.264 RTP on given UDP port into MP4 at outputPath.
// Assumes payload type PT=96, clock-rate=90000, encoding-name=H264.
bool start(const std::string& outputPath, int rtpPort = 5006);
void stop();


bool isRecording() const { return recording_; }


private:
bool ensureInit();
bool buildPipeline(const std::string& outputPath, int rtpPort);


private:
GstElement* pipeline_ = nullptr;
bool gstInited_ = false;
bool recording_ = false;
};