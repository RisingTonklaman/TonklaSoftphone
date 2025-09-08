#pragma once
#include <pjsua2.hpp>
#include <string>


// Skeleton of a media transport adapter that duplicates **incoming video RTP**
// to localhost:5006 (or configurable). When enabled, GStreamer can read it.
//
// NOTE: Implementation here is a stub. To fully enable, implement on_rx_rtp()
// and wire this adapter to the call's media transport factory in PJSUA2.


class RtpTapAdapter : public pj::TransportAdapter {
public:
RtpTapAdapter();
~RtpTapAdapter() override;


void setTarget(const std::string& host, int port);
void enable(bool on) { enabled_ = on; }


protected:
// Override TransportAdapter callbacks to inspect/duplicate RTP.
// virtual pj_status_t on_rx_rtp(pjmedia_tp_cb_param *param) override; // (pjsua C-layer)
// In PJSUA2, you may need to expose a C adapter and register via ep.addTransportFactory.


private:
std::string host_ = "127.0.0.1";
int port_ = 5006;
bool enabled_ = true;
// Add a UDP socket here to sendto() duplicated RTP packets.
};