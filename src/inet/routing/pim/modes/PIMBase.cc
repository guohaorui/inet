//
// Copyright (C) 2013 Brno University of Technology (http://nes.fit.vutbr.cz/ansa)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 3
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//
// Authors: Veronika Rybova, Vladimir Vesely (ivesely@fit.vutbr.cz),
//          Tamas Borbely (tomi@omnetpp.org)

#include "inet/routing/pim/modes/PIMBase.h"

#include "inet/common/IProtocolRegistrationListener.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/networklayer/common/HopLimitTag_m.h"
#include "inet/networklayer/common/InterfaceTable.h"
#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/networklayer/contract/ipv4/IPv4Address.h"
#include "inet/networklayer/ipv4/Ipv4Header.h"
#include "inet/networklayer/ipv4/IPv4InterfaceData.h"

namespace inet {

using namespace std;

const IPv4Address PIMBase::ALL_PIM_ROUTERS_MCAST("224.0.0.13");

const PIMBase::AssertMetric PIMBase::AssertMetric::PIM_INFINITE;

simsignal_t PIMBase::sentHelloPkSignal = registerSignal("sentHelloPk");
simsignal_t PIMBase::rcvdHelloPkSignal = registerSignal("rcvdHelloPk");

bool PIMBase::AssertMetric::operator==(const AssertMetric& other) const
{
    return rptBit == other.rptBit && preference == other.preference &&
           metric == other.metric && address == other.address;
}

bool PIMBase::AssertMetric::operator!=(const AssertMetric& other) const
{
    return rptBit != other.rptBit || preference != other.preference ||
           metric != other.metric || address != other.address;
}

bool PIMBase::AssertMetric::operator<(const AssertMetric& other) const
{
    if (isInfinite())
        return false;
    if (other.isInfinite())
        return true;
    if (rptBit != other.rptBit)
        return rptBit < other.rptBit;
    if (preference != other.preference)
        return preference < other.preference;
    if (metric != other.metric)
        return metric < other.metric;
    return address > other.address;
}

PIMBase::~PIMBase()
{
    cancelAndDelete(helloTimer);
}

void PIMBase::initialize(int stage)
{
    OperationalBase::initialize(stage);

    if (stage == INITSTAGE_LOCAL) {
        ift = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);
        rt = getModuleFromPar<IIPv4RoutingTable>(par("routingTableModule"), this);
        pimIft = getModuleFromPar<PIMInterfaceTable>(par("pimInterfaceTableModule"), this);
        pimNbt = getModuleFromPar<PIMNeighborTable>(par("pimNeighborTableModule"), this);

        cModule *host = findContainingNode(this);
        if (!host)
            throw cRuntimeError("PIMBase: containing node not found.");

        hostname = host->getName();

        helloPeriod = par("helloPeriod");
        holdTime = par("holdTime");
        designatedRouterPriority = mode == PIMInterface::SparseMode ? par("designatedRouterPriority") : -1;
    }
    else if (stage == INITSTAGE_ROUTING_PROTOCOLS)
        registerProtocol(Protocol::pim, gate("ipOut"));
}

bool PIMBase::handleNodeStart(IDoneCallback *doneCallback)
{
    generationID = intrand(UINT32_MAX);
    // to receive PIM messages, join to ALL_PIM_ROUTERS multicast group
    isEnabled = false;
    for (int i = 0; i < pimIft->getNumInterfaces(); i++) {
        PIMInterface *pimInterface = pimIft->getInterface(i);
        if (pimInterface->getMode() == mode) {
            pimInterface->getInterfacePtr()->ipv4Data()->joinMulticastGroup(ALL_PIM_ROUTERS_MCAST);
            isEnabled = true;
        }
    }

    if (isEnabled) {
        EV_INFO << "PIM is enabled on device " << hostname << endl;
        helloTimer = new cMessage("PIM HelloTimer", HelloTimer);
        scheduleAt(simTime() + par("triggeredHelloDelay").doubleValue(), helloTimer);
    }

    return true;
}

bool PIMBase::handleNodeShutdown(IDoneCallback *doneCallback)
{
    // TODO unregister IP_PROT_PIM
    cancelAndDelete(helloTimer);
    helloTimer = nullptr;
    return true;
}

void PIMBase::handleNodeCrash()
{
    // TODO unregister IP_PROT_PIM
    cancelAndDelete(helloTimer);
    helloTimer = nullptr;
}

