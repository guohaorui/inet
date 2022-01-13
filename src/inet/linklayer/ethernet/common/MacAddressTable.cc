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

#include "inet/linklayer/ethernet/common/MacAddressTable.h"

#include <map>

#include "inet/common/ModuleAccess.h"
#include "inet/common/StringFormat.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/networklayer/contract/IInterfaceTable.h"

namespace inet {

#define MAX_LINE    100

Define_Module(MacAddressTable);

std::ostream& operator<<(std::ostream& os, const std::vector<int>& ids)
{
    os << "[";
    for (int i = 0; i < ids.size(); i++) {
        auto id = ids[i];
        if (i != 0)
            os << ", ";
        os << id;
    }
    return os << "]";
}

std::ostream& operator<<(std::ostream& os, const MacAddressTable::AddressEntry& entry)
{
    os << "{VID=" << entry.vid << ", interfaceIds=" << entry.interfaceIds << ", insertionTime=" << entry.insertionTime << "}";
    return os;
}

std::ostream& operator<<(std::ostream& os, const MacAddressTable::AddressTableKey& key)
{
    os << "{VID=" << key.first << ", addr=" << key.second << "}";
    return os;
}

MacAddressTable::MacAddressTable()
{
}

void MacAddressTable::initialize(int stage)
{
    OperationalBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        agingTime = par("agingTime");
        lastPurge = SIMTIME_ZERO;
        ifTable.reference(this, "interfaceTableModule", true);
        WATCH_MAP(addressTable);
    }
}

void MacAddressTable::handleParameterChange(const char *name)
{
    if (name != nullptr) {
        if (!strcmp(name, "addressTable")) {
            clearTable();
            parseAddressTableParameter();
        }
    }
}

/**
 * Function reads from a file stream pointed to by 'fp' and stores characters
 * until the '\n' or EOF character is found, the resultant string is returned.
 * Note that neither '\n' nor EOF character is stored to the resultant string,
 * also note that if on a line containing useful data that EOF occurs, then
 * that line will not be read in, hence must terminate file with unused line.
 */
static char *fgetline(FILE *fp)
{
    // alloc buffer and read a line
    char *line = new char[MAX_LINE];
    if (fgets(line, MAX_LINE, fp) == nullptr) {
        delete[] line;
        return nullptr;
    }

    // chop CR/LF
    line[MAX_LINE - 1] = '\0';
    int len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';

    return line;
}

void MacAddressTable::handleMessage(cMessage *)
{
    throw cRuntimeError("This module doesn't process messages");
}

void MacAddressTable::handleMessageWhenUp(cMessage *)
{
    throw cRuntimeError("This module doesn't process messages");
}

void MacAddressTable::refreshDisplay() const
{
    updateDisplayString();
}

void MacAddressTable::updateDisplayString() const
{
    if (getEnvir()->isGUI()) {
        auto text = StringFormat::formatString(par("displayStringTextFormat"), this);
        getDisplayString().setTagArg("t", 0, text);
    }
}

const char *MacAddressTable::resolveDirective(char directive) const
{
    static std::string result;
    switch (directive) {
        case 'a':
            result = std::to_string(addressTable.size());
            break;
        case 'v':
            break;
        default:
            throw cRuntimeError("Unknown directive: %c", directive);
    }
    return result.c_str();
}

/*
 * getTableForVid
 * Returns a MAC Address Table for a specified VLAN ID
 * or nullptr pointer if it is not found
 */

int MacAddressTable::getInterfaceIdForAddress(const MacAddress& address, unsigned int vid)
{
    Enter_Method("getInterfaceIdForAddress");

    AddressTableKey key(vid, address);
    auto iter = addressTable.find(key);

    if (iter == addressTable.end()) {
        // not found
        return -1;
    }
    if (iter->second.insertionTime + agingTime <= simTime()) {
        // don't use (and throw out) aged entries
        EV << "Ignoring and deleting aged entry: " << iter->first << " --> interfaceIds " << iter->second.interfaceIds << "\n";
        addressTable.erase(iter);
        return -1;
    }
    if (iter->second.interfaceIds.size() != 1)
        throw cRuntimeError("Invalid number of interfaces");
    return iter->second.interfaceIds[0];
}

