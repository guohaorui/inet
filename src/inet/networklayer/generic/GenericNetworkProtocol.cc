//
// Copyright (C) 2012 Opensim Ltd.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//

#include "inet/applications/common/SocketTag_m.h"
#include "inet/networklayer/generic/GenericNetworkProtocol.h"

#include "inet/common/ModuleAccess.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/linklayer/common/MACAddressTag_m.h"
#include "inet/networklayer/common/HopLimitTag_m.h"
#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/networklayer/common/L3Tools.h"
#include "inet/networklayer/common/NextHopAddressTag_m.h"
#include "inet/networklayer/contract/L3SocketCommand_m.h"
#include "inet/networklayer/generic/GenericDatagram.h"
#include "inet/networklayer/generic/GenericNetworkProtocolInterfaceData.h"
#include "inet/networklayer/generic/GenericRoute.h"
#include "inet/networklayer/generic/GenericRoutingTable.h"

namespace inet {

Define_Module(GenericNetworkProtocol);

GenericNetworkProtocol::GenericNetworkProtocol() :
        interfaceTable(nullptr),
        routingTable(nullptr),
        arp(nullptr),
        defaultHopLimit(-1),
        numLocalDeliver(0),
        numDropped(0),
        numUnroutable(0),
        numForwarded(0)
{
}

GenericNetworkProtocol::~GenericNetworkProtocol()
{
    for (auto it : socketIdToSocketDescriptor)
        delete it.second;
    for (auto & elem : queuedDatagramsForHooks) {
        delete elem.datagram;
    }
    queuedDatagramsForHooks.clear();
}

void GenericNetworkProtocol::initialize(int stage)
{
    if (stage == INITSTAGE_LOCAL) {
        QueueBase::initialize();

        interfaceTable = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);
        routingTable = getModuleFromPar<GenericRoutingTable>(par("routingTableModule"), this);
        arp = getModuleFromPar<IARP>(par("arpModule"), this);

        defaultHopLimit = par("hopLimit");
        numLocalDeliver = numDropped = numUnroutable = numForwarded = 0;

        WATCH(numLocalDeliver);
        WATCH(numDropped);
        WATCH(numUnroutable);
        WATCH(numForwarded);
    }
    else if (stage == INITSTAGE_NETWORK_LAYER) {
        registerProtocol(Protocol::gnp, gate("transportOut"));
        registerProtocol(Protocol::gnp, gate("queueOut"));
    }
}

void GenericNetworkProtocol::handleRegisterProtocol(const Protocol& protocol, cGate *gate)
{
    Enter_Method("handleRegisterProtocol");
    mapping.addProtocolMapping(ProtocolGroup::ipprotocol.getProtocolNumber(&protocol), gate->getIndex());
}

void GenericNetworkProtocol::refreshDisplay() const
{
    char buf[80] = "";
    if (numForwarded > 0)
        sprintf(buf + strlen(buf), "fwd:%d ", numForwarded);
    if (numLocalDeliver > 0)
        sprintf(buf + strlen(buf), "up:%d ", numLocalDeliver);
    if (numDropped > 0)
        sprintf(buf + strlen(buf), "DROP:%d ", numDropped);
    if (numUnroutable > 0)
        sprintf(buf + strlen(buf), "UNROUTABLE:%d ", numUnroutable);
    getDisplayString().setTagArg("t", 0, buf);
}

void GenericNetworkProtocol::handleMessage(cMessage *msg)
{
    if (!msg->isPacket())
        handleCommand(msg);
    else
        QueueBase::handleMessage(msg);
}

