#pragma once
#include <pjsua2.hpp>
#include <memory>
class Recorder; class RtpTapAdapter;


class SipCall : public pj::Call {
public:
using pj::Call::Call;


void onCallState(pj::OnCallStateParam &prm) override;
void onCallMediaState(pj::OnCallMediaStateParam &prm) override;


void attachRecorder(std::shared_ptr<Recorder> rec) { recorder_ = std::move(rec); }
void setRtpTap(std::shared_ptr<RtpTapAdapter> tap) { tap_ = std::move(tap); }


private:
std::shared_ptr<Recorder> recorder_;
std::shared_ptr<RtpTapAdapter> tap_;
};