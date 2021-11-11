//
// Copyright (C) 2004 OpenSim Ltd.
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

#ifndef __INET_PPP_H
#define __INET_PPP_H

#include "inet/common/lifecycle/ILifecycle.h"
#include "inet/common/lifecycle/NodeStatus.h"
#include "inet/common/packet/Packet.h"
#include "inet/linklayer/base/MacProtocolBase.h"
#include "inet/linklayer/ppp/PppFrame_m.h"
#include "inet/queueing/contract/IPacketQueue.h"

namespace inet {

class NetworkInterface;

/**
 * PPP implementation.
 */
class INET_API Ppp : public LayeredProtocolBase, public cListener
{
  protected:
    /** @brief Gate ids */
    //@{
    int upperLayerInGateId = -1;
    int upperLayerOutGateId = -1;
    int lowerLayerInGateId = -1;
    int lowerLayerOutGateId = -1;
    //@}

    NetworkInterface *networkInterface = nullptr;

    /** Currently transmitted frame if any */
    Packet *currentTxFrame = nullptr;

    /** Messages received from upper layer and to be transmitted later */
    opp_component_ptr<queueing::IPacketQueue> txQueue;

    cModule *hostModule = nullptr;

    const char *displayStringTextFormat = nullptr;
    bool sendRawBytes = false;
    cGate *physOutGate = nullptr;
    cChannel *datarateChannel = nullptr; // nullptr if we're not connected

    cMessage *endTransmissionEvent = nullptr;

    // saved current transmission
    Packet *curTxPacket = nullptr;

    std::string oldConnColor;

    // statistics
    long numSent = 0;
    long numRcvdOK = 0;
    long numDroppedBitErr = 0;
    long numDroppedIfaceDown = 0;

    static simsignal_t transmissionStateChangedSignal;
    static simsignal_t rxPkOkSignal;

  protected:
    virtual void startTransmitting();
    virtual void encapsulate(Packet *msg);
    virtual void decapsulate(Packet *packet);
    virtual void refreshDisplay() const override;
    virtual void refreshOutGateConnection(bool connected);

    // cListener function
    virtual void receiveSignal(cComponent *source, simsignal_t signalID, cObject *obj, cObject *details) override;

    // MacBase functions
    virtual void configureNetworkInterface();

  public:
    Ppp();
    virtual ~Ppp();

  protected:
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessageWhenUp(cMessage *message) override;
    virtual void handleSelfMessage(cMessage *message) override;
    virtual void handleUpperPacket(Packet *packet) override;
    virtual void handleLowerPacket(Packet *packet) override;
    virtual void handleStartOperation(LifecycleOperation *operation) override;
    virtual void handleStopOperation(LifecycleOperation *operation) override;
    virtual void handleCrashOperation(LifecycleOperation *operation) override;

    virtual void registerInterface();

    virtual MacAddress parseMacAddressParameter(const char *addrstr);

    virtual void sendUp(cMessage *message);
    virtual void sendDown(cMessage *message);

    virtual bool isUpperMessage(cMessage *message) override;
    virtual bool isLowerMessage(cMessage *message) override;

    virtual bool isInitializeStage(int stage) override { return stage == INITSTAGE_LINK_LAYER; }
    virtual bool isModuleStartStage(int stage) override { return stage == ModuleStartOperation::STAGE_LINK_LAYER; }
    virtual bool isModuleStopStage(int stage) override { return stage == ModuleStopOperation::STAGE_LINK_LAYER; }

    virtual void deleteCurrentTxFrame();
    virtual void dropCurrentTxFrame(PacketDropDetails& details);
    virtual void popTxQueue();

    /**
     * should clear queue and emit signal "packetDropped" with entire packets
     */
    virtual void flushQueue(PacketDropDetails& details);

    /**
     * should clear queue silently
     */
    virtual void clearQueue();

    using cListener::receiveSignal;
    virtual void handleMessageWhenDown(cMessage *msg) override;
};

} // namespace inet

#endif