std::vector<int> MacAddressTable::getInterfaceIdsForAddress(const MacAddress& address, unsigned int vid)
{
    Enter_Method("getInterfaceIdsForAddress");

    AddressTableKey key(vid, address);
    auto iter = addressTable.find(key);

    if (iter == addressTable.end()) {
        // not found
        return std::vector<int>();
    }
    if (iter->second.insertionTime + agingTime <= simTime()) {
        // don't use (and throw out) aged entries
        EV << "Ignoring and deleting aged entry: " << iter->first << " --> interfaceIds " << iter->second.interfaceIds << "\n";
        addressTable.erase(iter);
        return std::vector<int>();
    }
    return iter->second.interfaceIds;
}

/*
 * Register a new MAC address at addressTable.
 * True if refreshed. False if it is new.
 */

bool MacAddressTable::learnMacAddress(int interfaceId, const MacAddress& address, unsigned int vid)
{
    Enter_Method("learnMacAddress");
    if (address.isBroadcast() || address.isMulticast())
        return false;
    else
        return updateMacAddressTable(interfaceId, address, vid);
}

bool MacAddressTable::updateMacAddressTable(int interfaceId, const MacAddress& address, unsigned int vid)
{
    AddressTableKey key(vid, address);
    auto iter = addressTable.find(key);

    if (iter == addressTable.end()) {
        removeAgedEntriesIfNeeded();

        // Add entry to table
        EV << "Adding entry" << EV_FIELD(address) << EV_FIELD(interfaceId) << EV_FIELD(vid) << EV_ENDL;
        addressTable[key] = AddressEntry(vid, {interfaceId}, simTime());
        return false;
    }
    else {
        // Update existing entry
        EV << "Updating entry" << EV_FIELD(address) << EV_FIELD(interfaceId) << EV_FIELD(vid) << EV_ENDL;
        AddressEntry& entry = iter->second;
        entry.insertionTime = simTime();
        for (auto id : entry.interfaceIds)
            if (id == interfaceId)
                return true;
        entry.interfaceIds.push_back(interfaceId);
    }
    return true;
}

/*
 * Clears interfaceId MAC cache.
 */

void MacAddressTable::flush(int interfaceId)
{
    Enter_Method("flush");
    for (auto cur = addressTable.begin(); cur != addressTable.end();) {
        auto& interfaceIds = cur->second.interfaceIds;
        interfaceIds.erase(std::remove(interfaceIds.begin(), interfaceIds.end(), interfaceId), interfaceIds.end());
        if (interfaceIds.empty())
            cur = addressTable.erase(cur);
        else
            ++cur;
    }
}

/*
 * Prints verbose information
 */

void MacAddressTable::printState()
{
    EV << endl << "MAC Address Table" << endl;
    EV << "VLAN ID    MAC    IfIds    Inserted" << endl;
    for (auto& elem : addressTable)
        EV << elem.first.first << "   " << elem.first.second << "   " << elem.second.interfaceIds << "   " << elem.second.insertionTime << endl;
}

//TODO rename it (replace interfaceIdA to interfaceIdB in table)
void MacAddressTable::copyTable(int interfaceIdA, int interfaceIdB)
{
    for (auto& elem : addressTable) {
        for (auto& interfaceId : elem.second.interfaceIds)
        if (interfaceId == interfaceIdA)
            interfaceId = interfaceIdB;
    }
}

void MacAddressTable::removeAgedEntriesFromAllVlans()
{
    for (auto cur = addressTable.begin(); cur != addressTable.end();) {
        AddressEntry& entry = cur->second;
        if (entry.insertionTime + agingTime <= simTime()) {
            EV << "Removing aged entry from Address Table: "
               << cur->first.first << " " << cur->first.second << " --> interfaceIds " << cur->second.interfaceIds << "\n";
            cur = addressTable.erase(cur);
        }
        else
            ++cur;
    }
}

void MacAddressTable::removeAgedEntriesIfNeeded()
{
    simtime_t now = simTime();

    if (now >= lastPurge + SimTime(1, SIMTIME_S))
        removeAgedEntriesFromAllVlans();

    lastPurge = simTime();
}