void GenericNetworkProtocol::handleCommand(cMessage *msg)
{
    if (L3SocketBindCommand *command = dynamic_cast<L3SocketBindCommand *>(msg->getControlInfo())) {
        int socketId = msg->getMandatoryTag<SocketReq>()->getSocketId();
        SocketDescriptor *descriptor = new SocketDescriptor(socketId, command->getProtocolId());
        socketIdToSocketDescriptor[socketId] = descriptor;
        protocolIdToSocketDescriptors.insert(std::pair<int, SocketDescriptor *>(command->getProtocolId(), descriptor));
        delete msg;
    }
    else if (dynamic_cast<L3SocketCloseCommand *>(msg->getControlInfo()) != nullptr) {
        int socketId = msg->getMandatoryTag<SocketReq>()->getSocketId();
        auto it = socketIdToSocketDescriptor.find(socketId);
        if (it != socketIdToSocketDescriptor.end()) {
            int protocol = it->second->protocolId;
            auto lowerBound = protocolIdToSocketDescriptors.lower_bound(protocol);
            auto upperBound = protocolIdToSocketDescriptors.upper_bound(protocol);
            for (auto jt = lowerBound; jt != upperBound; jt++) {
                if (it->second == jt->second) {
                    protocolIdToSocketDescriptors.erase(jt);
                    break;
                }
            }
            delete it->second;
            socketIdToSocketDescriptor.erase(it);
        }
        delete msg;
    }
    else
        throw cRuntimeError("Invalid command: (%s)%s", msg->getClassName(), msg->getName());
}

void GenericNetworkProtocol::endService(cPacket *pk)
{
    if (pk->getArrivalGate()->isName("transportIn"))
        handlePacketFromHL(check_and_cast<Packet *>(pk));
    else {
        handlePacketFromNetwork(check_and_cast<Packet *>(pk));
    }
}

const InterfaceEntry *GenericNetworkProtocol::getSourceInterfaceFrom(cPacket *packet)
{
    auto interfaceInd = packet->getTag<InterfaceInd>();
    return interfaceInd != nullptr ? interfaceTable->getInterfaceById(interfaceInd->getInterfaceId()) : nullptr;
}

void GenericNetworkProtocol::handlePacketFromNetwork(Packet *packet)
{
    if (packet->hasBitError()) {
        //TODO discard
    }

    const auto& header = packet->peekHeader<GenericDatagramHeader>();
    packet->ensureTag<NetworkProtocolInd>()->setProtocol(&Protocol::gnp);
    packet->ensureTag<NetworkProtocolInd>()->setNetworkProtocolHeader(header);

    const InterfaceEntry *inIE = interfaceTable->getInterfaceById(packet->getMandatoryTag<InterfaceInd>()->getInterfaceId());
    const InterfaceEntry *destIE = nullptr;

    EV_DETAIL << "Received datagram `" << packet->getName() << "' with dest=" << header->getDestAddr() << " from " << header->getSrcAddr() << " in interface" << inIE->getInterfaceName() << "\n";

    if (datagramPreRoutingHook(packet) != IHook::ACCEPT)
        return;

    inIE = interfaceTable->getInterfaceById(packet->getMandatoryTag<InterfaceInd>()->getInterfaceId());
    auto destIeTag = packet->getTag<InterfaceReq>();
    destIE = destIeTag ? interfaceTable->getInterfaceById(destIeTag->getInterfaceId()) : nullptr;
    auto nextHopTag = packet->getTag<NextHopAddressReq>();
    L3Address nextHop = (nextHopTag) ? nextHopTag->getNextHopAddress() : L3Address();
    datagramPreRouting(packet, inIE, destIE, nextHop);
}

void GenericNetworkProtocol::handlePacketFromHL(Packet *packet)
{
    // if no interface exists, do not send datagram
    if (interfaceTable->getNumInterfaces() == 0) {
        EV_INFO << "No interfaces exist, dropping packet\n";
        delete packet;
        return;
    }

    // encapsulate and send
    const InterfaceEntry *destIE;    // will be filled in by encapsulate()
    encapsulate(packet, destIE);

    L3Address nextHop;
    if (datagramLocalOutHook(packet) != IHook::ACCEPT)
        return;

    auto destIeTag = packet->getTag<InterfaceReq>();
    destIE = destIeTag ? interfaceTable->getInterfaceById(destIeTag->getInterfaceId()) : nullptr;
    auto nextHopTag = packet->getTag<NextHopAddressReq>();
    nextHop = (nextHopTag) ? nextHopTag->getNextHopAddress() : L3Address();

    datagramLocalOut(packet, destIE, nextHop);
}

