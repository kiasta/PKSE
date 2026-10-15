#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

#include "UI/ListSearch.h"
#include "UI/PKSEFramebuffer.h"
#include "UI/ScreenChrome.h"
#include "Utils/Keyboard.h"

namespace UI
{
    namespace
    {
        /// ASCII lowercase only, deliberately.
        ///
        /// The labels these lists draw carry real non-ASCII -- "Poke Ball" is written with an
        /// accented e, and the species table keeps PKHeX's punctuation verbatim (Flabebe, Nidoran
        /// with a gender sign). std::tolower on a UTF-8 byte would mangle exactly those, so the
        /// bytes above 0x7F are compared as they are. A user typing plain ASCII still finds them,
        /// because the ASCII runs around the accent are what they will type.
        char toLowerAscii(char character) noexcept
        {
            const unsigned char raw = static_cast<unsigned char>(character);
            if (raw >= 'A' && raw <= 'Z')
                return static_cast<char>(raw - 'A' + 'a');
            return character;
        }

        bool containsIgnoringCase(const char *label, const std::string &term) noexcept
        {
            if (term.empty())
                return true;
            for (const char *start = label; *start != '\0'; ++start)
            {
                size_t termIndex = 0;
                while (termIndex < term.size() && start[termIndex] != '\0' && toLowerAscii(start[termIndex]) == toLowerAscii(term[termIndex]))
                    ++termIndex;
                if (termIndex == term.size())
                    return true;
            }
            return false;
        }
    }

    bool listSearchMatches(const char *label, const std::string &query)
    {
        if (query.empty())
            return true;
        if (label == nullptr)
            return false;

        // Every term must appear, in any order. Splitting here rather than at prompt time keeps
        // the query the user typed intact, so the box shows it back to them exactly.
        size_t termStart = 0;
        while (termStart < query.size())
        {
            while (termStart < query.size() && std::isspace(static_cast<unsigned char>(query[termStart])))
                ++termStart;
            if (termStart >= query.size())
                break;
            size_t termEnd = termStart;
            while (termEnd < query.size() && !std::isspace(static_cast<unsigned char>(query[termEnd])))
                ++termEnd;
            if (!containsIgnoringCase(label, query.substr(termStart, termEnd - termStart)))
                return false;
            termStart = termEnd;
        }
        return true;
    }

    bool ListSearch::promptForQuery(const std::string &listName, FilterTiming timing, const std::function<void()> &queryChanged)
    {
        const std::string queryBeforeTyping = query;
        const auto followQuery = [this, &queryChanged](const std::string &typedText)
        {
            if (typedText == query)
                return;
            query = typedText;
            if (queryChanged)
                queryChanged();
        };
        std::function<void(const std::string &)> textChanged;
        if (timing == FilterTiming::WhileTyping)
            textChanged = followQuery;
        const Utils::KeyboardResult typed = Utils::promptText("Search " + listName, "Type part of a name", query, 32, textChanged);
        followQuery(typed.accepted ? typed.text : queryBeforeTyping);
        return query != queryBeforeTyping;
    }

    int listSearchBoxHeight() noexcept { return 34; }

    void drawListSearchBox(PKSEFramebuffer &framebuffer, int boxX, int boxY, int boxWidth, const ListSearch &search, int matchCount, int totalCount)
    {
        const int boxHeight = listSearchBoxHeight() - 6;
        const bool filtering = search.isFiltering();
        framebuffer.drawFilledRoundedRect(boxX, boxY, boxWidth, boxHeight, 8, Colors::PanelAlt);
        framebuffer.drawRoundedRect(boxX, boxY, boxWidth, boxHeight, 8, filtering ? Colors::Accent : Colors::Border, filtering ? 2 : 1);

        // The magnifier. Drawn rather than set in text -- the UI font has no such glyph -- and sized off
        // the box so the two cannot drift apart. See PKSEFramebuffer::drawSearchIcon for why it is a whole
        // icon rather than a circle beside a stub.
        const int iconSize = std::max(12, boxHeight - 10);
        framebuffer.drawSearchIcon(boxX + 7, boxY + (boxHeight - iconSize) / 2, iconSize, filtering ? Colors::Accent : Colors::TextDim);

        const int textX = boxX + 32;
        const int textY = boxY + (boxHeight - framebuffer.lineHeight(TextStyle::Body)) / 2;
        if (filtering)
        {
            framebuffer.drawText(textX, textY, search.query, Colors::Text);
        }
        else
        {
            framebuffer.drawText(textX, textY, "Y: Search", Colors::TextDim);
        }

        // The tally, right-aligned. Only while filtering: an unfiltered list already says its size
        // in the panel header, and repeating it here would be noise on every screen.
        if (filtering)
        {
            const std::string tally = std::to_string(matchCount) + " of " + std::to_string(totalCount);
            int tallyWidth = 0, tallyHeight = 0;
            framebuffer.measureText(tally, tallyWidth, tallyHeight, TextStyle::Caption);
            framebuffer.drawText(boxX + boxWidth - 12 - tallyWidth, boxY + (boxHeight - tallyHeight) / 2, tally, matchCount == 0 ? Colors::ShinyStar : Colors::TextDim, TextStyle::Caption);
        }
    }
}
