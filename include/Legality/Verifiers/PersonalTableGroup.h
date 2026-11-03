/**
 * The personal table PKHeX's entity class reads for a format (PKM.PersonalInfo): always the last game
 * of the format's generation, never the origin game's. A Black/White Pokemon is judged by Black
 * 2/White 2's abilities and forms, a Sun/Moon one by Ultra Sun/Ultra Moon's.
 */
#ifndef LEGALITY_VERIFIERS_PERSONALTABLEGROUP_H
#define LEGALITY_VERIFIERS_PERSONALTABLEGROUP_H

#include "Enums/GameVersion.h"

namespace Legality
{
    /// PKHeX PK3 RS, PK4 HG/SS, PK5 B2/W2, PK6 OR/AS, PK7 US/UM; every later format reads its own.
    inline Enums::GameVersion getPersonalTableGroup(Enums::GameVersion formatGroup)
    {
        switch (formatGroup)
        {
        case Enums::GameVersion::FRLG:
            return Enums::GameVersion::RSE;
        case Enums::GameVersion::DP:
        case Enums::GameVersion::PT:
            return Enums::GameVersion::HGSS;
        case Enums::GameVersion::BW:
            return Enums::GameVersion::B2W2;
        case Enums::GameVersion::XY:
            return Enums::GameVersion::ORAS;
        case Enums::GameVersion::SM:
            return Enums::GameVersion::USUM;
        default:
            return formatGroup;
        }
    }
}

#endif
