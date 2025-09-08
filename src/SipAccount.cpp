#include "SipAccount.hpp"
#include "SipCall.hpp"
#include "Recorder.hpp"
#include "RtpTapAdapter.hpp"
#include <iostream>


SipAccount::SipAccount() {}
SipAccount::~SipAccount() {}


void SipAccount::onRegState(pj::OnRegStateParam &prm) {
PJ_UNUSED_ARG(prm);
pj::AccountInfo ai = getInfo();
std::cout << "[REG] " << (ai.regIsActive?"Registered":"Unregistered")
<< " (code=" << ai.regStatus << ")" << std::endl;
}


void SipAccount::onIncomingCall(pj::OnIncomingCallParam &iprm) {
currentCall_ = std::make_shared<SipCall>(*this, iprm.callId);
currentCall_->attachRecorder(recorder_);
currentCall_->setRtpTap(tap_);


pj::CallInfo ci = currentCall_->getInfo();
std::cout << "[CALL] Incoming from: " << ci.remoteUri << std::endl;


pj::CallOpParam prm;
prm.statusCode = (pjsip_status_code)200; // auto-answer for starter
currentCall_->answer(prm);
}


std::shared_ptr<SipCall> SipAccount::dial(const std::string &sipUri) {
if (currentCall_) { std::cout << "A call is already active." << std::endl; return currentCall_; }
currentCall_ = std::make_shared<SipCall>(*this);
currentCall_->attachRecorder(recorder_);
currentCall_->setRtpTap(tap_);


pj::CallOpParam prm(true);
prm.opt.audioCount = 1;
prm.opt.videoCount = 1; // request video
currentCall_->makeCall(sipUri, prm);
return currentCall_;
}