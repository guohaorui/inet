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
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

#include "inet/common/INETUtils.h"
#include "inet/common/INETMath.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/common/ProtocolGroup.h"
#include "inet/networklayer/common/InterfaceEntry.h"
#include "inet/common/ModuleAccess.h"
#include "inet/linklayer/bmac/BMacHeader_m.h"
#include "inet/linklayer/bmac/BMacLayer.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/linklayer/common/MACAddressTag_m.h"

namespace inet {

Define_Module(BMacLayer);

void BMacLayer::initialize(int stage)
{
    MACProtocolBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        queueLength = par("queueLength");
        animation = par("animation");
        slotDuration = par("slotDuration");
        bitrate = par("bitrate");
        headerLength = par("headerLength");
        checkInterval = par("checkInterval");
        useMacAcks = par("useMACAcks");
        maxTxAttempts = par("maxTxAttempts");
        EV_DETAIL << "headerLength: " << headerLength << ", bitrate: " << bitrate << endl;

        nbTxDataPackets = 0;
        nbTxPreambles = 0;
        nbRxDataPackets = 0;
        nbRxPreambles = 0;
        nbMissedAcks = 0;
        nbRecvdAcks = 0;
        nbDroppedDataPackets = 0;
        nbTxAcks = 0;

        txAttempts = 0;
        lastDataPktDestAddr = MACAddress::BROADCAST_ADDRESS;
        lastDataPktSrcAddr = MACAddress::BROADCAST_ADDRESS;

        macState = INIT;

        initializeMACAddress();
        registerInterface();

        cModule *radioModule = getModuleFromPar<cModule>(par("radioModule"), this);
        radioModule->subscribe(IRadio::radioModeChangedSignal, this);
        radioModule->subscribe(IRadio::transmissionStateChangedSignal, this);
        radio = check_and_cast<IRadio *>(radioModule);

        // init the dropped packet info
        WATCH(macState);
    }
    else if (stage == INITSTAGE_LINK_LAYER) {
        wakeup = new cMessage("wakeup");
        wakeup->setKind(BMAC_WAKE_UP);

        data_timeout = new cMessage("data_timeout");
        data_timeout->setKind(BMAC_DATA_TIMEOUT);
        data_timeout->setSchedulingPriority(100);

        data_tx_over = new cMessage("data_tx_over");
        data_tx_over->setKind(BMAC_DATA_TX_OVER);

        stop_preambles = new cMessage("stop_preambles");
        stop_preambles->setKind(BMAC_STOP_PREAMBLES);

        send_preamble = new cMessage("send_preamble");
        send_preamble->setKind(BMAC_SEND_PREAMBLE);

        ack_tx_over = new cMessage("ack_tx_over");
        ack_tx_over->setKind(BMAC_ACK_TX_OVER);

        cca_timeout = new cMessage("cca_timeout");
        cca_timeout->setKind(BMAC_CCA_TIMEOUT);
        cca_timeout->setSchedulingPriority(100);

        send_ack = new cMessage("send_ack");
        send_ack->setKind(BMAC_SEND_ACK);

        start_bmac = new cMessage("start_bmac");
        start_bmac->setKind(BMAC_START_BMAC);

        ack_timeout = new cMessage("ack_timeout");
        ack_timeout->setKind(BMAC_ACK_TIMEOUT);

        resend_data = new cMessage("resend_data");
        resend_data->setKind(BMAC_RESEND_DATA);
        resend_data->setSchedulingPriority(100);

        scheduleAt(0.0, start_bmac);
    }
}

BMacLayer::~BMacLayer()
{
    cancelAndDelete(wakeup);
    cancelAndDelete(data_timeout);
    cancelAndDelete(data_tx_over);
    cancelAndDelete(stop_preambles);
    cancelAndDelete(send_preamble);
    cancelAndDelete(ack_tx_over);
    cancelAndDelete(cca_timeout);
    cancelAndDelete(send_ack);
    cancelAndDelete(start_bmac);
    cancelAndDelete(ack_timeout);
    cancelAndDelete(resend_data);

    for (auto & elem : macQueue) {
        delete (elem);
    }
    macQueue.clear();
}

