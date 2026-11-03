/**
 * The species and move ids the move verifiers name, from PKHeX PKHeX.Core/Game/Enums/Species.cs and
 * Move.cs, and the per-generation highest move ids (PKHeX PKHeX.Core/Legality/Legal.cs).
 */
#ifndef LEGALITY_LEARNSOURCE_LEARNSOURCECONSTANTS_H
#define LEGALITY_LEARNSOURCE_LEARNSOURCECONSTANTS_H

#include <cstdint>

namespace Legality
{
    namespace LearnSource
    {
        constexpr uint16_t VENUSAUR_SPECIES = 3;
        constexpr uint16_t CHARIZARD_SPECIES = 6;
        constexpr uint16_t BLASTOISE_SPECIES = 9;
        constexpr uint16_t PIKACHU_SPECIES = 25;
        constexpr uint16_t RAICHU_SPECIES = 26;
        constexpr uint16_t NIDORAN_FEMALE_SPECIES = 29;
        constexpr uint16_t NIDORAN_MALE_SPECIES = 32;
        constexpr uint16_t EEVEE_SPECIES = 133;
        constexpr uint16_t DRATINI_SPECIES = 147;
        constexpr uint16_t MEW_SPECIES = 151;
        constexpr uint16_t PICHU_SPECIES = 172;
        constexpr uint16_t UNOWN_SPECIES = 201;
        constexpr uint16_t SMEARGLE_SPECIES = 235;
        constexpr uint16_t CELEBI_SPECIES = 251;
        constexpr uint16_t NINCADA_SPECIES = 290;
        constexpr uint16_t NINJASK_SPECIES = 291;
        constexpr uint16_t SHEDINJA_SPECIES = 292;
        constexpr uint16_t VOLBEAT_SPECIES = 313;
        constexpr uint16_t ILLUMISE_SPECIES = 314;
        constexpr uint16_t CASTFORM_SPECIES = 351;
        constexpr uint16_t DEOXYS_SPECIES = 386;
        constexpr uint16_t BURMY_SPECIES = 412;
        constexpr uint16_t MOTHIM_SPECIES = 414;
        constexpr uint16_t CHERRIM_SPECIES = 421;
        constexpr uint16_t SHELLOS_SPECIES = 422;
        constexpr uint16_t GASTRODON_SPECIES = 423;
        constexpr uint16_t ROTOM_SPECIES = 479;
        constexpr uint16_t GIRATINA_SPECIES = 487;
        constexpr uint16_t SHAYMIN_SPECIES = 492;
        constexpr uint16_t ARCEUS_SPECIES = 493;
        constexpr uint16_t ZORUA_SPECIES = 570;
        constexpr uint16_t DEERLING_SPECIES = 585;
        constexpr uint16_t SAWSBUCK_SPECIES = 586;
        constexpr uint16_t LARVESTA_SPECIES = 636;
        constexpr uint16_t TORNADUS_SPECIES = 641;
        constexpr uint16_t THUNDURUS_SPECIES = 642;
        constexpr uint16_t LANDORUS_SPECIES = 645;
        constexpr uint16_t KYUREM_SPECIES = 646;
        constexpr uint16_t KELDEO_SPECIES = 647;
        constexpr uint16_t MELOETTA_SPECIES = 648;
        constexpr uint16_t GENESECT_SPECIES = 649;
        constexpr uint16_t SCATTERBUG_SPECIES = 664;
        constexpr uint16_t SPEWPA_SPECIES = 665;
        constexpr uint16_t VIVILLON_SPECIES = 666;
        constexpr uint16_t FLABEBE_SPECIES = 669;
        constexpr uint16_t FLOETTE_SPECIES = 670;
        constexpr uint16_t FLORGES_SPECIES = 671;
        constexpr uint16_t XERNEAS_SPECIES = 716;
        constexpr uint16_t ZYGARDE_SPECIES = 718;
        constexpr uint16_t HOOPA_SPECIES = 720;
        constexpr uint16_t SILVALLY_SPECIES = 773;
        constexpr uint16_t NECROZMA_SPECIES = 800;
        constexpr uint16_t MELTAN_SPECIES = 808;
        constexpr uint16_t MELMETAL_SPECIES = 809;
        constexpr uint16_t INDEEDEE_SPECIES = 876;
        constexpr uint16_t URSHIFU_SPECIES = 892;
        constexpr uint16_t CALYREX_SPECIES = 898;
        constexpr uint16_t ENAMORUS_SPECIES = 905;

