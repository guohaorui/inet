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

#ifndef __INET_ISOTROPICDIMENSIONALBACKGROUNDNOISE_H
#define __INET_ISOTROPICDIMENSIONALBACKGROUNDNOISE_H

#include "inet/physicallayer/contract/packetlevel/IBackgroundNoise.h"

namespace inet {

namespace physicallayer {

class INET_API IsotropicDimensionalBackgroundNoise : public cModule, public IBackgroundNoise
{
  protected:
    WpHz powerSpectralDensity = WpHz(NaN);
    W power = W(NaN);
    mutable Hz bandwidth = Hz(NaN);

  protected:
    virtual void initialize(int stage) override;

  public:
    virtual std::ostream& printToStream(std::ostream& stream, int level, int evFlags = 0) const override;
    virtual const INoise *computeNoise(const IListening *listening) const override;
};

} // namespace physicallayer

} // namespace inet

#endif