void BMacLayer::finish()
{
    recordScalar("nbTxDataPackets", nbTxDataPackets);
    recordScalar("nbTxPreambles", nbTxPreambles);
    recordScalar("nbRxDataPackets", nbRxDataPackets);
    recordScalar("nbRxPreambles", nbRxPreambles);
    recordScalar("nbMissedAcks", nbMissedAcks);
    recordScalar("nbRecvdAcks", nbRecvdAcks);
    recordScalar("nbTxAcks", nbTxAcks);
    recordScalar("nbDroppedDataPackets", nbDroppedDataPackets);
    //recordScalar("timeSleep", timeSleep);
    //recordScalar("timeRX", timeRX);
    //recordScalar("timeTX", timeTX);
}

void BMacLayer::initializeMACAddress()
{
    const char *addrstr = par("address");

    if (!strcmp(addrstr, "auto")) {
        // assign automatic address
        address = MACAddress::generateAutoAddress();

        // change module parameter from "auto" to concrete address
        par("address").setStringValue(address.str().c_str());
    }
    else {
        address.setAddress(addrstr);
    }
}

InterfaceEntry *BMacLayer::createInterfaceEntry()
{
    InterfaceEntry *e = getContainingNicModule(this);

    // data rate
    e->setDatarate(bitrate);

    // generate a link-layer address to be used as interface token for IPv6
    e->setMACAddress(address);
    e->setInterfaceToken(address.formInterfaceIdentifier());

    // capabilities
    e->setMtu(par("mtu").longValue());
    e->setMulticast(false);
    e->setBroadcast(true);

    return e;
}

/**
 * Check whether the queue is not full: if yes, print a warning and drop the
 * packet. Then initiate sending of the packet, if the node is sleeping. Do
 * nothing, if node is working.
 */
void BMacLayer::handleUpperPacket(Packet *packet)
{
    bool pktAdded = addToQueue(packet);
    if (!pktAdded)
        return;
    // force wakeup now
    if (wakeup->isScheduled() && (macState == SLEEP)) {
        cancelEvent(wakeup);
        scheduleAt(simTime() + dblrand() * 0.1f, wakeup);
    }
}

/**
 * Send one short preamble packet immediately.
 */
void BMacLayer::sendPreamble()
{
    auto preamble = makeShared<BMacHeader>();
    preamble->setSrcAddr(address);
    preamble->setDestAddr(MACAddress::BROADCAST_ADDRESS);
    preamble->setChunkLength(b(headerLength));
    preamble->markImmutable();

    //attach signal and send down
    auto packet = new Packet();
    packet->setKind(BMAC_PREAMBLE);
    packet->pushHeader(preamble);
    attachSignal(packet);
    sendDown(packet);
    nbTxPreambles++;
}

/**
 * Send one short preamble packet immediately.
 */
void BMacLayer::sendMacAck()
{
    auto ack = makeShared<BMacHeader>();
    ack->setSrcAddr(address);
    ack->setDestAddr(lastDataPktSrcAddr);
    ack->setChunkLength(b(headerLength));

    //attach signal and send down
    auto packet = new Packet();
    packet->setKind(BMAC_ACK);
    packet->pushHeader(ack);
    attachSignal(packet);
    sendDown(packet);
    nbTxAcks++;
    //endSimulation();
}

/**
 * Handle own messages:
 * BMAC_WAKEUP: wake up the node, check the channel for some time.
 * BMAC_CHECK_CHANNEL: if the channel is free, check whether there is something
 * in the queue and switch the radio to TX. When switched to TX, the node will
 * start sending preambles for a full slot duration. If the channel is busy,
 * stay awake to receive message. Schedule a timeout to handle false alarms.
 * BMAC_SEND_PREAMBLES: sending of preambles over. Next time the data packet
 * will be send out (single one).
 * BMAC_TIMEOUT_DATA: timeout the node after a false busy channel alarm. Go
 * back to sleep.
 */