        constexpr uint16_t HYDRO_PUMP_MOVE = 56;
        constexpr uint16_t SURF_MOVE = 57;
        constexpr uint16_t BLIZZARD_MOVE = 59;
        constexpr uint16_t MIMIC_MOVE = 102;
        constexpr uint16_t METRONOME_MOVE = 118;
        constexpr uint16_t MIRROR_MOVE_MOVE = 119;
        constexpr uint16_t SELF_DESTRUCT_MOVE = 120;
        constexpr uint16_t SKY_ATTACK_MOVE = 143;
        constexpr uint16_t TRANSFORM_MOVE = 144;
        constexpr uint16_t EXPLOSION_MOVE = 153;
        constexpr uint16_t STRUGGLE_MOVE = 165;
        constexpr uint16_t NIGHTMARE_MOVE = 171;
        constexpr uint16_t SPITE_MOVE = 180;
        constexpr uint16_t SCARY_FACE_MOVE = 184;
        constexpr uint16_t SLEEP_TALK_MOVE = 214;
        constexpr uint16_t BATON_PASS_MOVE = 226;
        constexpr uint16_t EXTREME_SPEED_MOVE = 245;
        constexpr uint16_t WHIRLPOOL_MOVE = 250;
        constexpr uint16_t BLAST_BURN_MOVE = 307;
        constexpr uint16_t HYDRO_CANNON_MOVE = 308;
        constexpr uint16_t OVERHEAT_MOVE = 315;
        constexpr uint16_t FRENZY_PLANT_MOVE = 338;
        constexpr uint16_t VOLT_TACKLE_MOVE = 344;
        constexpr uint16_t DRAGON_DANCE_MOVE = 349;
        constexpr uint16_t AIR_SLASH_MOVE = 403;
        constexpr uint16_t DEFOG_MOVE = 432;
        constexpr uint16_t DRACO_METEOR_MOVE = 434;
        constexpr uint16_t LEAF_STORM_MOVE = 437;
        constexpr uint16_t CHATTER_MOVE = 448;
        constexpr uint16_t BUG_BITE_MOVE = 450;
        constexpr uint16_t DARK_VOID_MOVE = 464;
        constexpr uint16_t RELIC_SONG_MOVE = 547;
        constexpr uint16_t SECRET_SWORD_MOVE = 548;
        constexpr uint16_t FUSION_FLARE_MOVE = 558;
        constexpr uint16_t FUSION_BOLT_MOVE = 559;
        constexpr uint16_t HYPERSPACE_HOLE_MOVE = 593;
        constexpr uint16_t THOUSAND_ARROWS_MOVE = 614;
        constexpr uint16_t THOUSAND_WAVES_MOVE = 615;
        constexpr uint16_t LIGHT_OF_RUIN_MOVE = 617;
        constexpr uint16_t HYPERSPACE_FURY_MOVE = 621;
        constexpr uint16_t CORE_ENFORCER_MOVE = 687;
        constexpr uint16_t SUNSTEEL_STRIKE_MOVE = 713;
        constexpr uint16_t MOONGEIST_BEAM_MOVE = 714;
        constexpr uint16_t REVIVAL_BLESSING_MOVE = 863;
        constexpr uint16_t BLAZING_TORQUE_MOVE = 896;
        constexpr uint16_t WICKED_TORQUE_MOVE = 897;
        constexpr uint16_t NOXIOUS_TORQUE_MOVE = 898;
        constexpr uint16_t COMBAT_TORQUE_MOVE = 899;
        constexpr uint16_t MAGICAL_TORQUE_MOVE = 900;
        constexpr uint16_t TERA_STARSTORM_MOVE = 906;

        // PKHeX MoveInfo.IsMoveZ / IsMoveDynamax ranges.
        constexpr uint16_t FIRST_Z_MOVE = 622;
        constexpr uint16_t LAST_Z_MOVE_FIRST_BLOCK = 658;
        constexpr uint16_t FIRST_Z_MOVE_SECOND_BLOCK = 695;
        constexpr uint16_t LAST_Z_MOVE_SECOND_BLOCK = 703;
        constexpr uint16_t SIGNATURE_Z_MOVE_PIKACHU_CAP = 719;
        constexpr uint16_t FIRST_Z_MOVE_THIRD_BLOCK = 723;
        constexpr uint16_t LAST_Z_MOVE = 728;
        constexpr uint16_t FIRST_DYNAMAX_MOVE = 757;
        constexpr uint16_t LAST_DYNAMAX_MOVE = 774;

        constexpr uint16_t MAX_MOVE_ID_1 = 165;
        constexpr uint16_t MAX_MOVE_ID_2 = 251;
        constexpr uint16_t MAX_MOVE_ID_3 = 354;
        constexpr uint16_t MAX_MOVE_ID_4 = 467;
        constexpr uint16_t MAX_MOVE_ID_5 = 559;
        constexpr uint16_t MAX_MOVE_ID_6_ORAS = 621;
        constexpr uint16_t MAX_MOVE_ID_7_USUM = 728;
        constexpr uint16_t MAX_MOVE_ID_7_LETS_GO = 742;
        constexpr uint16_t MAX_MOVE_ID_8_LEGENDS_ARCEUS = 850;
        constexpr uint16_t MAX_MOVE_ID_8_BDSP = 826;
        constexpr uint16_t MAX_MOVE_ID_9 = 919;
        constexpr uint16_t MAX_MOVE_ID_9_LEGENDS_ZA = 920;

        constexpr uint16_t MAX_SPECIES_ID_7_SUN_MOON = 802;
    }
}

#endif