void GenericNetworkProtocol::routePacket(Packet *datagram, const InterfaceEntry *destIE, const L3Address& requestedNextHop, bool fromHL)
{
    // TBD add option handling code here

    auto header = datagram->peekHeader<GenericDatagramHeader>();
    L3Address destAddr = header->getDestinationAddress();

    EV_INFO << "Routing datagram `" << datagram->getName() << "' with dest=" << destAddr << ": ";

    // check for local delivery
    if (routingTable->isLocalAddress(destAddr)) {
        EV_INFO << "local delivery\n";
        if (fromHL && header->getSourceAddress().isUnspecified()) {
            datagram->removePoppedHeaders();
            const auto& newHeader = removeNetworkProtocolHeader<GenericDatagramHeader>(datagram);
            newHeader->setSourceAddress(destAddr); // allows two apps on the same host to communicate
            insertNetworkProtocolHeader(datagram, Protocol::gnp, newHeader);
            header = newHeader;
        }
        numLocalDeliver++;

        if (datagramLocalInHook(datagram) != INetfilter::IHook::ACCEPT)
            return;

        sendDatagramToHL(datagram);
        return;
    }

    // if datagram arrived from input gate and Generic_FORWARD is off, delete datagram
    if (!fromHL && !routingTable->isForwardingEnabled()) {
        EV_INFO << "forwarding off, dropping packet\n";
        numDropped++;
        delete datagram;
        return;
    }

    if (!fromHL) {
        datagram->removePoppedChunks();
    }

    // if output port was explicitly requested, use that, otherwise use GenericNetworkProtocol routing
    // TODO: see IPv4, using destIE here leaves nextHope unspecified
    L3Address nextHop;
    if (destIE && !requestedNextHop.isUnspecified()) {
        EV_DETAIL << "using manually specified output interface " << destIE->getInterfaceName() << "\n";
        nextHop = requestedNextHop;
    }
    else {
        // use GenericNetworkProtocol routing (lookup in routing table)
        const GenericRoute *re = routingTable->findBestMatchingRoute(destAddr);

        // error handling: destination address does not exist in routing table:
        // throw packet away and continue
        if (re == nullptr) {
            EV_INFO << "unroutable, discarding packet\n";
            numUnroutable++;
            delete datagram;
            return;
        }

        // extract interface and next-hop address from routing table entry
        destIE = re->getInterface();
        nextHop = re->getNextHopAsGeneric();
    }

    if (!fromHL) {
        datagram->removePoppedHeaders();
        const auto& newHeader = removeNetworkProtocolHeader<GenericDatagramHeader>(datagram);
        newHeader->setHopLimit(header->getHopLimit() - 1);
        insertNetworkProtocolHeader(datagram, Protocol::gnp, newHeader);
        header = newHeader;
    }

    // set datagram source address if not yet set
    if (header->getSourceAddress().isUnspecified()) {
        datagram->removePoppedHeaders();
        const auto& newHeader = removeNetworkProtocolHeader<GenericDatagramHeader>(datagram);
        newHeader->setSourceAddress(destIE->getGenericNetworkProtocolData()->getAddress());
        insertNetworkProtocolHeader(datagram, Protocol::gnp, newHeader);
        header = newHeader;
    }

    // default: send datagram to fragmentation
    EV_INFO << "output interface is " << destIE->getInterfaceName() << ", next-hop address: " << nextHop << "\n";
    numForwarded++;

    sendDatagramToOutput(datagram, destIE, nextHop);
}

