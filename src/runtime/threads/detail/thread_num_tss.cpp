//  Copyright (c) 2007-2015 Hartmut Kaiser
//
//  SPDX-License-Identifier: BSL-1.0
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <hpx/runtime/threads/detail/thread_num_tss.hpp>

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>

namespace hpx { namespace threads { namespace detail {
    namespace {
        std::size_t& thread_num_tss()
        {
            HPX_NATIVE_TLS std::size_t thread_num_tss_ = std::size_t(-1);
            return thread_num_tss_;
        }

        thread_tuple& thread_numbers_tss()
        {
            HPX_NATIVE_TLS thread_tuple thread_numbers_tss_ = {
                std::uint16_t(-1), std::uint16_t(-1)};
            return thread_numbers_tss_;
        }
    }    // namespace

    // use this to store the global thread number/id in thread local storage
    std::size_t set_thread_num_tss(std::size_t num)
    {
        std::swap(thread_num_tss(), num);
        return num;
    }

    // this returns the globl thread number from thread local storage
    std::size_t get_thread_num_tss()
    {
        return thread_num_tss();
    }

    // set the thread number and pool index associated with this
    // system thread into thread local storage
    void set_thread_numbers_tss(const thread_tuple& tup)
    {
        thread_numbers_tss() = tup;
    }

    // this returns a tuple of the global thread number and the pool
    // Id or index that this thread is assigned to
    thread_tuple get_thread_numbers_tss()
    {
        return thread_numbers_tss();
    }

}}}    // namespace hpx::threads::detail