void MacAddressTable::readAddressTable(const char *fileName)
{
    FILE *fp = fopen(fileName, "r");
    if (fp == nullptr)
        throw cRuntimeError("cannot open address table file `%s'", fileName);

    // parse address table file:
    char *line;
    for (int lineno = 0; (line = fgetline(fp)) != nullptr; delete[] line) {
        lineno++;

        // lines beginning with '#' are treated as comments
        if (line[0] == '#')
            continue;

        // scan in VLAN ID
        char *vlanIdStr = strtok(line, " \t");
        // scan in MAC address
        char *macAddressStr = strtok(nullptr, " \t");
        // scan in interface name
        char *interfaceName = strtok(nullptr, " \t");

        char *endptr = nullptr;

        // empty line or comment?
        if (!vlanIdStr || *vlanIdStr == '#')
            continue;

        // broken line?
        if (!vlanIdStr || !macAddressStr || !interfaceName)
            throw cRuntimeError("line %d invalid in address table file `%s'", lineno, fileName);

        // parse columns:

        // parse VLAN ID:
        unsigned int vlanId = strtol(vlanIdStr, &endptr, 10);
        if (!endptr || *endptr)
            throw cRuntimeError("error in line %d in address table file `%s': VLAN ID '%s' unresolved", lineno, fileName, vlanIdStr);

        // parse MAC address:
        L3Address addr;
        if (!L3AddressResolver().tryResolve(macAddressStr, addr, L3AddressResolver::ADDR_MAC))
            throw cRuntimeError("error in line %d in address table file `%s': MAC address '%s' unresolved", lineno, fileName, macAddressStr);
        MacAddress macAddress = addr.toMac();

        // parse interface:
        int interfaceId = -1;
        auto ie = ifTable->findInterfaceByName(interfaceName);
        if (ie == nullptr) {
            long int num = strtol(interfaceName, &endptr, 10);
            if (endptr && *endptr == '\0')
                ie = ifTable->findInterfaceById(num);
        }
        if (ie == nullptr)
            throw cRuntimeError("error in line %d in address table file `%s': interface '%s' not found", lineno, fileName, interfaceName);
        interfaceId = ie->getInterfaceId();

        // Create an entry with address and interfaceId and insert into table
        AddressEntry entry(vlanId, {interfaceId}, 0);
        AddressTableKey key(vlanId, macAddress);
        addressTable[key] = entry;
    }
    fclose(fp);
}

void MacAddressTable::parseAddressTableParameter()
{
    auto addressTable = check_and_cast<cValueArray *>(par("addressTable").objectValue());
    for (int i = 0; i < addressTable->size(); i++) {
        cValueMap *entry = check_and_cast<cValueMap *>(addressTable->get(i).objectValue());
        auto vlan = entry->containsKey("vlan") ? entry->get("vlan").intValue() : 0;
        auto macAddressString = entry->get("address").stringValue();
        L3Address l3Address;
        if (!L3AddressResolver().tryResolve(macAddressString, l3Address, L3AddressResolver::ADDR_MAC))
            throw cRuntimeError("Cannot resolve MAC address of '%s'", macAddressString);
        MacAddress macAddress = l3Address.toMac();
        auto interfaceName = entry->get("interface").stringValue();
        auto networkInterface = ifTable->findInterfaceByName(interfaceName);
        if (networkInterface == nullptr)
            throw cRuntimeError("Cannot find network interface '%s'", interfaceName);
        updateMacAddressTable(networkInterface->getInterfaceId(), macAddress, vlan);
    }
}

void MacAddressTable::initializeTable()
{
    clearTable();
    parseAddressTableParameter();

    // Option to pre-read in Address Table. To turn it off, set addressTableFile to empty string
    const char *addressTableFile = par("addressTableFile");
    if (addressTableFile && *addressTableFile)
        readAddressTable(addressTableFile);
}

void MacAddressTable::clearTable()
{
    addressTable.clear();
}

MacAddressTable::~MacAddressTable()
{
}

void MacAddressTable::setAgingTime(simtime_t agingTime)
{
    this->agingTime = agingTime;
}

void MacAddressTable::resetDefaultAging()
{
    agingTime = par("agingTime");
}

} // namespace inet