void GenericNetworkProtocol::routeMulticastPacket(Packet *datagram, const InterfaceEntry *destIE, const InterfaceEntry *fromIE)
{
    const auto& header = datagram->peekHeader<GenericDatagramHeader>();
    L3Address destAddr = header->getDestinationAddress();
    // if received from the network...
    if (fromIE != nullptr) {
        //TODO: decrement hopLimit before forward frame to another host

        // check for local delivery
        if (routingTable->isLocalMulticastAddress(destAddr))
            sendDatagramToHL(datagram);
//
//        // don't forward if GenericNetworkProtocol forwarding is off
//        if (!rt->isGenericForwardingEnabled())
//        {
//            delete datagram;
//            return;
//        }
//
//        // don't forward if dest address is link-scope
//        if (destAddr.isLinkLocalMulticast())
//        {
//            delete datagram;
//            return;
//        }
    }
    else {
        //TODO
        for (int i = 0; i < interfaceTable->getNumInterfaces(); ++i) {
            const InterfaceEntry *destIE = interfaceTable->getInterface(i);
            if (!destIE->isLoopback())
                sendDatagramToOutput(datagram->dup(), destIE, header->getDestinationAddress());
        }
        delete datagram;
    }

//    Address destAddr = datagram->getDestinationAddress();
//    EV << "Routing multicast datagram `" << datagram->getName() << "' with dest=" << destAddr << "\n";
//
//    numMulticast++;
//
//    // DVMRP: process datagram only if sent locally or arrived on the shortest
//    // route (provided routing table already contains srcAddr); otherwise
//    // discard and continue.
//    const InterfaceEntry *shortestPathIE = rt->getInterfaceForDestinationAddr(datagram->getSourceAddress());
//    if (fromIE!=nullptr && shortestPathIE!=nullptr && fromIE!=shortestPathIE)
//    {
//        // FIXME count dropped
//        EV << "Packet dropped.\n";
//        delete datagram;
//        return;
//    }
//
//    // if received from the network...
//    if (fromIE!=nullptr)
//    {
//        // check for local delivery
//        if (rt->isLocalMulticastAddress(destAddr))
//        {
//            GenericDatagram *datagramCopy = datagram->dup();
//
//            // FIXME code from the MPLS model: set packet dest address to routerId (???)
//            datagramCopy->setDestinationAddress(rt->getRouterId());
//
//            reassembleAndDeliver(datagramCopy);
//        }
//
//        // don't forward if GenericNetworkProtocol forwarding is off
//        if (!rt->isGenericForwardingEnabled())
//        {
//            delete datagram;
//            return;
//        }
//
//        // don't forward if dest address is link-scope
//        if (destAddr.isLinkLocalMulticast())
//        {
//            delete datagram;
//            return;
//        }
//
//    }
//
//    // routed explicitly via Generic_MULTICAST_IF
//    if (destIE!=nullptr)
//    {
//        ASSERT(datagram->getDestinationAddress().isMulticast());
//
//        EV << "multicast packet explicitly routed via output interface " << destIE->getName() << endl;
//
//        // set datagram source address if not yet set
//        if (datagram->getSourceAddress().isUnspecified())
//            datagram->setSourceAddress(destIE->ipv4Data()->getGenericAddress());
//
//        // send
//        sendDatagramToOutput(datagram, destIE, datagram->getDestinationAddress());
//
//        return;
//    }
//
//    // now: routing
//    MulticastRoutes routes = rt->getMulticastRoutesFor(destAddr);
//    if (routes.size()==0)
//    {
//        // no destination: delete datagram
//        delete datagram;
//    }
//    else
//    {
//        // copy original datagram for multiple destinations
//        for (unsigned int i=0; i<routes.size(); i++)
//        {
//            const InterfaceEntry *destIE = routes[i].interf;
//
//            // don't forward to input port
//            if (destIE && destIE!=fromIE)
//            {
//                GenericDatagram *datagramCopy = datagram->dup();
//
//                // set datagram source address if not yet set
//                if (datagramCopy->getSourceAddress().isUnspecified())
//                    datagramCopy->setSourceAddress(destIE->ipv4Data()->getGenericAddress());
//
//                // send
//                Address nextHop = routes[i].gateway;
//                sendDatagramToOutput(datagramCopy, destIE, nextHop);
//            }
//        }
//
//        // only copies sent, delete original datagram
//        delete datagram;
//    }
}

void GenericNetworkProtocol::decapsulate(Packet *packet)
{
    // decapsulate transport packet
    const InterfaceEntry *fromIE = getSourceInterfaceFrom(packet);
    const auto& header = packet->popHeader<GenericDatagramHeader>();

    // create and fill in control info
    if (fromIE) {
        auto ifTag = packet->ensureTag<InterfaceInd>();
        ifTag->setInterfaceId(fromIE->getInterfaceId());
    }

    // attach control info
    packet->ensureTag<PacketProtocolTag>()->setProtocol(header->getProtocol());
    packet->ensureTag<DispatchProtocolReq>()->setProtocol(header->getProtocol());
    packet->ensureTag<NetworkProtocolInd>()->setProtocol(&Protocol::gnp);
    packet->ensureTag<NetworkProtocolInd>()->setNetworkProtocolHeader(header);
    auto l3AddressInd = packet->ensureTag<L3AddressInd>();
    l3AddressInd->setSrcAddress(header->getSourceAddress());
    l3AddressInd->setDestAddress(header->getDestinationAddress());
    packet->ensureTag<HopLimitInd>()->setHopLimit(header->getHopLimit());
}

