/**
 * A lazily produced sequence of encounter candidates: PKSE's counterpart of the C# iterator
 * methods (IEnumerable with `yield return`) PKHeX's encounter generators and enumerators are written
 * as. A C++20 coroutine that `co_yield`s values, read with moveNext()/current() or a range-for.
 *
 * Coroutines need no exceptions: an exception escaping the body cannot happen with
 * -fno-exceptions, and unhandled_exception() aborts to satisfy the language.
 */
#ifndef LEGALITY_ENCOUNTERS_ENCOUNTERSEQUENCE_H
#define LEGALITY_ENCOUNTERS_ENCOUNTERSEQUENCE_H

#include <coroutine>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>

#include "Legality/Encounters/Encounterable.h"

namespace Legality
{
    namespace Encounters
    {
        /// A template that stays alive as long as someone holds it. Static table entries are
        /// borrowed (no allocation); templates a generator builds on the fly (an egg) are owned.
        using EncounterReference = std::shared_ptr<const Encounterable>;

        /// A static table entry, without taking ownership.
        inline EncounterReference borrowEncounter(const Encounterable &encounter) noexcept
        {
            return EncounterReference(std::shared_ptr<const Encounterable>(), &encounter);
        }

        template <typename TemplateClass, typename... Arguments>
        EncounterReference makeEncounter(Arguments &&...arguments)
        {
            return std::make_shared<const TemplateClass>(std::forward<Arguments>(arguments)...);
        }

        /// PKHeX MatchedEncounter<T>: what an enumerator yields.
        struct MatchedEncounter
        {
            EncounterReference encounter;
            EncounterMatchRating rating = EncounterMatchRating::Match;
        };

        template <typename ValueType>
        class EncounterSequence
        {
        public:
            struct promise_type
            {
                std::optional<ValueType> currentValue;

                EncounterSequence get_return_object() noexcept { return EncounterSequence(std::coroutine_handle<promise_type>::from_promise(*this)); }
                std::suspend_always initial_suspend() const noexcept { return {}; }
                std::suspend_always final_suspend() const noexcept { return {}; }
                std::suspend_always yield_value(ValueType value) noexcept
                {
                    currentValue = std::move(value);
                    return {};
                }
                void return_void() const noexcept {}
                void unhandled_exception() const noexcept { std::abort(); }
            };

            EncounterSequence() noexcept = default;
            explicit EncounterSequence(std::coroutine_handle<promise_type> coroutine) noexcept : coroutine(coroutine) {}
            EncounterSequence(EncounterSequence &&other) noexcept : coroutine(std::exchange(other.coroutine, nullptr)) {}
            EncounterSequence &operator=(EncounterSequence &&other) noexcept
            {
                if (this != &other)
                {
                    destroy();
                    coroutine = std::exchange(other.coroutine, nullptr);
                }
                return *this;
            }
            EncounterSequence(const EncounterSequence &) = delete;
            EncounterSequence &operator=(const EncounterSequence &) = delete;
            ~EncounterSequence() { destroy(); }

            /// Runs the body to its next yield; false once it has finished.
            bool moveNext()
            {
                if (!coroutine || coroutine.done())
                {
                    return false;
                }
                coroutine.promise().currentValue.reset();
                coroutine.resume();
                return !coroutine.done();
            }

            /// The value of the last yield. Valid only after moveNext() returned true.
            const ValueType &current() const noexcept { return *coroutine.promise().currentValue; }

            class Iterator
            {
            public:
                explicit Iterator(EncounterSequence *sequence) noexcept : sequence(sequence) {}
                const ValueType &operator*() const noexcept { return sequence->current(); }
                Iterator &operator++()
                {
                    if (!sequence->moveNext())
                    {
                        sequence = nullptr;
                    }
                    return *this;
                }
                bool operator==(std::default_sentinel_t) const noexcept { return sequence == nullptr; }

            private:
                EncounterSequence *sequence;
            };

            Iterator begin()
            {
                return Iterator(moveNext() ? this : nullptr);
            }
            std::default_sentinel_t end() const noexcept { return {}; }

        private:
            void destroy() noexcept
            {
                if (coroutine)
                {
                    coroutine.destroy();
                    coroutine = nullptr;
                }
            }

            std::coroutine_handle<promise_type> coroutine = nullptr;
        };

        /// The candidates a generation's generator yields, best first (PKHeX IEnumerable<IEncounterable>).
        using EncounterCandidates = EncounterSequence<EncounterReference>;
        /// What a generation's enumerator yields (PKHeX IEnumerator<MatchedEncounter<IEncounterable>>).
        using MatchedEncounters = EncounterSequence<MatchedEncounter>;

        /// PKHeX PeekEnumerator<T>: a sequence that can look one candidate ahead.
        class PeekEnumerator
        {
        public:
            explicit PeekEnumerator(EncounterCandidates &&sequence) noexcept : sequence(std::move(sequence)) {}

            bool moveNext()
            {
                if (hasPeeked)
                {
                    hasPeeked = false;
                    currentEncounter = std::move(peekedEncounter);
                    peekedEncounter.reset();
                    return peekResult;
                }
                if (!sequence.moveNext())
                {
                    currentEncounter.reset();
                    return false;
                }
                currentEncounter = sequence.current();
                return true;
            }

            /// PKHeX PeekIsNext: true when another candidate follows the current one.
            bool peekIsNext()
            {
                if (!hasPeeked)
                {
                    hasPeeked = true;
                    peekResult = sequence.moveNext();
                    if (peekResult)
                    {
                        peekedEncounter = sequence.current();
                    }
                }
                return peekResult;
            }

            /// PKHeX Current: the peeked candidate while one is held, else the current one.
            const EncounterReference &current() const noexcept { return hasPeeked && peekResult ? peekedEncounter : currentEncounter; }

        private:
            EncounterCandidates sequence;
            EncounterReference currentEncounter;
            EncounterReference peekedEncounter;
            bool hasPeeked = false;
            bool peekResult = false;
        };
    }
}

#endif
