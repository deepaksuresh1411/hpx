//  Copyright (c) 2007-2016 Hartmut Kaiser
//
//  SPDX-License-Identifier: BSL-1.0
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <hpx/config.hpp>
#include <hpx/cache/entries/entry.hpp>

#include <cstddef>

///////////////////////////////////////////////////////////////////////////////
namespace hpx::util::cache::entries {

    ///////////////////////////////////////////////////////////////////////////
    template <typename Value, typename Derived = void>
    class size_entry;

    ///////////////////////////////////////////////////////////////////////////
    namespace detail {

        template <typename Value, typename Derived>
        struct size_derived
        {
            using type = Derived;
        };

        template <typename Value>
        struct size_derived<Value, void>
        {
            using type = size_entry<Value>;
        };
    }    // namespace detail

    ///////////////////////////////////////////////////////////////////////////
    /// \class size_entry size_entry.hpp hpx/cache/entries/size_entry.hpp
    ///
    /// The \a size_entry type can be used to store values in a cache which
    /// have a size associated (such as files, etc.).
    /// Using this type as the cache's entry type makes sure that the entries
    /// with the biggest size are discarded from the cache first.
    ///
    /// \note The \a size_entry conforms to the CacheEntry concept.
    /// \note This type can be used to model a 'discard smallest first' cache
    ///       policy if it is used with a std::greater as the caches'
    ///       UpdatePolicy (instead of the default std::less).
    ///
    /// \tparam Value     The data type to be stored in a cache. It has to be
    ///                   default constructible, copy constructible and
    ///                   less_than_comparable.
    /// \tparam Derived   The (optional) type for which this type is used as a
    ///                   base class.
    ///
    template <typename Value, typename Derived>
    class size_entry
      : public entry<Value, typename detail::size_derived<Value, Derived>::type>
    {
    private:
        using derived_type =
            typename detail::size_derived<Value, Derived>::type;
        using base_type = entry<Value, derived_type>;

    public:
        /// \brief Any cache entry has to be default constructible
        size_entry() = default;

        /// \brief Construct a new instance of a cache entry holding the given
        ///        value.
        explicit size_entry(Value const& val, std::size_t size = 0) noexcept(
            std::is_nothrow_constructible_v<base_type, Value const&>)
          : base_type(val)
          , size_(size)
        {
        }

        /// \brief Construct a new instance of a cache entry holding the given
        ///        value.
        explicit size_entry(Value&& val, std::size_t size = 0) noexcept
          : base_type(HPX_MOVE(val))
          , size_(size)
        {
        }

        /// \brief    Return the 'size' of this entry.
        constexpr std::size_t get_size() const noexcept
        {
            return size_;
        }

        /// \brief Compare the 'age' of two entries. An entry is 'older' than
        ///        another entry if it has a bigger size.
        friend constexpr bool operator<(
            size_entry const& lhs, size_entry const& rhs) noexcept
        {
            return lhs.get_size() > rhs.get_size();
        }

    private:
        std::size_t size_ = 0;    // the 'size' of the entry
    };
}    // namespace hpx::util::cache::entries
