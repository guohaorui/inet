//
// Copyright (C) 2013 OpenSim Ltd.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//

#ifndef __INET_SIGNALPACKETMODEL_H
#define __INET_SIGNALPACKETMODEL_H

#include "inet/physicallayer/contract/bitlevel/ISignalPacketModel.h"

namespace inet {
namespace physicallayer {

class INET_API SignalPacketModel : public virtual ISignalPacketModel
{
  protected:
    const Packet *packet;
    const bps bitrate;

  public:
    SignalPacketModel(const Packet *packet, bps bitrate);

    virtual std::ostream& printToStream(std::ostream& stream, int level, int evFlags = 0) const override;
    virtual const Packet *getPacket() const override { return packet; }
    virtual bps getBitrate() const override { return bitrate; }
};

class INET_API TransmissionPacketModel : public SignalPacketModel, public virtual ITransmissionPacketModel
{
  public:
    TransmissionPacketModel(const Packet *packet, bps bitrate);
};

class INET_API ReceptionPacketModel : public SignalPacketModel, public IReceptionPacketModel
{
  protected:
    double packetErrorRate;

  public:
    ReceptionPacketModel(const Packet *packet, bps bitrate, double packetErrorRate);

    virtual double getPacketErrorRate() const override { return packetErrorRate; }
};

} // namespace physicallayer
} // namespace inet

#endif