void BMacLayer::handleSelfMessage(cMessage *msg)
{
    switch (macState) {
        case INIT:
            if (msg->getKind() == BMAC_START_BMAC) {
                EV_DETAIL << "State INIT, message BMAC_START, new state SLEEP" << endl;
                radio->setRadioMode(IRadio::RADIO_MODE_SLEEP);
                macState = SLEEP;
                scheduleAt(simTime() + dblrand() * slotDuration, wakeup);
                return;
            }
            break;

        case SLEEP:
            if (msg->getKind() == BMAC_WAKE_UP) {
                EV_DETAIL << "State SLEEP, message BMAC_WAKEUP, new state CCA" << endl;
                scheduleAt(simTime() + checkInterval, cca_timeout);
                radio->setRadioMode(IRadio::RADIO_MODE_RECEIVER);
                macState = CCA;
                return;
            }
            break;

        case CCA:
            if (msg->getKind() == BMAC_CCA_TIMEOUT) {
                // channel is clear
                // something waiting in eth queue?
                if (macQueue.size() > 0) {
                    EV_DETAIL << "State CCA, message CCA_TIMEOUT, new state"
                                 " SEND_PREAMBLE" << endl;
                    macState = SEND_PREAMBLE;
                    radio->setRadioMode(IRadio::RADIO_MODE_TRANSMITTER);
                    scheduleAt(simTime() + slotDuration, stop_preambles);
                    return;
                }
                // if not, go back to sleep and wake up after a full period
                else {
                    EV_DETAIL << "State CCA, message CCA_TIMEOUT, new state SLEEP"
                              << endl;
                    scheduleAt(simTime() + slotDuration, wakeup);
                    macState = SLEEP;
                    radio->setRadioMode(IRadio::RADIO_MODE_SLEEP);
                    return;
                }
            }
            // during CCA, we received a preamble. Go to state WAIT_DATA and
            // schedule the timeout.
            if (msg->getKind() == BMAC_PREAMBLE) {
                nbRxPreambles++;
                EV_DETAIL << "State CCA, message BMAC_PREAMBLE received, new state"
                             " WAIT_DATA" << endl;
                macState = WAIT_DATA;
                cancelEvent(cca_timeout);
                scheduleAt(simTime() + slotDuration + checkInterval, data_timeout);
                delete msg;
                return;
            }
            // this case is very, very, very improbable, but let's do it.
            // if in CCA and the node receives directly the data packet, switch to
            // state WAIT_DATA and re-send the message
            if (msg->getKind() == BMAC_DATA) {
                nbRxDataPackets++;
                EV_DETAIL << "State CCA, message BMAC_DATA, new state WAIT_DATA"
                          << endl;
                macState = WAIT_DATA;
                cancelEvent(cca_timeout);
                scheduleAt(simTime() + slotDuration + checkInterval, data_timeout);
                scheduleAt(simTime(), msg);
                return;
            }
            //in case we get an ACK, we simply dicard it, because it means the end
            //of another communication
            if (msg->getKind() == BMAC_ACK) {
                EV_DETAIL << "State CCA, message BMAC_ACK, new state CCA" << endl;
                delete msg;
                return;
            }
            break;

        case SEND_PREAMBLE:
            if (msg->getKind() == BMAC_SEND_PREAMBLE) {
                EV_DETAIL << "State SEND_PREAMBLE, message BMAC_SEND_PREAMBLE, new"
                             " state SEND_PREAMBLE" << endl;
                sendPreamble();
                scheduleAt(simTime() + 0.5f * checkInterval, send_preamble);
                macState = SEND_PREAMBLE;
                return;
            }
            // simply change the state to SEND_DATA
            if (msg->getKind() == BMAC_STOP_PREAMBLES) {
                EV_DETAIL << "State SEND_PREAMBLE, message BMAC_STOP_PREAMBLES, new"
                             " state SEND_DATA" << endl;
                macState = SEND_DATA;
                txAttempts = 1;
                return;
            }
            break;

        case SEND_DATA:
            if ((msg->getKind() == BMAC_SEND_PREAMBLE)
                || (msg->getKind() == BMAC_RESEND_DATA))
            {
                EV_DETAIL << "State SEND_DATA, message BMAC_SEND_PREAMBLE or"
                             " BMAC_RESEND_DATA, new state WAIT_TX_DATA_OVER" << endl;
                // send the data packet
                sendDataPacket();
                macState = WAIT_TX_DATA_OVER;
                return;
            }
            break;

        case WAIT_TX_DATA_OVER:
            if (msg->getKind() == BMAC_DATA_TX_OVER) {
                if ((useMacAcks) && !lastDataPktDestAddr.isBroadcast()) {
                    EV_DETAIL << "State WAIT_TX_DATA_OVER, message BMAC_DATA_TX_OVER,"
                                 " new state WAIT_ACK" << endl;
                    macState = WAIT_ACK;
                    radio->setRadioMode(IRadio::RADIO_MODE_RECEIVER);
                    scheduleAt(simTime() + checkInterval, ack_timeout);
                }
                else {
                    EV_DETAIL << "State WAIT_TX_DATA_OVER, message BMAC_DATA_TX_OVER,"
                                 " new state  SLEEP" << endl;
                    delete macQueue.front();
                    macQueue.pop_front();
                    // if something in the queue, wakeup soon.
                    if (macQueue.size() > 0)
                        scheduleAt(simTime() + dblrand() * checkInterval, wakeup);
                    else
                        scheduleAt(simTime() + slotDuration, wakeup);
                    macState = SLEEP;
                    radio->setRadioMode(IRadio::RADIO_MODE_SLEEP);
                }
                return;
            }
            break;

        case WAIT_ACK:
            if (msg->getKind() == BMAC_ACK_TIMEOUT) {
                // No ACK received. try again or drop.
                if (txAttempts < maxTxAttempts) {
                    EV_DETAIL << "State WAIT_ACK, message BMAC_ACK_TIMEOUT, new state"
                                 " SEND_DATA" << endl;
                    txAttempts++;
                    macState = SEND_PREAMBLE;
                    scheduleAt(simTime() + slotDuration, stop_preambles);
                    radio->setRadioMode(IRadio::RADIO_MODE_TRANSMITTER);
                }
                else {
                    EV_DETAIL << "State WAIT_ACK, message BMAC_ACK_TIMEOUT, new state"
                                 " SLEEP" << endl;
                    //drop the packet
                    cMessage *mac = macQueue.front();
                    macQueue.pop_front();
                    emit(linkBreakSignal, mac);
                    delete mac;

                    // if something in the queue, wakeup soon.
                    if (macQueue.size() > 0)
                        scheduleAt(simTime() + dblrand() * checkInterval, wakeup);
                    else
                        scheduleAt(simTime() + slotDuration, wakeup);
                    macState = SLEEP;
                    radio->setRadioMode(IRadio::RADIO_MODE_SLEEP);
                    nbMissedAcks++;
                }
                return;
            }
            //ignore and other packets
            if ((msg->getKind() == BMAC_DATA) || (msg->getKind() == BMAC_PREAMBLE)) {
                EV_DETAIL << "State WAIT_ACK, message BMAC_DATA or BMAC_PREMABLE, new"
                             " state WAIT_ACK" << endl;
                delete msg;
                return;
            }
            if (msg->getKind() == BMAC_ACK) {
                EV_DETAIL << "State WAIT_ACK, message BMAC_ACK" << endl;
                auto mac = check_and_cast<Packet *>(msg);
                const MACAddress src = mac->peekHeader<BMacHeader>()->getSrcAddr();
                // the right ACK is received..
                EV_DETAIL << "We are waiting for ACK from : " << lastDataPktDestAddr
                          << ", and ACK came from : " << src << endl;
                if (src == lastDataPktDestAddr) {
                    EV_DETAIL << "New state SLEEP" << endl;
                    nbRecvdAcks++;
                    lastDataPktDestAddr = MACAddress::BROADCAST_ADDRESS;
                    cancelEvent(ack_timeout);
                    delete macQueue.front();
                    macQueue.pop_front();
                    // if something in the queue, wakeup soon.
                    if (macQueue.size() > 0)
                        scheduleAt(simTime() + dblrand() * checkInterval, wakeup);
                    else
                        scheduleAt(simTime() + slotDuration, wakeup);
                    macState = SLEEP;
                    radio->setRadioMode(IRadio::RADIO_MODE_SLEEP);
                    lastDataPktDestAddr = MACAddress::BROADCAST_ADDRESS;
                }
                delete msg;
                return;
            }
            break;

        case WAIT_DATA:
            if (msg->getKind() == BMAC_PREAMBLE) {
                //nothing happens
                EV_DETAIL << "State WAIT_DATA, message BMAC_PREAMBLE, new state"
                             " WAIT_DATA" << endl;
                nbRxPreambles++;
                delete msg;
                return;
            }
            if (msg->getKind() == BMAC_ACK) {
                //nothing happens
                EV_DETAIL << "State WAIT_DATA, message BMAC_ACK, new state WAIT_DATA"
                          << endl;
                delete msg;
                return;
            }
            if (msg->getKind() == BMAC_DATA) {
                nbRxDataPackets++;
                auto mac = check_and_cast<Packet *>(msg);
                const auto bmacHeader = mac->peekHeader<BMacHeader>();
                const MACAddress& dest = bmacHeader->getDestAddr();
                const MACAddress& src = bmacHeader->getSrcAddr();
                if ((dest == address) || dest.isBroadcast()) {
                    EV_DETAIL << "Local delivery " << mac << endl;
                    decapsulate(mac);
                    sendUp(mac);
                }
                else {
                    EV_DETAIL << "Received " << mac << " is not for us, dropping frame." << endl;
                    PacketDropDetails details;
                    details.setReason(NOT_ADDRESSED_TO_US);
                    emit(packetDropSignal, msg, &details);
                    delete msg;
                    msg = nullptr;
                    mac = nullptr;
                }

                cancelEvent(data_timeout);
                if ((useMacAcks) && (dest == address)) {
                    EV_DETAIL << "State WAIT_DATA, message BMAC_DATA, new state"
                                 " SEND_ACK" << endl;
                    macState = SEND_ACK;
                    lastDataPktSrcAddr = src;
                    radio->setRadioMode(IRadio::RADIO_MODE_TRANSMITTER);
                }
                else {
                    EV_DETAIL << "State WAIT_DATA, message BMAC_DATA, new state SLEEP"
                              << endl;
                    // if something in the queue, wakeup soon.
                    if (macQueue.size() > 0)
                        scheduleAt(simTime() + dblrand() * checkInterval, wakeup);
                    else
                        scheduleAt(simTime() + slotDuration, wakeup);
                    macState = SLEEP;
                    radio->setRadioMode(IRadio::RADIO_MODE_SLEEP);
                }
                return;
            }
            if (msg->getKind() == BMAC_DATA_TIMEOUT) {
                EV_DETAIL << "State WAIT_DATA, message BMAC_DATA_TIMEOUT, new state"
                             " SLEEP" << endl;
                // if something in the queue, wakeup soon.
                if (macQueue.size() > 0)
                    scheduleAt(simTime() + dblrand() * checkInterval, wakeup);
                else
                    scheduleAt(simTime() + slotDuration, wakeup);
                macState = SLEEP;
                radio->setRadioMode(IRadio::RADIO_MODE_SLEEP);
                return;
            }
            break;

        case SEND_ACK:
            if (msg->getKind() == BMAC_SEND_ACK) {
                EV_DETAIL << "State SEND_ACK, message BMAC_SEND_ACK, new state"
                             " WAIT_ACK_TX" << endl;
                // send now the ack packet
                sendMacAck();
                macState = WAIT_ACK_TX;
                return;
            }
            break;

        case WAIT_ACK_TX:
            if (msg->getKind() == BMAC_ACK_TX_OVER) {
                EV_DETAIL << "State WAIT_ACK_TX, message BMAC_ACK_TX_OVER, new state"
                             " SLEEP" << endl;
                // ack sent, go to sleep now.
                // if something in the queue, wakeup soon.
                if (macQueue.size() > 0)
                    scheduleAt(simTime() + dblrand() * checkInterval, wakeup);
                else
                    scheduleAt(simTime() + slotDuration, wakeup);
                macState = SLEEP;
                radio->setRadioMode(IRadio::RADIO_MODE_SLEEP);
                lastDataPktSrcAddr = MACAddress::BROADCAST_ADDRESS;
                return;
            }
            break;
    }
    throw cRuntimeError("Undefined event of type %d in state %d (radio mode %d, radio reception state %d, radio transmission state %d)!",
            msg->getKind(), macState, radio->getRadioMode(), radio->getReceptionState(), radio->getTransmissionState());
}