void GenericNetworkProtocol::encapsulate(Packet *transportPacket, const InterfaceEntry *& destIE)
{
    auto header = makeShared<GenericDatagramHeader>();
    header->setChunkLength(B(par("headerLength").longValue()));
    auto l3AddressReq = transportPacket->removeMandatoryTag<L3AddressReq>();
    L3Address src = l3AddressReq->getSrcAddress();
    L3Address dest = l3AddressReq->getDestAddress();
    delete l3AddressReq;

    header->setProtocol(transportPacket->getMandatoryTag<PacketProtocolTag>()->getProtocol());

    auto hopLimitReq = transportPacket->removeTag<HopLimitReq>();
    short ttl = (hopLimitReq != nullptr) ? hopLimitReq->getHopLimit() : -1;
    delete hopLimitReq;

    // set source and destination address
    header->setDestinationAddress(dest);

    // Generic_MULTICAST_IF option, but allow interface selection for unicast packets as well
    auto ifTag = transportPacket->getTag<InterfaceReq>();
    destIE = ifTag ? interfaceTable->getInterfaceById(ifTag->getInterfaceId()) : nullptr;


    // when source address was given, use it; otherwise it'll get the address
    // of the outgoing interface after routing
    if (!src.isUnspecified()) {
        // if interface parameter does not match existing interface, do not send datagram
        if (routingTable->getInterfaceByAddress(src) == nullptr)
            throw cRuntimeError("Wrong source address %s in (%s)%s: no interface with such address",
                    src.str().c_str(), transportPacket->getClassName(), transportPacket->getFullName());
        header->setSourceAddress(src);
    }

    // set other fields
    if (ttl != -1) {
        ASSERT(ttl > 0);
    }
    else if (false) //TODO: datagram->getDestinationAddress().isLinkLocalMulticast())
        ttl = 1;
    else
        ttl = defaultHopLimit;

    header->setHopLimit(ttl);

    // setting GenericNetworkProtocol options is currently not supported

    delete transportPacket->removeControlInfo();

    insertNetworkProtocolHeader(transportPacket, Protocol::gnp, header);
}

void GenericNetworkProtocol::sendDatagramToHL(Packet *packet)
{
    const auto& header = packet->peekHeader<GenericDatagramHeader>();
    int protocol = header->getProtocolId();
    decapsulate(packet);
    // deliver to sockets
    auto lowerBound = protocolIdToSocketDescriptors.lower_bound(protocol);
    auto upperBound = protocolIdToSocketDescriptors.upper_bound(protocol);
    bool hasSocket = lowerBound != upperBound;
    for (auto it = lowerBound; it != upperBound; it++) {
        cPacket *packetCopy = packet->dup();
        packetCopy->ensureTag<SocketInd>()->setSocketId(it->second->socketId);
        send(packetCopy, "transportOut");
    }
    if (mapping.findOutputGateForProtocol(protocol) >= 0) {
        send(packet, "transportOut");
        numLocalDeliver++;
    }
    else if (!hasSocket) {
        EV_ERROR << "Transport protocol ID=" << protocol << " not connected, discarding packet\n";
        //TODO send an ICMP error: protocol unreachable
        // sendToIcmp(datagram, inputInterfaceId, ICMP_DESTINATION_UNREACHABLE, ICMP_DU_PROTOCOL_UNREACHABLE);
        delete packet;
    }
}

