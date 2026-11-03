// Port of PKHeX PKHeX.Core/Legality/Verifiers/TrashByteVerifier.cs (and the TrashBytesUTF16 helpers it
// calls): string terminators and the bytes left behind a name.
//
// A name field keeps whatever was written to it before, behind the current name's terminator. The
// games and HOME clear or overwrite those bytes in known ways, so leftover text the record's history
// cannot explain was written by something else.
#include <cstddef>
#include <span>
#include <string>

#include "Enums/LanguageID.h"
#include "Legality/Verifiers/NameRules.h"
#include "Legality/Verifiers/SpeciesName.h"
#include "Legality/Verifiers/Verifiers.h"
#include "Pokemon/Pokemon.h"
#include "Legality/Encounters/Encounterable.h"
#include "Legality/Encounters/LegalityInfo.h"

namespace Legality
{
    namespace
    {
        constexpr size_t BYTES_PER_CHARACTER = 2;
        constexpr uint16_t LOCATION_MAX_LAIR = 244;
        constexpr uint8_t MAX_LAIR_LEGENDARY_LEVEL = 70;

        /// PKHeX TrashMatch; every value below TooLongToTell is a failure (TrashMatch.IsInvalid).
        enum class TrashMatch : uint8_t
        {
            NotPresent,
            NotEmpty,
            TooLongToTell,
            Present,
            PresentNone,
            PresentSingle,
        };

        bool isTrashInvalid(TrashMatch match)
        {
            return match < TrashMatch::TooLongToTell;
        }

        char16_t readCharacter(std::span<const std::byte> field, size_t characterIndex)
        {
            const size_t byteIndex = characterIndex * BYTES_PER_CHARACTER;
            return static_cast<char16_t>(static_cast<uint8_t>(field[byteIndex]) | (static_cast<uint8_t>(field[byteIndex + 1]) << 8));
        }

        /// TrashBytesUTF16.GetTerminatorIndex: the character index of the first zero, or -1.
        int getTerminatorIndex(std::span<const std::byte> field)
        {
            const size_t characterCount = field.size() / BYTES_PER_CHARACTER;
            for (size_t characterIndex = 0; characterIndex < characterCount; ++characterIndex)
            {
                if (readCharacter(field, characterIndex) == 0)
                {
                    return static_cast<int>(characterIndex);
                }
            }
            return -1;
        }

        /// TrashBytesUTF16.IsTrashEmpty: every byte is zero (an empty span counts as empty).
        bool isTrashEmpty(std::span<const std::byte> field)
        {
            for (const std::byte fieldByte : field)
            {
                if (fieldByte != std::byte{0})
                {
                    return false;
                }
            }
            return true;
        }

        /// TrashBytesUTF16.IsTrashNotEmpty.
        bool isTrashNotEmpty(std::span<const std::byte> field)
        {
            return !isTrashEmpty(field) || field.empty();
        }

        /// TrashBytesUTF16.IsFinalTerminatorPresent: the field's last character is a terminator.
        bool isFinalTerminatorPresent(std::span<const std::byte> field)
        {
            return field.size() >= BYTES_PER_CHARACTER && field[field.size() - 1] == std::byte{0} && field[field.size() - 2] == std::byte{0};
        }

        /// TrashBytesUTF16.IsTrashNone: nothing follows the terminator.
        TrashMatch getTrashNone(std::span<const std::byte> field)
        {
            const size_t remainderStart = static_cast<size_t>(getTerminatorIndex(field) + 1) * BYTES_PER_CHARACTER;
            if (remainderStart >= field.size())
            {
                return TrashMatch::TooLongToTell;
            }
            return isTrashEmpty(field.subspan(remainderStart)) ? TrashMatch::PresentNone : TrashMatch::NotEmpty;
        }

        /// TrashBytesUTF16.IsTrashSingleOrNone: at most one earlier name follows the terminator.
        TrashMatch getTrashSingleOrNone(std::span<const std::byte> field)
        {
            const size_t remainderStart = static_cast<size_t>(getTerminatorIndex(field) + 1) * BYTES_PER_CHARACTER;
            if (remainderStart >= field.size())
            {
                return TrashMatch::TooLongToTell;
            }
            const std::span<const std::byte> remainder = field.subspan(remainderStart);
            const int remainderTerminatorEnd = getTerminatorIndex(remainder) + 1;
            const size_t afterSecondName = static_cast<size_t>(remainderTerminatorEnd) * BYTES_PER_CHARACTER;
            if (afterSecondName < remainder.size() && !isTrashEmpty(remainder.subspan(afterSecondName)))
            {
                return TrashMatch::NotEmpty;
            }
            return remainderTerminatorEnd == 1 ? TrashMatch::PresentNone : TrashMatch::PresentSingle;
        }