/**
 * Handle BMAC preambles and received data packets.
 */
void BMacLayer::handleLowerPacket(Packet *packet)
{
    if (packet->hasBitError()) {
        EV << "Received " << packet << " contains bit errors or collision, dropping it\n";
        PacketDropDetails details;
        details.setReason(INCORRECTLY_RECEIVED);
        emit(packetDropSignal, packet, &details);
        delete packet;
        return;
    }
    else
        // simply pass the massage as self message, to be processed by the FSM.
        handleSelfMessage(packet);
}

void BMacLayer::sendDataPacket()
{
    nbTxDataPackets++;
    Packet *pkt = macQueue.front()->dup();
    attachSignal(pkt);
    lastDataPktDestAddr = pkt->peekHeader<BMacHeader>()->getDestAddr();
    pkt->setKind(BMAC_DATA);
    sendDown(pkt);
}

void BMacLayer::receiveSignal(cComponent *source, simsignal_t signalID, long value, cObject *details)
{
    Enter_Method_Silent();
    if (signalID == IRadio::radioModeChangedSignal) {
        IRadio::RadioMode radioMode = (IRadio::RadioMode)value;
        if (radioMode == IRadio::RADIO_MODE_TRANSMITTER) {
            // we just switched to TX after CCA, so simply send the first
            // sendPremable self message
            if (macState == SEND_PREAMBLE)
                scheduleAt(simTime(), send_preamble);
            else if (macState == SEND_ACK)
                scheduleAt(simTime(), send_ack);
            // we were waiting for acks, but none came. we switched to TX and now
            // need to resend data
            else if (macState == SEND_DATA)
                scheduleAt(simTime(), resend_data);
        }
    }
    // Transmission of one packet is over
    else if (signalID == IRadio::transmissionStateChangedSignal) {
        IRadio::TransmissionState newRadioTransmissionState = (IRadio::TransmissionState)value;
        if (transmissionState == IRadio::TRANSMISSION_STATE_TRANSMITTING && newRadioTransmissionState == IRadio::TRANSMISSION_STATE_IDLE) {
            if (macState == WAIT_TX_DATA_OVER)
                scheduleAt(simTime(), data_tx_over);
            else if (macState == WAIT_ACK_TX)
                scheduleAt(simTime(), ack_tx_over);
        }
        transmissionState = newRadioTransmissionState;
    }
}