void GenericNetworkProtocol::sendDatagramToOutput(Packet *datagram, const InterfaceEntry *ie, L3Address nextHop)
{
    delete datagram->removeControlInfo();

    if (datagram->getByteLength() > ie->getMTU())
        throw cRuntimeError("datagram too large"); //TODO refine

    const auto& header = datagram->peekHeader<GenericDatagramHeader>();
    // hop counter check
    if (header->getHopLimit() <= 0) {
        EV_INFO << "datagram hopLimit reached zero, discarding\n";
        delete datagram;    //TODO stats counter???
        return;
    }

    if (!ie->isBroadcast()) {
        EV_DETAIL << "output interface " << ie->getInterfaceName() << " is not broadcast, skipping ARP\n";
        //Peer to peer interface, no broadcast, no MACAddress. // packet->ensureTag<MACAddressReq>()->setDestinationAddress(macAddress);
        delete datagram->removeTag<DispatchProtocolReq>();
        datagram->ensureTag<InterfaceReq>()->setInterfaceId(ie->getInterfaceId());
        datagram->ensureTag<DispatchProtocolInd>()->setProtocol(&Protocol::gnp);
        datagram->ensureTag<PacketProtocolTag>()->setProtocol(&Protocol::gnp);
        send(datagram, "queueOut");
        return;
    }

    // determine what address to look up in ARP cache
    if (nextHop.isUnspecified()) {
        nextHop = header->getDestinationAddress();
        EV_WARN << "no next-hop address, using destination address " << nextHop << " (proxy ARP)\n";
    }

    // send out datagram to NIC, with control info attached
    MACAddress nextHopMAC = arp->resolveL3Address(nextHop, ie);
    if (nextHopMAC == MACAddress::UNSPECIFIED_ADDRESS) {
        throw cRuntimeError("ARP couldn't resolve the '%s' address", nextHop.str().c_str());
    }
    else {
        // add control info with MAC address
        datagram->ensureTag<MacAddressReq>()->setDestAddress(nextHopMAC);
        delete datagram->removeTag<DispatchProtocolReq>();
        datagram->ensureTag<InterfaceReq>()->setInterfaceId(ie->getInterfaceId());
        datagram->ensureTag<DispatchProtocolInd>()->setProtocol(&Protocol::gnp);
        datagram->ensureTag<PacketProtocolTag>()->setProtocol(&Protocol::gnp);

        // send out
        send(datagram, "queueOut");
    }
}

void GenericNetworkProtocol::datagramPreRouting(Packet *datagram, const InterfaceEntry *inIE, const InterfaceEntry *destIE, const L3Address& nextHop)
{
    const auto& header = datagram->peekHeader<GenericDatagramHeader>();
    // route packet
    if (!header->getDestinationAddress().isMulticast())
        routePacket(datagram, destIE, nextHop, false);
    else
        routeMulticastPacket(datagram, destIE, inIE);
}

void GenericNetworkProtocol::datagramLocalIn(Packet *packet, const InterfaceEntry *inIE)
{
    sendDatagramToHL(packet);
}

void GenericNetworkProtocol::datagramLocalOut(Packet *datagram, const InterfaceEntry *destIE, const L3Address& nextHop)
{
    const auto& header = datagram->peekHeader<GenericDatagramHeader>();
    // route packet
    if (!header->getDestinationAddress().isMulticast())
        routePacket(datagram, destIE, nextHop, true);
    else
        routeMulticastPacket(datagram, destIE, nullptr);
}

void GenericNetworkProtocol::registerHook(int priority, IHook *hook)
{
    Enter_Method("registerHook()");
    NetfilterBase::registerHook(priority, hook);
}

void GenericNetworkProtocol::unregisterHook(IHook *hook)
{
    Enter_Method("unregisterHook()");
    NetfilterBase::unregisterHook(hook);
}

void GenericNetworkProtocol::dropQueuedDatagram(const Packet *datagram)
{
    Enter_Method("dropDatagram()");
    for (auto iter = queuedDatagramsForHooks.begin(); iter != queuedDatagramsForHooks.end(); iter++) {
        if (iter->datagram == datagram) {
            delete datagram;
            queuedDatagramsForHooks.erase(iter);
            return;
        }
    }
}

void GenericNetworkProtocol::reinjectQueuedDatagram(const Packet *datagram)
{
    Enter_Method("reinjectDatagram()");
    for (auto iter = queuedDatagramsForHooks.begin(); iter != queuedDatagramsForHooks.end(); iter++) {
        if (iter->datagram == datagram) {
            Packet *datagram = iter->datagram;
            const InterfaceEntry *inIE = iter->inIE;
            const InterfaceEntry *outIE = iter->outIE;
            const L3Address& nextHop = iter->nextHop;
            INetfilter::IHook::Type hookType = iter->hookType;
            switch (hookType) {
                case INetfilter::IHook::PREROUTING:
                    datagramPreRouting(datagram, inIE, outIE, nextHop);
                    break;

                case INetfilter::IHook::LOCALIN:
                    datagramLocalIn(datagram, inIE);
                    break;

                case INetfilter::IHook::LOCALOUT:
                    datagramLocalOut(datagram, outIE, nextHop);
                    break;

                default:
                    throw cRuntimeError("Re-injection of datagram queued for this hook not implemented");
                    break;
            }
            queuedDatagramsForHooks.erase(iter);
            return;
        }
    }
}

