#include <algorithm>

#include "Pokemon/Pokemon3FRLG.h"

#include "Pokemon/PersonalInfoTable.h"  // getPersonalInfo -- the cross-game index (gender ratio)
#include "Pokemon/PersonalInfo3FRLG.h"    // this group's own per-species data
#include "Pokemon/Experience.h"        // getLevelFromExp / getExpForLevel / getGrowthRate
#include "Pokemon/PIDSearch.h"         // wurmpleEvolutionToHold / wurmpleEvolutionFromPID
#include "Utils/Gen3Text.h"            // the Gen 3 character set, shared with Trainer3FRLG + Convert

namespace Names
{
    // Species names are generation-stable, so there is one table and this is the whole of
    // the dependency on it. Forward-declared rather than included: the name table is
    // declared in Trainer/Trainer.h, and a Pokemon-layer source must not include the
    // trainer layer to reach it.
    const char *getSpeciesName(uint16_t speciesId);
}


namespace Pokemon
{

    const char *Pokemon3FRLG::species() const noexcept { return Names::getSpeciesName(speciesID()); }

    uint8_t Pokemon3FRLG::form() const noexcept
    {
        if (speciesID() == 201)
        { // Unown: form derived from PID
            return unownFormFromPID(pid());
        }
        return 0;
    }

    uint8_t Pokemon3FRLG::level() const noexcept
    {
        uint8_t levelValue = getLevelFromExp(exp(), getGrowthRate(speciesID()));
        return levelValue < 1 ? 1 : (levelValue > 100 ? 100 : levelValue);
    }

    uint8_t Pokemon3FRLG::gender() const noexcept
    {
        const uint8_t genderRatio = getPersonalInfo(speciesID(), form()).genderRatio;
        if (genderRatio == 255) return 2;
        // female-only
        if (genderRatio == 254) return 1;
        // male-only
        if (genderRatio == 0) return 0;
        return (pid() & 0xFF) < genderRatio ? 1 : 0;
    }

    uint16_t Pokemon3FRLG::ability() const noexcept
    {
        // Gen 3's own slot pair -- the modern table disagrees for 101 of the 386 Gen 3
        // species (Sableye is Keen Eye alone here, Keen Eye/Stall there), so reading the
        // bit through the modern table names an ability the game would never show.
        const PersonalRecord &g3 = getPersonalInfo3FRLG(speciesID(), 0);
        return ((iv32() >> 31) & 1) ? g3.ability2 : g3.ability1;
    }

    // A byte with no glyph is SKIPPED, not shown. Substituting '?' for it is what made a real
    // FireRed FARFETCH'D read back as FARFETCH?D -- and worse, made the substitute indistinguishable
    // from the genuine '?' at 0xAC.
    //
    // THE RECORD'S OWN LANGUAGE PICKS THE TABLE, not the save it is sitting in. A byte is a font
    // index, and a Japanese cartridge's font is a different one, so a Japanese Pokemon traded into
    // an English game keeps its own bytes and has to be read back through its own table. PKHeX does
    // the same (PK3.Nickname passes Language, never the save's). Reading one through the wrong table
    // does not fail loudly -- it silently truncates at the first character the table has no slot for.
    std::u16string Pokemon3FRLG::readG3Name(size_t offset, int maxChars, uint8_t languageId) const
    {
        std::u16string text;
        for (int index = 0; index < maxChars; ++index)
        {
            const uint8_t packedByte = rd8(offset + index);
            if (packedByte == Utils::GEN3_TERMINATOR)
                break;
            if (const char16_t character = Utils::gen3ToChar(packedByte, languageId))
                text += character;
        }
        return text;
    }

    // The NICKNAME is read through the record's own language even on an egg, where those bytes are
    // the Japanese word for egg and mean nothing else -- keeping the getter byte-faithful is what
    // lets the writer put the placeholder back untouched. What the user is shown instead is the
    // caller's business; see Pokemon::eggTextIsPlaceholder.
    std::u16string Pokemon3FRLG::nickname() const { return readG3Name(0x08, 10, language()); }
    std::u16string Pokemon3FRLG::otName() const { return readG3Name(0x14, 7, otTextLanguage()); }