/**
 * Encapsulates the received network-layer packet into a BMacHeader and set all
 * needed header fields.
 */
bool BMacLayer::addToQueue(cMessage *msg)
{
    if (macQueue.size() >= queueLength) {
        // queue is full, message has to be deleted
        EV_DETAIL << "New packet arrived, but queue is FULL, so new packet is"
                     " deleted\n";
        PacketDropDetails details;
        details.setReason(QUEUE_OVERFLOW);
        details.setLimit(queueLength);
        emit(packetDropSignal, msg, &details);
        nbDroppedDataPackets++;
        return false;
    }

    auto packet = check_and_cast<Packet *>(msg);
    encapsulate(packet);
    macQueue.push_back(packet);
    EV_DETAIL << "Max queue length: " << queueLength << ", packet put in queue"
                                                        "\n  queue size: " << macQueue.size() << " macState: "
              << macState << endl;
    return true;
}

void BMacLayer::flushQueue()
{
    // TODO:
    macQueue.clear();
}

void BMacLayer::clearQueue()
{
    macQueue.clear();
}

void BMacLayer::attachSignal(Packet *macPkt)
{
    //calc signal duration
    simtime_t duration = macPkt->getBitLength() / bitrate;
    //create and initialize control info with new signal
    macPkt->setDuration(duration);
}

