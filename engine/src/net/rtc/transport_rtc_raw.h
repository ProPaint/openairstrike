// The rtc backend's raw-SDP path (engine/src/net/rtc/transport_rtc.cpp), for apps/net_tool only:
// the same transport, but the descriptions travel as whole SDP text instead of SignalInfo, so
// that the transport can be brought up and tested without the code conversions
// (as3d/net_signal.h). The game never uses it.
//
// Every function takes a transport made by makeRtcTransport (the rtc backend; there is no RTTI
// to check it). rtcHostOfferRaw / rtcJoinOfferSdp replace hostOffer / joinOffer; the state
// machine, the timeouts, the channels and the messages are the same. In raw mode localSignal()
// is false and rtcLocalSdp() has the description once the state is SignalReady.
#pragma once

#include <string>

#include "as3d/net_transport.h"

namespace as3d::net {

void rtcHostOfferRaw(Transport& t);
void rtcJoinOfferSdp(Transport& t, const std::string& offerSdp);
void rtcAcceptAnswerSdp(Transport& t, const std::string& answerSdp);
bool rtcLocalSdp(const Transport& t, std::string& out);

} // namespace as3d::net
