//
// Copyright (C) 2013 OpenSim Ltd.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#ifndef __INET_ACKINGMAC_H
#define __INET_ACKINGMAC_H

#include "inet/linklayer/base/MacProtocolBase.h"
#include "inet/linklayer/common/MacAddress.h"
#include "inet/linklayer/contract/IMacProtocol.h"
#include "inet/physicallayer/wireless/common/contract/packetlevel/IRadio.h"
#include "inet/queueing/contract/IActivePacketSink.h"
#include "inet/queueing/contract/IPacketQueue.h"

namespace inet {

/**
 * Implements a simplified ideal MAC.
 *
 * See the NED file for details.
 */
class INET_API AckingMac : public MacProtocolBase, public IMacProtocol, public queueing::IActivePacketSink
{
  protected:
    // parameters
    int headerLength = 0; // AckingMacFrame header length in bytes
    double bitrate = 0; // [bits per sec]
    bool promiscuous = false; // promiscuous mode
    bool fullDuplex = false;
    bool useAck = true;

    physicallayer::IRadio *radio = nullptr;
    physicallayer::IRadio::TransmissionState transmissionState = physicallayer::IRadio::TRANSMISSION_STATE_UNDEFINED;

    simtime_t ackTimeout;
    cMessage *ackTimeoutMsg = nullptr;

  protected:
    /** implements MacBase functions */
    //@{
    virtual void configureNetworkInterface() override;
    //@}

    virtual void startTransmitting();
    virtual bool dropFrameNotForUs(Packet *frame);
    virtual void encapsulate(Packet *msg);
    virtual void decapsulate(Packet *frame);
    virtual void acked(Packet *packet); // called by other AckingMac module, when receiving a packet with my moduleID

    // cListener:
    virtual void receiveSignal(cComponent *src, simsignal_t id, intval_t value, cObject *details) override;

    /** implements MacProtocolBase functions */
    //@{
    virtual void handleUpperPacket(Packet *packet) override;
    virtual void handleLowerPacket(Packet *packet) override;
    virtual void handleSelfMessage(cMessage *message) override;
    //@}

  public:
    AckingMac();
    virtual ~AckingMac();

  protected:
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;

    virtual void processUpperPacket();

  public:
    // IActivePacketSink:
    virtual queueing::IPassivePacketSource *getProvider(cGate *gate) override;
    virtual void handleCanPullPacketChanged(cGate *gate) override;
    virtual void handlePullPacketProcessed(Packet *packet, cGate *gate, bool successful) override;
};

} // namespace inet

#endif

