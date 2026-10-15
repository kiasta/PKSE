#include "Trainer/TradeDonor.h"

#include <string>

#include "Pokemon/Pokemon.h"
#include "Trainer/Trainer.h"
#include "Utils/StringHelpers.h"

namespace Trainer
{

    using Enums::GameVersion;

    bool gamesCanTrade(GameVersion donorGroup, GameVersion recordGroup) noexcept
    {
        // Legends: Arceus has no trading feature, in either direction.
        if (donorGroup == GameVersion::PLA || recordGroup == GameVersion::PLA)
        {
            return false;
        }
        if (donorGroup == recordGroup)
        {
            return true;
        }
        // The two places PKSE's per-save-format split is finer than the games' link cable.
        const bool bothGen6 = (donorGroup == GameVersion::XY || donorGroup == GameVersion::ORAS) && (recordGroup == GameVersion::XY || recordGroup == GameVersion::ORAS);
        const bool bothGen7 = (donorGroup == GameVersion::SM || donorGroup == GameVersion::USUM) && (recordGroup == GameVersion::SM || recordGroup == GameVersion::USUM);
        return bothGen6 || bothGen7;
    }

    bool titleCanTradeWith(GameVersion titleVersion, const ::Pokemon::Pokemon &pokemon)
    {
        if (!pokemon.hasHandler())
        {
            return false;
        }
        return gamesCanTrade(Enums::getGameGroup(titleVersion), pokemon.getGameGroup());
    }

    TradeDonor buildTradeDonor(const Trainer &donor, const ::Pokemon::Pokemon &pokemon)
    {
        TradeDonor result;

        // Before Gen 6 a trade writes nothing at all, so there is no donor to want.
        if (!pokemon.hasHandler())
        {
            result.refusal = TradeDonorRefusal::NoHandler;
            return result;
        }
        if (!gamesCanTrade(donor.getGameGroup(), pokemon.getGameGroup()))
        {
            result.refusal = TradeDonorRefusal::CannotTrade;
            return result;
        }

        const std::u16string donorName = Utils::utf8ToUtf16(donor.trainerName);

        // A TRADE TO YOURSELF IS NOT A TRADE. PKHeX decides whether a trainer is the OT on name,
        // full 32-bit id and gender together (PKM.BelongsTo), and a handler matching all three
        // would claim the player traded with themselves -- which the games cannot do and the
        // handler checks flag.
        const bool sameName = (donorName == pokemon.otName());
        const bool sameIdentity = sameName && donor.ID32 == pokemon.id32() && donor.trainerGender == pokemon.otGender();
        if (sameIdentity)
        {
            result.refusal = TradeDonorRefusal::SameTrainer;
            return result;
        }

        // Every Gen 6+ handler field is 12 characters, so this only bites for a donor from a format
        // with a longer name than the destination can hold. Refused rather than truncated: half a
        // trainer's name is not their name, and it would read as a different person.
        if (donorName.empty() || static_cast<int>(donorName.size()) > pokemon.getMaxNicknameLength())
        {
            result.refusal = TradeDonorRefusal::NameUnstorable;
            return result;
        }

        result.partner.trainerName = donorName;
        result.partner.gender = donor.trainerGender;
        result.partner.language = donor.language();
        // consoleCountry / consoleRegion stay 0 -- see the header. An empty geolocation history is
        // legal; an unverifiable country/region pair is not.
        return result;
    }

    const char *tradeDonorRefusalText(TradeDonorRefusal refusal) noexcept
    {
        switch (refusal)
        {
        case TradeDonorRefusal::None:
            return "";
        // Short enough to sit in the details row that shows them -- that column is the only place
        // a refusal is displayed, and a sentence there would run into the label beside it.
        case TradeDonorRefusal::NoHandler:
            return "No handler field";
        case TradeDonorRefusal::CannotTrade:
            return "Game cannot trade";
        case TradeDonorRefusal::SameTrainer:
            return "That is the OT";
        case TradeDonorRefusal::NameUnstorable:
            return "Name will not fit";
        }
        return "";
    }
}
