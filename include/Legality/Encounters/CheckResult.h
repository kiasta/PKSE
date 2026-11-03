/**
 * One finding an encounter-phase check produced, including PKHeX's Valid judgement, which the
 * encounter search reads (a Valid result lets the search accept a candidate) but never reports.
 * Ported from PKHeX PKHeX.Core/Legality/Structures/CheckResult.cs.
 */
#ifndef LEGALITY_ENCOUNTERS_CHECKRESULT_H
#define LEGALITY_ENCOUNTERS_CHECKRESULT_H

#include <cstdint>
#include <string>

#include "Legality/Legality.h"
#include "Legality/ResultCode.h"

namespace Legality
{
    namespace Encounters
    {
        /// PKHeX Severity, without NotImplemented.
        enum class Judgement : uint8_t
        {
            Valid,
            Fishy,
            Invalid,
        };

        struct CheckResult
        {
            Judgement judgement = Judgement::Valid;
            CheckIdentifier identifier = CheckIdentifier::Encounter;
            ResultCode code = ResultCode::Valid;
            std::string firstArgument;
            std::string secondArgument;

            /// PKHeX CheckResult.Valid: anything short of Invalid.
            bool isValid() const noexcept { return judgement != Judgement::Invalid; }
            /// PKHeX IsNotGeneric: the code is not the plain Valid one.
            bool isNotGeneric() const noexcept { return code != ResultCode::Valid; }

            static CheckResult getValid(CheckIdentifier identifier) { return CheckResult{Judgement::Valid, identifier, ResultCode::Valid, {}, {}}; }
            static CheckResult get(Judgement judgement, CheckIdentifier identifier, ResultCode code, std::string firstArgument = {}, std::string secondArgument = {}) { return CheckResult{judgement, identifier, code, std::move(firstArgument), std::move(secondArgument)}; }
        };
    }
}

#endif