        /// TrashBytesUTF16.IsTrashSpecific: the bytes behind the name are `underlayer`'s, and nothing past it.
        TrashMatch getTrashSpecific(std::span<const std::byte> field, const std::u16string &underlayer)
        {
            const size_t charactersUsed = static_cast<size_t>(getTerminatorIndex(field) + 1);
            size_t remainderStart = charactersUsed * BYTES_PER_CHARACTER;
            if (remainderStart >= field.size())
            {
                return TrashMatch::TooLongToTell;
            }
            // TrashBytesUTF16.IsUnderlayerPresent
            if (charactersUsed < underlayer.size())
            {
                const size_t fieldCharacterCount = field.size() / BYTES_PER_CHARACTER;
                for (size_t characterIndex = charactersUsed; characterIndex < underlayer.size(); ++characterIndex)
                {
                    if (characterIndex >= fieldCharacterCount || readCharacter(field, characterIndex) != underlayer[characterIndex])
                    {
                        return TrashMatch::NotPresent;
                    }
                }
            }
            const size_t underlayerEnd = underlayer.size() * BYTES_PER_CHARACTER;
            if (underlayerEnd > remainderStart)
            {
                remainderStart = underlayerEnd;
            }
            if (remainderStart < field.size() && !isTrashEmpty(field.subspan(remainderStart)))
            {
                return TrashMatch::NotEmpty;
            }
            return TrashMatch::Present;
        }

        void verifyTrashNone(const AnalysisContext &context, std::span<const std::byte> field, CheckIdentifier identifier, Severity severity = Severity::Invalid)
        {
            if (isTrashInvalid(getTrashNone(field)))
            {
                context.add(severity, identifier, ResultCode::TrashBytesShouldBeEmpty);
            }
        }

        void verifyTrashSpecific(const AnalysisContext &context, std::span<const std::byte> field, const std::u16string &underlayer, CheckIdentifier identifier, Severity severity = Severity::Invalid)
        {
            if (isTrashInvalid(getTrashSpecific(field, underlayer)))
            {
                context.add(severity, identifier, ResultCode::TrashBytesExpected);
            }
        }

        void verifyTrashSingle(const AnalysisContext &context, std::span<const std::byte> field, CheckIdentifier identifier)
        {
            if (isTrashInvalid(getTrashSingleOrNone(field)))
            {
                context.add(Severity::Invalid, identifier, ResultCode::TrashBytesShouldBeEmpty);
            }
        }

        void verifyTrashNotEmpty(const AnalysisContext &context, std::span<const std::byte> field, CheckIdentifier identifier)
        {
            if (!isTrashNotEmpty(field))
            {
                context.add(Severity::Invalid, identifier, ResultCode::TrashBytesExpected);
            }
        }

        void verifyTrashEmpty(const AnalysisContext &context, std::span<const std::byte> field, CheckIdentifier identifier)
        {
            if (!isTrashEmpty(field))
            {
                context.add(Severity::Invalid, identifier, ResultCode::TrashBytesShouldBeEmpty);
            }
        }

        /// PKHeX EncounterStatic8U.GetScientistName: the OT a Dynamax Adventure Pokemon is first written with.
        std::u16string getScientistName(uint8_t language)
        {
            switch (static_cast<Enums::LanguageID>(language))
            {
            case Enums::LanguageID::Japanese:
                return u"けんきゅういん";
            case Enums::LanguageID::English:
                return u"Scientist";
            case Enums::LanguageID::French:
                return u"Scientifique";
            case Enums::LanguageID::Italian:
                return u"Scienziata";
            case Enums::LanguageID::German:
                return u"Forscherin";
            case Enums::LanguageID::Spanish:
                return u"Científica";
            case Enums::LanguageID::Korean:
                return u"연구원";
            case Enums::LanguageID::ChineseSimplified:
                return u"研究员";
            case Enums::LanguageID::ChineseTraditional:
                return u"研究員";
            default:
                return std::u16string();
            }
        }

        bool isSwordShieldOrigin(const Pokemon::Pokemon &pokemon)
        {
            const uint8_t originVersion = pokemon.hasOriginGame() ? pokemon.originGame() : 0;
            return originVersion == static_cast<uint8_t>(Enums::GameVersion::SW) || originVersion == static_cast<uint8_t>(Enums::GameVersion::SH);
        }

        /// PKHeX TrashByteVerifier.VerifyTrashBytesHOME: the Switch formats and Let's Go.
        void verifyHomeEraTrashBytes(const AnalysisContext &context)
        {
            const Pokemon::Pokemon &pokemon = context.pokemon;
            const std::span<const std::byte> nicknameField = pokemon.nicknameBytes();
            const std::span<const std::byte> trainerField = pokemon.originalTrainerNameBytes();
            const std::span<const std::byte> handlerField = pokemon.handlingTrainerNameBytes();
            if (nicknameField.empty() || trainerField.empty() || handlerField.empty())
            {
                return; // the format's name fields are not exposed
            }
            if (!isFinalTerminatorPresent(nicknameField))
            {
                context.add(Severity::Invalid, CheckIdentifier::Nickname, ResultCode::TrashBytesMissingTerminatorFinal);
            }
            if (!isFinalTerminatorPresent(trainerField))
            {
                context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::TrashBytesMissingTerminatorFinal);
            }
            if (!isFinalTerminatorPresent(handlerField))
            {
                context.add(Severity::Invalid, CheckIdentifier::Handler, ResultCode::TrashBytesMissingTerminatorFinal);
            }

