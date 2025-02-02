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

#ifndef __INET_MACPROTOCOLBASE_H
#define __INET_MACPROTOCOLBASE_H

#include "inet/common/LayeredProtocolBase.h"
#include "inet/common/lifecycle/ModuleOperations.h"
#include "inet/common/packet/Packet.h"
#include "inet/networklayer/common/NetworkInterface.h"
#include "inet/queueing/contract/IPacketQueue.h"

namespace inet {

class INET_API MacProtocolBase : public LayeredProtocolBase, public cListener
{
  protected:
    /** @brief Gate ids */
    //@{
    int upperLayerInGateId = -1;
    int upperLayerOutGateId = -1;
    int lowerLayerInGateId = -1;
    int lowerLayerOutGateId = -1;
    //@}

    opp_component_ptr<NetworkInterface> networkInterface;

    opp_component_ptr<cModule> hostModule;

    /** Currently transmitted frame if any */
    Packet *currentTxFrame = nullptr;

    /** Messages received from upper layer and to be transmitted later */
    opp_component_ptr<queueing::IPacketQueue> txQueue;

  protected:
    MacProtocolBase();
    virtual ~MacProtocolBase();

    virtual void initialize(int stage) override;

    virtual void registerInterface();
    virtual void configureNetworkInterface() = 0;

    virtual MacAddress parseMacAddressParameter(const char *addrstr);

    virtual void deleteCurrentTxFrame();
    virtual void dropCurrentTxFrame(PacketDropDetails& details);

    virtual void handleMessageWhenDown(cMessage *msg) override;

    virtual void sendUp(cMessage *message);
    virtual void sendDown(cMessage *message);

    virtual bool isUpperMessage(cMessage *message) override;
    virtual bool isLowerMessage(cMessage *message) override;

    virtual bool isInitializeStage(int stage) override { return stage == INITSTAGE_LINK_LAYER; }
    virtual bool isModuleStartStage(int stage) override { return stage == ModuleStartOperation::STAGE_LINK_LAYER; }
    virtual bool isModuleStopStage(int stage) override { return stage == ModuleStopOperation::STAGE_LINK_LAYER; }

    /**
     * should clear queue and emit signal "packetDropped" with entire packets
     */
    virtual void flushQueue(PacketDropDetails& details);

    /**
     * should clear queue silently
     */
    virtual void clearQueue();

    using cListener::receiveSignal;
    virtual void receiveSignal(cComponent *source, simsignal_t signalID, cObject *obj, cObject *details) override;
    virtual void handleStartOperation(LifecycleOperation *operation) override;
    virtual void handleStopOperation(LifecycleOperation *operation) override;
    virtual void handleCrashOperation(LifecycleOperation *operation) override;

    queueing::IPacketQueue *getQueue(cGate *gate) const;

    virtual bool canDequeuePacket() const;
    virtual Packet *dequeuePacket();
};

} // namespace inet

#endif