/**
 * Change the color of the node for animation purposes.
 */
void BMacLayer::refreshDisplay() const
{
    if (!animation)
        return;
    cDisplayString& dispStr = findContainingNode(this)->getDisplayString();

    switch (macState) {
        case INIT:
        case SLEEP:
            dispStr.setTagArg("b", 3, "black");
            break;

        case CCA:
            dispStr.setTagArg("b", 3, "green");
            break;

        case SEND_ACK:
        case SEND_PREAMBLE:
        case SEND_DATA:
            dispStr.setTagArg("b", 3, "blue");
            break;

        case WAIT_ACK:
        case WAIT_DATA:
        case WAIT_TX_DATA_OVER:
        case WAIT_ACK_TX:
            dispStr.setTagArg("b", 3, "yellow");
            break;

        default:
            dispStr.setTagArg("b", 3, "");
            break;
    }
}

/*void BMacLayer::changeMacState(States newState)
   {
    switch (macState)
    {
    case RX:
        timeRX += (simTime() - lastTime);
        break;
    case TX:
        timeTX += (simTime() - lastTime);
        break;
    case SLEEP:
        timeSleep += (simTime() - lastTime);
        break;
    case CCA:
        timeRX += (simTime() - lastTime);
    }
    lastTime = simTime();

    switch (newState)
    {
    case CCA:
        changeDisplayColor(GREEN);
        break;
    case TX:
        changeDisplayColor(BLUE);
        break;
    case SLEEP:
        changeDisplayColor(BLACK);
        break;
    case RX:
        changeDisplayColor(YELLOW);
        break;
    }

    macState = newState;
   }*/