void PIMBase::processHelloTimer(cMessage *timer)
{
    ASSERT(timer == helloTimer);
    EV_DETAIL << "Hello Timer expired.\n";
    sendHelloPackets();
    scheduleAt(simTime() + helloPeriod, helloTimer);
}

void PIMBase::sendHelloPackets()
{
    for (int i = 0; i < pimIft->getNumInterfaces(); i++) {
        PIMInterface *pimInterface = pimIft->getInterface(i);
        if (pimInterface->getMode() == mode)
            sendHelloPacket(pimInterface);
    }
}

void PIMBase::sendHelloPacket(PIMInterface *pimInterface)
{
    EV_INFO << "Sending Hello packet on interface '" << pimInterface->getInterfacePtr()->getInterfaceName() << "'\n";

    Packet *pk = new Packet("PIMHello");
    const auto& msg = makeShared<PIMHello>();

    int byteLength = PIM_HEADER_LENGTH + 6 + 8;    // HoldTime + GenerationID option

    msg->setOptionsArraySize(designatedRouterPriority < 0 ? 2 : 3);
    HoldtimeOption *holdtimeOption = new HoldtimeOption();
    holdtimeOption->setHoldTime(holdTime < 0 ? (uint16_t)0xffff : (uint16_t)holdTime);
    msg->setOptions(0, holdtimeOption);

    GenerationIDOption *genIdOption = new GenerationIDOption();
    genIdOption->setGenerationID(generationID);
    msg->setOptions(1, genIdOption);

    if (designatedRouterPriority >= 0) {
        DRPriorityOption *drPriorityOption = new DRPriorityOption();
        drPriorityOption->setPriority(designatedRouterPriority);
        msg->setOptions(2, drPriorityOption);
        byteLength += 8;
    }

    msg->setChunkLength(B(byteLength));
    msg->markImmutable();
    pk->pushHeader(msg);
    pk->ensureTag<PacketProtocolTag>()->setProtocol(&Protocol::pim);
    pk->ensureTag<InterfaceReq>()->setInterfaceId(pimInterface->getInterfaceId());
    pk->ensureTag<DispatchProtocolInd>()->setProtocol(&Protocol::pim);
    pk->ensureTag<DispatchProtocolReq>()->setProtocol(&Protocol::ipv4);
    pk->ensureTag<L3AddressReq>()->setDestAddress(ALL_PIM_ROUTERS_MCAST);
    pk->ensureTag<HopLimitReq>()->setHopLimit(1);

    emit(sentHelloPkSignal, pk);

    send(pk, "ipOut");
}

void PIMBase::processHelloPacket(Packet *packet)
{
    int interfaceId = packet->getMandatoryTag<InterfaceInd>()->getInterfaceId();

    IPv4Address address = packet->getMandatoryTag<L3AddressInd>()->getSrcAddress().toIPv4();
    const auto& pimPacket = packet->peekHeader<PIMHello>();
    int version = pimPacket->getVersion();

    emit(rcvdHelloPkSignal, packet);

    // process options
    double holdTime = 3.5 * 30;
    long drPriority = -1L;
    unsigned int generationId = 0;
    for (unsigned int i = 0; i < pimPacket->getOptionsArraySize(); i++) {
        HelloOption *option = pimPacket->getOptions(i);
        if (dynamic_cast<HoldtimeOption *>(option))
            holdTime = (double)static_cast<HoldtimeOption *>(option)->getHoldTime();
        else if (dynamic_cast<DRPriorityOption *>(option))
            drPriority = static_cast<DRPriorityOption *>(option)->getPriority();
        else if (dynamic_cast<GenerationIDOption *>(option))
            generationId = static_cast<GenerationIDOption *>(option)->getGenerationID();
    }

    InterfaceEntry *ie = ift->getInterfaceById(interfaceId);

    EV_INFO << "Received PIM Hello from neighbor: interface=" << ie->getInterfaceName() << " address=" << address << "\n";

    PIMNeighbor *neighbor = pimNbt->findNeighbor(interfaceId, address);
    if (neighbor)
        pimNbt->restartLivenessTimer(neighbor, holdTime);
    else {
        neighbor = new PIMNeighbor(ie, address, version);
        pimNbt->addNeighbor(neighbor, holdTime);

        // TODO If a Hello message is received from a new neighbor, the
        // receiving router SHOULD send its own Hello message after a random
        // delay between 0 and Triggered_Hello_Delay.
    }

    neighbor->setGenerationId(generationId);
    neighbor->setDRPriority(drPriority);

    delete packet;
}

}    // namespace inet

