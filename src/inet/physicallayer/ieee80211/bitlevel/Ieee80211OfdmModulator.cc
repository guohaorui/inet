//
// Copyright (C) 2014 OpenSim Ltd.
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

#include "inet/physicallayer/ieee80211/bitlevel/Ieee80211OfdmDefs.h"
#include "inet/physicallayer/ieee80211/bitlevel/Ieee80211OfdmModulator.h"
#include "inet/physicallayer/ieee80211/bitlevel/Ieee80211OfdmSymbolModel.h"
#include "inet/physicallayer/modulation/BpskModulation.h"
#include "inet/physicallayer/modulation/Qam16Modulation.h"
#include "inet/physicallayer/modulation/Qam64Modulation.h"
#include "inet/physicallayer/modulation/QpskModulation.h"

namespace inet {
namespace physicallayer {

const ApskSymbol Ieee80211OfdmModulator::positivePilotSubcarrier(1,0);
const ApskSymbol Ieee80211OfdmModulator::negativePilotSubcarrier(-1,0);

// The sequence p_n is generated by the scrambler defined by Figure 18-7 when the all ones initial state is used,
// and by replacing all 1s with -1 and all 0s with 1. Each sequence element is used for one OFDM symbol. The
// first element, p_0, multiplies the pilot subcarriers of the SIGNAL symbol, while the elements from p_1 on are
// used for the DATA symbols.
const double Ieee80211OfdmModulator::pilotSubcarrierPolarityVector[] = {
    1, 1, 1, 1, -1, -1, -1, 1, -1, -1, -1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, 1, -1, 1, 1, 1, 1, 1, 1, -1, 1,
    1, 1, -1, 1, 1, -1, -1, 1, 1, 1, -1, 1, -1, -1, -1, 1, -1, 1, -1, -1, 1, -1, -1, 1, 1, 1, 1, 1, -1, -1, 1, 1,
    -1, -1, 1, -1, 1, -1, 1, 1, -1, -1, -1, 1, 1, -1, -1, -1, -1, 1, -1, -1, 1, -1, 1, 1, 1, 1, -1, 1, -1, 1, -1, 1,
    -1, -1, -1, -1, -1, 1, -1, 1, 1, -1, 1, -1, 1, 1, 1, -1, -1, 1, -1, -1, -1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1
};

Ieee80211OfdmModulator::Ieee80211OfdmModulator(const Ieee80211OfdmModulation *subcarrierModulation, unsigned int polarityVectorOffset) :
    subcarrierModulation(subcarrierModulation),
    pilotSubcarrierPolarityVectorOffset(polarityVectorOffset)
{
}

std::ostream& Ieee80211OfdmModulator::printToStream(std::ostream& stream, int level, int evFlags) const
{
    stream << "Ieee80211OfdmModulator";
    if (level <= PRINT_LEVEL_TRACE)
        stream << EV_FIELD(subcarrierModulation, printFieldToString(subcarrierModulation, level + 1, evFlags));
    return stream;
}

int Ieee80211OfdmModulator::getSubcarrierIndex(int ofdmSymbolIndex) const
{
    // This is the translated version of the M(k) function defined in 18.3.5.10 OFDM modulation: (18-23) equation.
    // We translate it by 26 since its range is [-26,26] and we use subcarrier indices as array indices.
    if (ofdmSymbolIndex >= 0 && ofdmSymbolIndex <= 4)
        return ofdmSymbolIndex;
    else if (ofdmSymbolIndex >= 5 && ofdmSymbolIndex <= 17)
        return ofdmSymbolIndex + 1;
    else if (ofdmSymbolIndex >= 18 && ofdmSymbolIndex <= 23)
        return ofdmSymbolIndex + 2;
    else if (ofdmSymbolIndex >= 24 && ofdmSymbolIndex <= 29)
        return ofdmSymbolIndex + 3;
    else if (ofdmSymbolIndex >= 30 && ofdmSymbolIndex <= 42)
        return ofdmSymbolIndex + 4;
    else if (ofdmSymbolIndex >= 43 && ofdmSymbolIndex <= 47)
        return ofdmSymbolIndex + 5;
    else
        throw cRuntimeError("The domain of the M(k) (k = %d) function is [0,47]", ofdmSymbolIndex);
}

void Ieee80211OfdmModulator::insertPilotSubcarriers(Ieee80211OfdmSymbol *ofdmSymbol, int symbolID) const
{
    ofdmSymbol->pushApskSymbol(pilotSubcarrierPolarityVector[symbolID % 127] == 1 ? &positivePilotSubcarrier : &negativePilotSubcarrier, 5);
    ofdmSymbol->pushApskSymbol(pilotSubcarrierPolarityVector[symbolID % 127] == 1 ? &positivePilotSubcarrier : &negativePilotSubcarrier, 19);
    ofdmSymbol->pushApskSymbol(pilotSubcarrierPolarityVector[symbolID % 127] == 1 ? &positivePilotSubcarrier : &negativePilotSubcarrier, 33);
    ofdmSymbol->pushApskSymbol(pilotSubcarrierPolarityVector[symbolID % 127] == 1 ? &negativePilotSubcarrier : &positivePilotSubcarrier, 47);
}

const ITransmissionSymbolModel *Ieee80211OfdmModulator::modulate(const ITransmissionBitModel *bitModel) const
{
    std::vector<const ISymbol *> *ofdmSymbols = new std::vector<const ISymbol *>();
    const ApskModulationBase *modulationScheme = subcarrierModulation->getSubcarrierModulation();
    const BitVector *bits = bitModel->getBits();
    // Divide the resulting coded and interleaved data string into groups of N_BPSC bits.
    unsigned int nBPSC = modulationScheme->getCodeWordSize();
    ShortBitVector bitGroup;
    std::vector<const ApskSymbol *> apskSymbols;
    for (unsigned int i = 0; i < bits->getSize(); i++) {
        // For each of the bit groups, convert the bit group into a complex number according
        // to the modulation encoding tables
        bitGroup.setBit(i % nBPSC, bits->getBit(i));
        if (i % nBPSC == nBPSC - 1) {
            const ApskSymbol *apskSymbol = modulationScheme->mapToConstellationDiagram(bitGroup);
            apskSymbols.push_back(apskSymbol);
        }
    }
    // Divide the complex number string into groups of 48 complex numbers.
    // Each such group is associated with one OFDM symbol.
    for (unsigned int i = 0; i < apskSymbols.size(); i += NUMBER_OF_OFDM_DATA_SUBCARRIERS) {
        Ieee80211OfdmSymbol *ofdmSymbol = new Ieee80211OfdmSymbol();
        // In each group, the complex numbers are numbered 0 to 47 and mapped hereafter into OFDM
        // subcarriers numbered -26 to -22, -20 to -8, -6 to -1, 1 to 6, 8 to 20, and 22 to 26.
        // The 0 subcarrier, associated with center frequency, is omitted and filled with the value 0.
        for (unsigned int j = 0; j < NUMBER_OF_OFDM_DATA_SUBCARRIERS; j++) {
            int subcarrierIndex = getSubcarrierIndex(j);
            ofdmSymbol->pushApskSymbol(apskSymbols.at(j + i), subcarrierIndex);
        }
        insertPilotSubcarriers(ofdmSymbol, i / NUMBER_OF_OFDM_DATA_SUBCARRIERS + pilotSubcarrierPolarityVectorOffset);
        EV_DEBUG << "Modulated OFDM symbol: " << *ofdmSymbol << endl;
        ofdmSymbols->push_back(ofdmSymbol);
    }
    return new Ieee80211OfdmTransmissionSymbolModel(1, NaN, ofdmSymbols->size() - 1, NaN, ofdmSymbols, modulationScheme, modulationScheme);
}

} // namespace physicallayer
} // namespace inet

