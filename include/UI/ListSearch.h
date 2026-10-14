/**
 * WHICH LISTS HAVE ONE IS A RULE, not "all of them": a DIALOG that asks the user to pick an element
 * out of a list or array gets a search box. That is every picker kind, the Items pouch and the file
 * browser, plus Box/Bank in its highlight-rather-than-filter form. The two screens that pick WHAT TO
 * OPEN -- the save/title picker and the Save Backups screen -- deliberately do NOT: they choose a
 * target rather than an element, over lists short enough to fit on screen whole. Both once had a
 * box and both had it removed. Adding one to a list outside the rule is a product decision, never a
 * consistency tidy-up.
 *
 * The Met Location list is why the component exists at all: 600+ entries with no way to reach the
 * one you want. Sharing it rather than writing one per list is what stops a user who learns the
 * picker's search from having to learn the file browser's -- same key, same matching rule, same box
 * on screen, across the set.
 *
 * HOW A QUERY MATCHES. Case-insensitively, and EVERY whitespace-separated term must appear
 * somewhere in the label -- order-independent, so "couple nursery" finds "Nursery Couple". Someone
 * who half-remembers a name should not have to remember its word order too. Substring rather than
 * prefix for the same reason: "ball" finds "Poke Ball", which a prefix match would not.
 *
 * MATCH WHAT IS ON SCREEN. A list must filter on the label it actually draws, not on the value
 * behind it. Those differ constantly here -- the Met Location picker draws a name resolved through
 * the pokemon's origin version, the Form picker through its species -- and filtering on anything else
 * hides rows the user can plainly see.
 *
 * CLEARING. Accepting an empty query clears the filter. That is the whole contract, so no list
 * needs a second key for it, and there is no state a user can get stuck in.
 */
#ifndef UI_LIST_SEARCH_H
#define UI_LIST_SEARCH_H

#include <functional>
#include <string>

#include "UI/Common.h"

namespace UI
{
    class PKSEFramebuffer;

    /// Case-insensitive; every whitespace-separated term in `query` must appear in `label`.
    /// An empty query matches everything, which is what makes "no filter" the same code path as
    /// "filter" for every caller.
    bool listSearchMatches(const char *label, const std::string &query);

    /// The search state belonging to ONE list.
    struct ListSearch
    {
        /// What the user typed. Empty means no filter is applied.
        std::string query;

        bool isFiltering() const noexcept { return !query.empty(); }
        void clear() noexcept { query.clear(); }

        bool matches(const char *label) const { return listSearchMatches(label, query); }

        enum class FilterTiming
        {
            /// The query updates with every key, and `queryChanged` runs after each update so the
            /// list can re-filter under the keyboard while it is still open.
            WhileTyping,
            /// The query updates once, when the prompt is accepted.
            OnAccept
        };

        /// Opens the keyboard seeded with the current query and stores what it produces.
        /// Returns true when the query changed, so a caller can re-clamp its selection.
        ///
        /// A CANCEL LEAVES THE QUERY ALONE -- or, while typing, PUTS IT BACK: the query the list
        /// was filtered by before the keyboard opened is restored, and `queryChanged` runs once more
        /// for it. Treating a cancel as an empty string would silently drop a filter the user had set
        /// and wanted to keep -- the same trap Keyboard.h warns about for the fields it edits.
        bool promptForQuery(const std::string &listName, FilterTiming timing, const std::function<void()> &queryChanged = nullptr);
    };

    /// Height of the row drawListSearchBox() draws, so a caller can lay its list out before
    /// drawing anything.
    int listSearchBoxHeight() noexcept;

    /// Draws the search row: the query (or a hint when empty) and how much of the list survives it.
    ///
    /// `matchCount` / `totalCount` are shown because a filter that hides everything otherwise looks
    /// like an empty list -- "0 of 632" says the query is wrong, where a blank panel says the data
    /// is missing. That distinction is exactly what issue  turned on.
    void drawListSearchBox(PKSEFramebuffer &framebuffer, int boxX, int boxY, int boxWidth, const ListSearch &search, int matchCount, int totalCount);
}

#endif