            if (pokemon.isEgg())
            {
                if (!isTradedEgg(context) || isSwordShieldOrigin(pokemon))
                {
                    verifyTrashEmpty(context, handlerField, CheckIdentifier::Handler);
                }
                else
                {
                    verifyTrashNotEmpty(context, handlerField, CheckIdentifier::Handler);
                }
                verifyTrashNone(context, trainerField, CheckIdentifier::Trainer);
                if (context.formatGroup == Enums::GameVersion::BDSP)
                {
                    verifyTrashNone(context, nicknameField, CheckIdentifier::Nickname);
                    verifyTrashNone(context, handlerField, CheckIdentifier::Handler);
                }
                else
                {
                    // "Egg" is written over the species name.
                    const std::u16string speciesName = getSpeciesNameForGeneration(pokemon.speciesID(), pokemon.language(), context.formatGeneration);
                    verifyTrashSpecific(context, nicknameField, speciesName, CheckIdentifier::Nickname);
                }
                return;
            }

            if (isNicknamedAsStored(context))
            {
                const std::u16string speciesName = getSpeciesNameForGeneration(pokemon.speciesID(), pokemon.language(), context.formatGeneration);
                verifyTrashSpecific(context, nicknameField, speciesName, CheckIdentifier::Nickname, Severity::Warning);
            }
            else
            {
                verifyTrashNone(context, nicknameField, CheckIdentifier::Nickname, Severity::Warning);
            }

            const Encounters::Encounterable *encounterMatch = Encounters::getEncounterMatch(context);
            const bool isMaxLairOrigin = isSwordShieldOrigin(pokemon) && pokemon.metLocation() == LOCATION_MAX_LAIR;
            if (context.encounter.type == EncounterType::Egg && wasTradedEgg(context))
            {
                // A traded egg may keep one earlier OT behind the hatcher's.
                verifyTrashSingle(context, trainerField, CheckIdentifier::Trainer);
                if (!isSwordShieldOrigin(pokemon))
                {
                    verifyTrashNotEmpty(context, handlerField, CheckIdentifier::Handler); // Sword/Shield does not update it
                }
            }
            else if (isMaxLairOrigin)
            {
                // Dynamax Adventure Pokemon other than the level 70 legends are first written with
                // the Scientist's name (EncounterStatic8U.ShouldHaveScientistTrash); which template
                // matched decides it, so without a match it is unknown.
                if (encounterMatch != nullptr && encounterMatch->templateType() == Encounters::EncounterTemplateType::Static8U)
                {
                    if (encounterMatch->levelMin() != MAX_LAIR_LEGENDARY_LEVEL)
                    {
                        verifyTrashSpecific(context, trainerField, getScientistName(pokemon.language()), CheckIdentifier::Trainer);
                    }
                    else
                    {
                        verifyTrashNone(context, trainerField, CheckIdentifier::Trainer);
                    }
                }
            }
            else
            {
                verifyTrashNone(context, trainerField, CheckIdentifier::Trainer);
            }
        }

        /// PKHeX TrashByteVerifier.VerifyTrashBytesPalPark: Pal Park rewrites a Gen 3 OT, leaving
        /// trash in every language but Japanese.
        void verifyPalParkTrashBytes(const AnalysisContext &context)
        {
            const std::span<const std::byte> trainerField = context.pokemon.originalTrainerNameBytes();
            if (trainerField.empty())
            {
                return;
            }
            // PKHeX tests the WHOLE field, name included, not just what follows the terminator.
            if (context.pokemon.language() == static_cast<uint8_t>(Enums::LanguageID::Japanese))
            {
                if (!isTrashEmpty(trainerField))
                {
                    context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::TrashBytesShouldBeEmpty);
                }
            }
            else if (isTrashEmpty(trainerField))
            {
                context.add(Severity::Invalid, CheckIdentifier::Trainer, ResultCode::TrashBytesExpected);
            }
        }
    }

    void verifyTrashBytes(const AnalysisContext &context)
    {
        if (context.formatGeneration >= 8 || context.formatGroup == Enums::GameVersion::GG)
        {
            verifyHomeEraTrashBytes(context);
        }
        else if (context.formatGeneration == 4)
        {
            // A Gen 4 Mystery Gift (PCD) is compared with the gift's own name bytes, which this check
            // does not read from the matched card yet.
            if (context.encounter.type == EncounterType::Event)
            {
                return;
            }
            if (getOriginGeneration(context) == 3)
            {
                verifyPalParkTrashBytes(context);
            }
        }
    }
}
