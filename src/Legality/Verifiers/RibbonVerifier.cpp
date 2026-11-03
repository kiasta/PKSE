// Port of PKHeX PKHeX.Core/Legality/Verifiers/Ribbons/RibbonVerifier.cs: ribbons.
#include "Legality/Verifiers/Verifiers.h"

#include <string>

#include "Legality/Verifiers/RibbonRules.h"
#include "Names/RibbonNames.h"

namespace Legality
{
    namespace
    {
        /// PKHeX RibbonVerifier.AppendAll: the ribbons of one kind, named and comma separated.
        std::string getRibbonListText(const RibbonResultList &results, bool isMissing)
        {
            std::string ribbonListText;
            for (const RibbonResult &result : results)
            {
                if (result.isMissing != isMissing)
                {
                    continue;
                }
                if (!ribbonListText.empty())
                {
                    ribbonListText += ", ";
                }
                ribbonListText += Names::getRibbonName(*result.ribbon, 0);
            }
            return ribbonListText;
        }
    }

    void verifyRibbons(const AnalysisContext &context)
    {
        const RibbonArguments arguments = makeRibbonArguments(context);
        RibbonResultList results;
        getRibbonResults(arguments, results);
        if (results.empty())
        {
            return;
        }
        const std::string missingText = getRibbonListText(results, true);
        const std::string invalidText = getRibbonListText(results, false);
        if (!missingText.empty())
        {
            context.add(Severity::Invalid, CheckIdentifier::Ribbon, ResultCode::RibbonsMissing, missingText);
        }
        if (!invalidText.empty())
        {
            context.add(Severity::Invalid, CheckIdentifier::Ribbon, ResultCode::RibbonsInvalid, invalidText);
        }
    }
}