INetfilter::IHook::Result GenericNetworkProtocol::datagramPreRoutingHook(Packet *datagram)
{
    for (auto & elem : hooks) {
        IHook::Result r = elem.second->datagramPreRoutingHook(datagram);
        switch (r) {
            case IHook::ACCEPT:
                break;    // continue iteration

            case IHook::DROP:
                delete datagram;
                return r;

            case IHook::QUEUE:
                if (datagram->getOwner() != this)
                    throw cRuntimeError("Model error: netfilter hook changed the owner of queued datagram '%s'", datagram->getFullName());
                queuedDatagramsForHooks.push_back(QueuedDatagramForHook(datagram, INetfilter::IHook::PREROUTING));
                return r;

            case IHook::STOLEN:
                return r;

            default:
                throw cRuntimeError("Unknown Hook::Result value: %d", (int)r);
        }
    }
    return IHook::ACCEPT;
}

INetfilter::IHook::Result GenericNetworkProtocol::datagramForwardHook(Packet *datagram)
{
    for (auto & elem : hooks) {
        IHook::Result r = elem.second->datagramForwardHook(datagram);
        switch (r) {
            case IHook::ACCEPT:
                break;    // continue iteration

            case IHook::DROP:
                delete datagram;
                return r;

            case IHook::QUEUE:
                queuedDatagramsForHooks.push_back(QueuedDatagramForHook(datagram, INetfilter::IHook::FORWARD));
                return r;

            case IHook::STOLEN:
                return r;

            default:
                throw cRuntimeError("Unknown Hook::Result value: %d", (int)r);
        }
    }
    return IHook::ACCEPT;
}

INetfilter::IHook::Result GenericNetworkProtocol::datagramPostRoutingHook(Packet *datagram)
{
    for (auto & elem : hooks) {
        IHook::Result r = elem.second->datagramPostRoutingHook(datagram);
        switch (r) {
            case IHook::ACCEPT:
                break;    // continue iteration

            case IHook::DROP:
                delete datagram;
                return r;

            case IHook::QUEUE:
                queuedDatagramsForHooks.push_back(QueuedDatagramForHook(datagram, INetfilter::IHook::POSTROUTING));
                return r;

            case IHook::STOLEN:
                return r;

            default:
                throw cRuntimeError("Unknown Hook::Result value: %d", (int)r);
        }
    }
    return IHook::ACCEPT;
}

INetfilter::IHook::Result GenericNetworkProtocol::datagramLocalInHook(Packet *datagram)
{
    L3Address address;
    for (auto & elem : hooks) {
        IHook::Result r = elem.second->datagramLocalInHook(datagram);
        switch (r) {
            case IHook::ACCEPT:
                break;    // continue iteration

            case IHook::DROP:
                delete datagram;
                return r;

            case IHook::QUEUE:
                queuedDatagramsForHooks.push_back(QueuedDatagramForHook(datagram, INetfilter::IHook::LOCALIN));
                return r;

            case IHook::STOLEN:
                return r;

            default:
                throw cRuntimeError("Unknown Hook::Result value: %d", (int)r);
        }
    }
    return IHook::ACCEPT;
}

INetfilter::IHook::Result GenericNetworkProtocol::datagramLocalOutHook(Packet *datagram)
{
    for (auto & elem : hooks) {
        IHook::Result r = elem.second->datagramLocalOutHook(datagram);
        switch (r) {
            case IHook::ACCEPT:
                break;    // continue iteration

            case IHook::DROP:
                delete datagram;
                return r;

            case IHook::QUEUE:
                queuedDatagramsForHooks.push_back(QueuedDatagramForHook(datagram, INetfilter::IHook::LOCALOUT));
                return r;

            case IHook::STOLEN:
                return r;

            default:
                throw cRuntimeError("Unknown Hook::Result value: %d", (int)r);
        }
    }
    return IHook::ACCEPT;
}

} // namespace inet