    // Encode a name into the Gen 3 character table at destination[offset..]: up to maxChars glyphs,
    // then a 0xFF terminator IF there is room. The PK3 name fields have NO space past maxChars (nickname
    // 10 @ 0x08, OT name 7 @ 0x14 -- 0x1B right after it is Markings), so the terminator is only written
    // when the name is short. Unrepresentable chars end the name; bytes past the terminator are left
    // untouched. Without these setters a created pokemon's OT name + nickname were BLANK, so Gen 3 read the
    // pokemon as traded-in ("Apparently met") and showed no name.
    static void g3EncodeName(std::byte *destination, size_t offset, const std::u16string &value, size_t maxChars, uint8_t languageId)
    {
        size_t byteCount = 0;
        for (const char16_t character : value)
        {
            if (byteCount >= maxChars)
                break;
            const uint8_t packedByte = Utils::charToGen3(character, languageId);
            // no Gen 3 glyph -> end the name here
            if (packedByte == Utils::GEN3_TERMINATOR) break;
            destination[offset + byteCount] = static_cast<std::byte>(packedByte);
            ++byteCount;
        }
        if (byteCount < maxChars)
            destination[offset + byteCount] = static_cast<std::byte>(Utils::GEN3_TERMINATOR);
    }

    void Pokemon3FRLG::setOTName(const std::u16string &value) noexcept
    {
        // otTextLanguage, not language(): an egg's byte says Japanese and its OT bytes do not.
        g3EncodeName(data.data(), 0x14, value, 7, otTextLanguage()); // OT name: 7 chars @ 0x14 (0x1B is Markings)
        refreshChecksum();
    }

    // Every character must have a byte in the Gen 3 table. Callers check this and refuse the name
    // outright, because g3EncodeName ends the name at the first unmappable character -- which would
    // store a truncated name instead of reporting that it can't be stored.
    //
    // WHAT IS STORABLE DEPENDS ON THE RECORD'S LANGUAGE, so this asks the same table the setter will
    // write through. On a Japanese record that is what lets a name typed in the Roman alphabet
    // through at all: the Japanese table has no half-width Latin, and charToGen3 rescues it by
    // retrying full-width, which is how the cartridge itself stores such a name.
    bool Pokemon3FRLG::canStoreNickname(const std::u16string &value) const noexcept
    {
        if (isEgg()) return true; // setNickname stores the egg placeholder, whatever it is given
        const uint8_t languageId = language();
        for (const char16_t character : value)
        {
            if (Utils::charToGen3(character, languageId) == Utils::GEN3_TERMINATOR)
                return false;
        }
        return true;
    }

    // An egg has no name of its own: the games write the Japanese word for egg in every region, and
    // PKHeX's PK3 Nickname setter writes that placeholder whatever it is given while IsEgg is set.
    // Storing a real name here would leave an egg no cartridge holds, in the Japanese charset its
    // language selects. Clear the egg bit before naming a hatched Pokemon.
    void Pokemon3FRLG::setNickname(const std::u16string &value) noexcept
    {
        const std::u16string storedNickname = isEgg() ? std::u16string(EGG_NICKNAME_JAPANESE) : value;
        g3EncodeName(data.data(), 0x08, storedNickname, 10, language()); // nickname: 10 chars @ 0x08 (no isNicknamed flag)
        refreshChecksum();
    }

    uint8_t Pokemon3FRLG::baseHP() const noexcept { return getPersonalInfo3FRLG(speciesID(), 0).hp; }
    uint8_t Pokemon3FRLG::baseATK() const noexcept { return getPersonalInfo3FRLG(speciesID(), 0).atk; }
    uint8_t Pokemon3FRLG::baseDEF() const noexcept { return getPersonalInfo3FRLG(speciesID(), 0).def; }
    uint8_t Pokemon3FRLG::baseSPE() const noexcept { return getPersonalInfo3FRLG(speciesID(), 0).spe; }
    uint8_t Pokemon3FRLG::baseSPA() const noexcept { return getPersonalInfo3FRLG(speciesID(), 0).spa; }
    uint8_t Pokemon3FRLG::baseSPD() const noexcept { return getPersonalInfo3FRLG(speciesID(), 0).spd; }

    uint16_t Pokemon3FRLG::computeStat(int index) const noexcept
    {
        const PersonalRecord &bs = getPersonalInfo3FRLG(speciesID(), 0);
        const int base[6] = {bs.hp, bs.atk, bs.def, bs.spe, bs.spa, bs.spd}; // HP,ATK,DEF,SPE,SPA,SPD
        // Base HP 0 means "no row for this species" (out of Gen 3's dex, or a blank slot), NOT a
        // real base stat -- computing from it would invent a stat for a pokemon we know nothing about.
        if (base[0] == 0)
            return 1;
        // Shedinja: a base HP of 1 is the game's flag for "always 1 HP", not an input to the
        // formula (PKHeX PKM.GetStats). Without this it would come out of the HP formula at ~76.
        if (index == 0 && base[0] == 1)
            return 1;
        const int individualValues[6] = {ivHP(), ivATK(), ivDEF(), ivSPE(), ivSPA(), ivSPD()};
        const int evValue[6] = {evHP(), evATK(), evDEF(), evSPE(), evSPA(), evSPD()};
        const int levelValue = level();
        const int common = (2 * base[index] + individualValues[index] + evValue[index] / 4) * levelValue / 100;
        // HP formula
        if (index == 0) return static_cast<uint16_t>(common + levelValue + 10);
        return static_cast<uint16_t>((common + 5) * natureStatPercent(nature(), index - 1) / 100);
    }