void BMacLayer::decapsulate(Packet *packet)
{
    const auto& bmacHeader = packet->popHeader<BMacHeader>();
    packet->ensureTag<MacAddressInd>()->setSrcAddress(bmacHeader->getSrcAddr());
    packet->ensureTag<InterfaceInd>()->setInterfaceId(interfaceEntry->getInterfaceId());
    auto protocol = ProtocolGroup::ethertype.getProtocol(bmacHeader->getNetworkProtocol());
    packet->ensureTag<DispatchProtocolReq>()->setProtocol(protocol);
    packet->ensureTag<PacketProtocolTag>()->setProtocol(protocol);
    EV_DETAIL << " message decapsulated " << endl;
}

void BMacLayer::encapsulate(Packet *packet)
{
    auto pkt = makeShared<BMacHeader>();
    pkt->setChunkLength(b(headerLength));

    // copy dest address from the Control Info attached to the network
    // message by the network layer
    auto dest = packet->getMandatoryTag<MacAddressReq>()->getDestAddress();
    EV_DETAIL << "CInfo removed, mac addr=" << dest << endl;
    pkt->setNetworkProtocol(ProtocolGroup::ethertype.getProtocolNumber(packet->getMandatoryTag<PacketProtocolTag>()->getProtocol()));
    pkt->setDestAddr(dest);

    //delete the control info
    delete packet->removeControlInfo();

    //set the src address to own mac address (nic module getId())
    pkt->setSrcAddr(address);

    //encapsulate the network packet
    pkt->markImmutable();
    packet->pushHeader(pkt);
    EV_DETAIL << "pkt encapsulated\n";
}

} // namespace inet

