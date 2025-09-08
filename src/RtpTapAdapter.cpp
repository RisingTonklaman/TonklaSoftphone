#include "RtpTapAdapter.hpp"


RtpTapAdapter::RtpTapAdapter() {}
RtpTapAdapter::~RtpTapAdapter() {}
void RtpTapAdapter::setTarget(const std::string& host, int port){ host_ = host; port_ = port; }
// TODO: Provide a concrete adapter that hooks into pjmedia transport callbacks and
// calls sendto() on a UDP socket to mirror incoming video RTP packets.