//  Copyright (c) 2007-2022 Hartmut Kaiser
//
//  SPDX-License-Identifier: BSL-1.0
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <hpx/config.hpp>
#include <hpx/binary_filter/zlib_serialization_filter_registration.hpp>

#if defined(HPX_HAVE_COMPRESSION_ZLIB)
#include <hpx/modules/serialization.hpp>

#include <cstddef>
#include <memory>
#include <vector>

#include <hpx/config/warnings_prefix.hpp>

#include <boost/iostreams/filter/zlib.hpp>

///////////////////////////////////////////////////////////////////////////////
namespace hpx::plugins::compression {

    namespace detail {

        class zlib_compdecomp
          : public boost::iostreams::detail::zlib_base
          , public boost::iostreams::detail::zlib_allocator<
                std::allocator<char>>
        {
            using allocator_type =
                boost::iostreams::detail::zlib_allocator<std::allocator<char>>;

        public:
            zlib_compdecomp(bool compress = false,
                boost::iostreams::zlib_params const& params =
                    boost::iostreams::zlib::default_compression);
            ~zlib_compdecomp();

            bool save(char const*& src_begin, char const* src_end,
                char*& dest_begin, char* dest_end, bool flush = false);
            bool load(char const*& begin_in, char const* end_in,
                char*& begin_out, char* end_out);

            void close();

            bool eof() const noexcept
            {
                return eof_;
            }

        private:
            bool compress_;
            bool eof_;
        };
    }    // namespace detail

    struct HPX_LIBRARY_EXPORT zlib_serialization_filter
      : public serialization::binary_filter
    {
        zlib_serialization_filter(bool compress = false,
            serialization::binary_filter* next_filter = nullptr) noexcept
          : compdecomp_(compress)
          , current_(0)
        {
        }

        void load(void* dst, std::size_t dst_count);
        void save(void const* src, std::size_t src_count);
        bool flush(void* dst, std::size_t dst_count, std::size_t& written);

        void set_max_length(std::size_t size);
        std::size_t init_data(
            char const* buffer, std::size_t size, std::size_t buffer_size);

    protected:
        std::size_t load_impl(void* dst, std::size_t dst_count, void const* src,
            std::size_t src_count);

    private:
        // serialization support
        friend class hpx::serialization::access;

        template <typename Archive>
        HPX_FORCEINLINE void serialize(Archive& ar, const unsigned int)
        {
        }

        HPX_SERIALIZATION_POLYMORPHIC(zlib_serialization_filter);

        detail::zlib_compdecomp compdecomp_;
        std::vector<char> buffer_;
        std::size_t current_;
    };
}    // namespace hpx::plugins::compression

#include <hpx/config/warnings_suffix.hpp>

#endif
