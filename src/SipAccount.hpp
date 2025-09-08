#pragma once
#include <pjsua2.hpp>
#include <memory>
class SipCall; class Recorder; class RtpTapAdapter;


class SipAccount : public pj::Account {
public:
SipAccount();
~SipAccount() override;


void setRecorder(std::shared_ptr<Recorder> rec) { recorder_ = std::move(rec); }
void setRtpTap(std::shared_ptr<RtpTapAdapter> tap) { tap_ = std::move(tap); }


void onRegState(pj::OnRegStateParam &prm) override;
void onIncomingCall(pj::OnIncomingCallParam &iprm) override;


std::shared_ptr<SipCall> dial(const std::string &sipUri);


private:
std::shared_ptr<SipCall> currentCall_;
std::shared_ptr<Recorder> recorder_;
std::shared_ptr<RtpTapAdapter> tap_;
};