    uint16_t Pokemon3FRLG::statHPMax() const noexcept { return computeStat(0); }
    uint16_t Pokemon3FRLG::statATK() const noexcept { return computeStat(1); }
    uint16_t Pokemon3FRLG::statDEF() const noexcept { return computeStat(2); }
    uint16_t Pokemon3FRLG::statSPE() const noexcept { return computeStat(3); }
    uint16_t Pokemon3FRLG::statSPA() const noexcept { return computeStat(4); }
    uint16_t Pokemon3FRLG::statSPD() const noexcept { return computeStat(5); }
    uint16_t Pokemon3FRLG::statHPCurrent() const noexcept
    {
        if (dataSize < 0x64)
            return statHPMax();
        return rd16(0x56);
    }

    void Pokemon3FRLG::setStatHPCurrent(uint16_t value) noexcept
    {
        // box record has no such field
        if (dataSize < 0x64) return;
        wr16(0x56, std::min<uint16_t>(value, statHPMax()));
        // 0x56 sits past the checksummed region (0x20..0x4F), so no refreshChecksum() is needed.
    }

    uint16_t Pokemon3FRLG::carryCurrentHP(uint16_t storedCur, uint16_t storedMax, uint16_t newMax) noexcept
    {
        // max moved (or was never set) -> refill
        if (storedMax != newMax) return newMax;
        return std::min(storedCur, newMax); // max unchanged -> keep the pokemon's own HP
    }

    void Pokemon3FRLG::recalculateStats() noexcept
    {
        // box pokemon (80 B): battle stats are computed on the fly, none stored
        if (dataSize < 0x64) return;
        wr8(0x54, level());
        const uint16_t max = computeStat(0);
        wr16(0x56, carryCurrentHP(rd16(0x56), rd16(0x58), max));
        wr16(0x58, max);
        wr16(0x5A, computeStat(1));
        wr16(0x5C, computeStat(2)); // ATK / DEF
        wr16(0x5E, computeStat(3));
        wr16(0x60, computeStat(4)); // SPE / SPA
        wr16(0x62, computeStat(5)); // SPD
    }

    void Pokemon3FRLG::setExp(uint32_t value) noexcept
    {
        // The level is not stored in a box record; level() derives it from EXP, and the party
        // level byte is restamped by recalculateStats().
        wr32(0x24, value);
        recalculateStats();
        refreshChecksum();
    }

    void Pokemon3FRLG::setLevel(uint8_t levelValue) noexcept
    {
        if (levelValue < 1)
            levelValue = 1;
        else if (levelValue > 100)
            levelValue = 100;
        wr32(0x24, getExpForLevel(levelValue, getGrowthRate(speciesID())));
        recalculateStats();
        refreshChecksum();
    }


