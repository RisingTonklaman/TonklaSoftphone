#include "SipCall.hpp"
#include "Recorder.hpp"
#include <iostream>


void SipCall::onCallState(pj::OnCallStateParam &prm) {
PJ_UNUSED_ARG(prm);
pj::CallInfo ci = getInfo();
std::cout << "[CALL] State=" << ci.stateText << ", Role=" << (ci.role==PJSIP_ROLE_UAC?"UAC":"UAS") << std::endl;
if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
if (recorder_ && recorder_->isRecording()) recorder_->stop();
}
}


void SipCall::onCallMediaState(pj::OnCallMediaStateParam &prm) {
PJ_UNUSED_ARG(prm);
pj::CallInfo ci = getInfo();
for (unsigned i=0; i<ci.media.size(); ++i) {
if (ci.media[i].type == PJMEDIA_TYPE_VIDEO && ci.media[i].status == PJMEDIA_MEDIA_ACTIVE) {
std::cout << "[MEDIA] Video active (index " << i << ")" << std::endl;
// At this point, the RTP tap (when fully implemented) should see packets.
}
}
}