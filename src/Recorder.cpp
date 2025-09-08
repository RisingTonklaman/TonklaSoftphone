#include "Recorder.hpp"
#include <iostream>


Recorder::Recorder() {}
Recorder::~Recorder() { stop(); }


bool Recorder::ensureInit() {
if (!gstInited_) {
GError* err = nullptr;
if (!gst_init_check(nullptr, nullptr, &err)) {
if (err) {
std::cerr << "GStreamer init error: " << err->message << std::endl;
g_error_free(err);
}
return false;
}
gstInited_ = true;
}
return true;
}


bool Recorder::buildPipeline(const std::string& outputPath, int rtpPort) {
// RTP/H264 → depay → parse → mp4mux → filesink
// IMPORTANT: caps may need adjustment to match actual SDP/PT
std::string launch =
"udpsrc port=" + std::to_string(rtpPort) +
" caps=application/x-rtp,media=video,encoding-name=H264,payload=96,clock-rate=90000 ! "
"rtpjitterbuffer latency=100 ! rtph264depay ! h264parse config-interval=-1 ! mp4mux faststart=true ! "
"filesink location='" + outputPath + "'";


GError* err = nullptr;
pipeline_ = gst_parse_launch(launch.c_str(), &err);
if (!pipeline_) {
if (err) { std::cerr << "gst_parse_launch error: " << err->message << std::endl; g_error_free(err);}
return false;
}
return true;
}


bool Recorder::start(const std::string& outputPath, int rtpPort) {
if (recording_) return true;
if (!ensureInit()) return false;
if (!buildPipeline(outputPath, rtpPort)) return false;


GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
if (ret == GST_STATE_CHANGE_FAILURE) {
std::cerr << "Failed to start recording pipeline" << std::endl;
gst_object_unref(GST_OBJECT(pipeline_));
pipeline_ = nullptr;
return false;
}
recording_ = true;
std::cout << "Recording started → " << outputPath << std::endl;
return true;
}


void Recorder::stop() {
if (!pipeline_) { recording_ = false; return; }
gst_element_set_state(pipeline_, GST_STATE_NULL);
gst_object_unref(GST_OBJECT(pipeline_));
pipeline_ = nullptr;
recording_ = false;
std::cout << "Recording stopped." << std::endl;
}