    void Pokemon3FRLG::rerollPID(int wantShiny, int wantGender, int wantNature, int wantAbilityBit) noexcept
    {
        const uint32_t trainerId32 = id32();
        const uint16_t trainerShinyValue = static_cast<uint16_t>((trainerId32 & 0xFFFF) ^ (trainerId32 >> 16));
        const uint8_t genderRatio = getPersonalInfo(speciesID(), form()).genderRatio;
        uint32_t pidValue = pid();

        // Unown: the letter is part of the PID, so a reroll silently renamed the Pokemon -- editing
        // shininess or nature turned an UNOWN A into an UNOWN F. It is always preserved; no caller
        // wants it changed (setForm is a no-op in Gen 3 precisely because the PID owns it).
        //
        // CONSTRUCTED, not filtered. Rejecting 27 of every 28 candidates would multiply an already
        // expensive search (a shiny nature change is ~1 PID in 819k) by 28 and blow the budget. Instead
        // the letter's bits are overwritten on each candidate with a pattern that already yields the
        // wanted letter, so every candidate tested is a letter match and the search costs what it did
        // before. The other 24 bits still come from the walk, so nature/gender/shiny stay uniform.
        // Wurmple: which line it evolves into is part of the PID too (PKHeX WurmpleUtil), so it is held.
        const int wantWurmpleEvolution = wurmpleEvolutionToHold(speciesID(), pidValue);
        const bool isUnown = (speciesID() == 201);
        const int wantLetter = isUnown ? static_cast<int>(form()) : -1;
        uint32_t letterPatterns[10] = {}; // 8-bit values v with v % 28 == wantLetter (at most 10)
        int letterPatternCount = 0;
        if (isUnown)
        {
            for (uint32_t value = static_cast<uint32_t>(wantLetter); value <= 0xFFu; value += 28)
                letterPatterns[letterPatternCount++] = value;
            // Every valid pattern shares the letter's parity (28 is even), so bit 0 -- and with it the
            // Gen 3 ability bit -- is decided by the letter. Preserving both is therefore consistent by
            // construction: the current PID already satisfies it. Nothing to reconcile.
        }
        // Budget: the hardest real case is a SHINY pokemon of a dual-ability species having its nature
        // changed -- shiny (1/8192) x nature (1/25) x gender (1/2) x ability bit (1/2) is roughly one
        // PID in 819k. Measured over 80 random shiny mons, 2M candidates left 10% of them unable to
        // stay shiny while 8M left none (worst successful search: 3.45M). A miss is only ever paid
        // once, on a button press, and each step is a multiply-add plus a few compares.
        uint32_t walk = pidValue;
        for (int index = 0; index < 8000000; ++index)
        {
            walk = walk * 0x41C64E6Du + 0x00006073u; // walk candidate PIDs
            pidValue = walk;
            // Stamp in a letter-preserving bit pattern before ANY test -- every constraint below reads
            // the whole PID, so this has to happen first or they would be judging a PID we won't store.
            if (letterPatternCount > 0)
                pidValue = withUnownFormValue(pidValue, letterPatterns[(walk >> 2) % static_cast<uint32_t>(letterPatternCount)]);
            // Cheapest reject first: this LCG's low bit flips every step, so half the walk is
            // discarded by an AND before the nature modulo runs.
            if (wantAbilityBit >= 0 && static_cast<int>(pidValue & 1u) != wantAbilityBit)
                continue;
            if (wantNature >= 0 && static_cast<int>(pidValue % 25) != wantNature)
                continue;
            if (wantWurmpleEvolution >= 0 && wurmpleEvolutionFromPID(pidValue) != wantWurmpleEvolution)
                continue;
            if (wantGender >= 0)
            {
                const int genderValue = (genderRatio == 255) ? 2 : (genderRatio == 254) ? 1 : (genderRatio == 0) ? 0 : ((pidValue & 0xFF) < genderRatio ? 1 : 0);
                if (genderValue != wantGender)
                    continue;
            }
            if (wantShiny >= 0)
            {
                const uint16_t pokemonShinyValue = static_cast<uint16_t>((pidValue & 0xFFFF) ^ (pidValue >> 16));
                const bool isShiny = ((trainerShinyValue ^ pokemonShinyValue) < 8);
                if (isShiny != (wantShiny != 0))
                    continue;
            }
            wr32(0x00, pidValue);
            recalculateStats();
            refreshChecksum();
            return;
        }
        // no PID satisfied the constraints within the budget -> leave the pokemon unchanged
    }

    void Pokemon3FRLG::rerollPreservingShiny(int wantGender, int wantNature, int wantAbilityBit) noexcept
    {
        const int wasShiny = isShiny(id32(), "") ? 1 : 0;
        const uint32_t before = pid();
        rerollPID(wasShiny, wantGender, wantNature, wantAbilityBit);
        // The walk steps before its first test and this LCG is full-period, so it cannot return to
        // its own start inside the budget: an unchanged PID means the search failed, not that it
        // found the PID we already had. Shininess is far and away the most expensive constraint, so
        // it is the one dropped on the retry -- the edit the user actually asked for still lands.
        if (pid() == before)
            rerollPID(-1, wantGender, wantNature, wantAbilityBit);
    }

    // Ability slot 1/2. Gen 3 keeps the choice in IV32 bit 31, but legality also wants the
    // PID's low bit to agree with it (PKHeX AbilityVerifier.GetPIDAbilityMatch), so the bit
    // write is paired with a PID re-roll -- exactly PKHeX's SetAbilityIndex, which calls
    // EntityPID.GetRandomPID and then RefreshAbility.
    void Pokemon3FRLG::setAbilityNumber(uint8_t number) noexcept
    {
        const uint16_t speciesId = speciesID();
        // Granbull / Vibrava / Flygon: PKHeX's Gen 3 personal data collapses their second
        // ability onto the first, but the real games still let the bit be set -- so leave an
        // existing bit alone rather than clearing it (PKHeX G3PKM.RefreshAbility does the same).
        // Vibrava and Flygon inherit the bit from the Trapinch they evolved from, which is where
        // a legitimately-set bit on a species with one ability comes from. Trapinch itself (328)
        // is NOT in this set -- it has a real Hyper Cutter / Arena Trap choice.
        if (speciesId == 210 || speciesId == 329 || speciesId == 330)
            return;

        const PersonalRecord &g3 = getPersonalInfo3FRLG(speciesId, 0);

        // Single-ability species: PKHeX only requires the slot bit to be CLEAR for these
        // (AbilityVerifier's IsAbility12Same branch) and asks nothing of the PID, since both slots
        // resolve to the same ability either way -- so clear the bit without re-rolling. That also
        // protects Unown, whose LETTER is derived from the PID: re-rolling to satisfy an ability it
        // only has one of would silently change which Unown it is.
        if (g3.ability2 == g3.ability1)
        {
            const uint32_t ivValue = iv32();
            if (ivValue & (1u << 31))
            {
                wr32(0x48, ivValue & ~(1u << 31));
                refreshChecksum();
            }
            return;
        }

        const int wantBit = (number == 2) ? 1 : 0;
        // Already in the wanted state, PID included -> change nothing. A pokemon whose PID and bit
        // DISAGREE still falls through, so re-picking the ability is also how that gets repaired.
        if (static_cast<int>((iv32() >> 31) & 1) == wantBit && static_cast<int>(pid() & 1u) == wantBit)
            return;

        uint32_t ivValue = iv32();
        if (wantBit)
            ivValue |= (1u << 31);
        else
            ivValue &= ~(1u << 31);
        wr32(0x48, ivValue);

        // Keep nature, gender and shininess; only the ability bit moves.
        if (static_cast<int>(pid() & 1u) != wantBit)
            rerollPreservingShiny(gender(), nature(), wantBit);
        refreshChecksum();
    }

    void Pokemon3FRLG::setAbility(uint16_t abilityValue) noexcept
    {
        // A PK3 has no ability id field, so only the species' own two slots are expressible.
        // An id that fills neither is silently ignored rather than written somewhere wrong.
        const PersonalRecord &g3 = getPersonalInfo3FRLG(speciesID(), 0);
        if (abilityValue == g3.ability1)
            setAbilityNumber(1);
        else if (abilityValue == g3.ability2)
            setAbilityNumber(2);
    }

    int Pokemon3FRLG::abilityPidBit() const noexcept
    {
        // Only a species with a real ability choice ties its PID's low bit to the stored slot bit;
        // for a single-ability species PKHeX asks nothing of the PID (AbilityVerifier only wants the
        // bit clear), so leaving it unconstrained keeps the search twice as fast -- and keeps an
        // Unown's letter reachable, which an extra pinned bit would make harder.
        const PersonalRecord &g3 = getPersonalInfo3FRLG(speciesID(), 0);
        if (g3.ability2 == g3.ability1)
            return -1;
        return static_cast<int>((iv32() >> 31) & 1);
    }

    // Nature, gender, shininess and the ability bit all live in the same PID, so changing one of
    // them means re-deriving all four. Each setter pins the three it was NOT asked to change --
    // editing a nature must not silently un-shiny the pokemon, nor desync its ability from its PID.
    void Pokemon3FRLG::setNature(uint8_t nature) noexcept
    {
        rerollPreservingShiny(gender(), nature % 25, abilityPidBit());
    }
    void Pokemon3FRLG::setGender(uint8_t genderValue) noexcept
    {
        rerollPreservingShiny(genderValue, nature(), abilityPidBit());
    }

    // setShiny is the one setter shininess is the SUBJECT of, so it is pinned rather than preserved.
    void Pokemon3FRLG::setShiny(bool makeShiny, uint32_t) noexcept
    {
        rerollPID(makeShiny ? 1 : 0, gender(), nature(), abilityPidBit());
    }

    // Legality touch-up after an IV/EV edit -- nothing about the pokemon was asked to change, so unlike
    // the setters above this does NOT fall back to dropping shininess. If no PID can hold all four,
    // keeping the old PID is the safe failure; quietly un-shinying a pokemon the user never edited is not.
    void Pokemon3FRLG::regeneratePID(uint32_t trainerID32) noexcept
    {
        rerollPID(isShiny(trainerID32, "") ? 1 : 0, gender(), nature(), abilityPidBit());
    }
}
