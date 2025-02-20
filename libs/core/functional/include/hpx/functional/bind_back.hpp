//  Copyright (c) 2011-2012 Thomas Heller
//  Copyright (c) 2013-2019 Agustin Berge
//
//  SPDX-License-Identifier: BSL-1.0
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <hpx/config.hpp>
#include <hpx/datastructures/member_pack.hpp>
#include <hpx/functional/invoke.hpp>
#include <hpx/functional/invoke_result.hpp>
#include <hpx/functional/one_shot.hpp>
#include <hpx/functional/traits/get_function_address.hpp>
#include <hpx/functional/traits/get_function_annotation.hpp>
#include <hpx/type_support/decay.hpp>
#include <hpx/type_support/pack.hpp>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace hpx {

    namespace detail {

        template <typename F, typename Ts, typename... Us>
        struct invoke_bound_back_result;

        template <typename F, typename... Ts, typename... Us>
        struct invoke_bound_back_result<F, util::pack<Ts...>, Us...>
          : util::invoke_result<F, Us..., Ts...>
        {
        };

        template <typename F, typename Ts, typename... Us>
        using invoke_bound_back_result_t =
            typename invoke_bound_back_result<F, Ts, Us...>::type;

        ///////////////////////////////////////////////////////////////////////
        template <typename F, typename Is, typename... Ts>
        class bound_back;

        template <typename F, std::size_t... Is, typename... Ts>
        class bound_back<F, util::index_pack<Is...>, Ts...>
        {
        public:
            bound_back() = default;    // needed for serialization

            template <typename F_, typename... Ts_,
                typename = std::enable_if_t<std::is_constructible_v<F, F_>>>
            constexpr explicit bound_back(F_&& f, Ts_&&... vs)
              : _f(HPX_FORWARD(F_, f))
              , _args(std::piecewise_construct, HPX_FORWARD(Ts_, vs)...)
            {
            }

#if !defined(__NVCC__) && !defined(__CUDACC__)
            bound_back(bound_back const&) = default;
            bound_back(bound_back&&) = default;
#else
            constexpr HPX_HOST_DEVICE bound_back(bound_back const& other)
              : _f(other._f)
              , _args(other._args)
            {
            }

            constexpr HPX_HOST_DEVICE bound_back(bound_back&& other)
              : _f(HPX_MOVE(other._f))
              , _args(HPX_MOVE(other._args))
            {
            }
#endif

            bound_back& operator=(bound_back const&) = delete;

            template <typename... Us>
            constexpr HPX_HOST_DEVICE
                invoke_bound_back_result_t<F&, util::pack<Ts&...>, Us&&...>
                operator()(Us&&... vs) &
            {
                return HPX_INVOKE(
                    _f, HPX_FORWARD(Us, vs)..., _args.template get<Is>()...);
            }

            template <typename... Us>
            constexpr HPX_HOST_DEVICE invoke_bound_back_result_t<F const&,
                util::pack<Ts const&...>, Us&&...>
            operator()(Us&&... vs) const&
            {
                return HPX_INVOKE(
                    _f, HPX_FORWARD(Us, vs)..., _args.template get<Is>()...);
            }

            template <typename... Us>
            constexpr HPX_HOST_DEVICE
                invoke_bound_back_result_t<F&&, util::pack<Ts&&...>, Us&&...>
                operator()(Us&&... vs) &&
            {
                return HPX_INVOKE(HPX_MOVE(_f), HPX_FORWARD(Us, vs)...,
                    HPX_MOVE(_args).template get<Is>()...);
            }

            template <typename... Us>
            constexpr HPX_HOST_DEVICE invoke_bound_back_result_t<F const&&,
                util::pack<Ts const&&...>, Us&&...>
            operator()(Us&&... vs) const&&
            {
                return HPX_INVOKE(HPX_MOVE(_f), HPX_FORWARD(Us, vs)...,
                    HPX_MOVE(_args).template get<Is>()...);
            }

            template <typename Archive>
            void serialize(Archive& ar, unsigned int const /*version*/)
            {
                // clang-format off
                ar & _f;
                ar & _args;
                // clang-format on
            }

            constexpr std::size_t get_function_address() const noexcept
            {
                return traits::get_function_address<F>::call(_f);
            }

            constexpr char const* get_function_annotation() const noexcept
            {
#if defined(HPX_HAVE_THREAD_DESCRIPTION)
                return traits::get_function_annotation<F>::call(_f);
#else
                return nullptr;
#endif
            }

#if HPX_HAVE_ITTNOTIFY != 0 && !defined(HPX_HAVE_APEX)
            util::itt::string_handle get_function_annotation_itt() const
            {
#if defined(HPX_HAVE_THREAD_DESCRIPTION)
                return traits::get_function_annotation_itt<F>::call(_f);
#else
                static util::itt::string_handle sh("bound_back");
                return sh;
#endif
            }
#endif

        private:
            F _f;
            util::member_pack_for<Ts...> _args;
        };
    }    // namespace detail

    template <typename F, typename... Ts>
    constexpr hpx::detail::bound_back<std::decay_t<F>,
        util::make_index_pack_t<sizeof...(Ts)>, util::decay_unwrap_t<Ts>...>
    bind_back(F&& f, Ts&&... vs)
    {
        using result_type = hpx::detail::bound_back<std::decay_t<F>,
            util::make_index_pack_t<sizeof...(Ts)>,
            util::decay_unwrap_t<Ts>...>;

        return result_type(HPX_FORWARD(F, f), HPX_FORWARD(Ts, vs)...);
    }

    // nullary functions do not need to be bound again
    template <typename F>
    constexpr std::decay_t<F> bind_back(F&& f)
    {
        return HPX_FORWARD(F, f);
    }
}    // namespace hpx

namespace hpx::util {

    template <typename F, typename... Ts>
    HPX_DEPRECATED_V(
        1, 8, "hpx::util::bind_back is deprecated, use hpx::bind_back instead")
    constexpr decltype(auto) bind_back(F&& f, Ts&&... ts)
    {
        return hpx::bind_back(HPX_FORWARD(F, f), HPX_FORWARD(Ts, ts)...);
    }
}    // namespace hpx::util

///////////////////////////////////////////////////////////////////////////////
namespace hpx { namespace traits {
    ///////////////////////////////////////////////////////////////////////////
#if defined(HPX_HAVE_THREAD_DESCRIPTION)
    template <typename F, typename... Ts>
    struct get_function_address<hpx::detail::bound_back<F, Ts...>>
    {
        static constexpr std::size_t call(
            hpx::detail::bound_back<F, Ts...> const& f) noexcept
        {
            return f.get_function_address();
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    template <typename F, typename... Ts>
    struct get_function_annotation<hpx::detail::bound_back<F, Ts...>>
    {
        static constexpr char const* call(
            hpx::detail::bound_back<F, Ts...> const& f) noexcept
        {
            return f.get_function_annotation();
        }
    };

#if HPX_HAVE_ITTNOTIFY != 0 && !defined(HPX_HAVE_APEX)
    template <typename F, typename... Ts>
    struct get_function_annotation_itt<hpx::detail::bound_back<F, Ts...>>
    {
        static util::itt::string_handle call(
            hpx::detail::bound_back<F, Ts...> const& f) noexcept
        {
            return f.get_function_annotation_itt();
        }
    };
#endif
#endif
}}    // namespace hpx::traits

///////////////////////////////////////////////////////////////////////////////
namespace hpx { namespace serialization {

    // serialization of the bound_back object
    template <typename Archive, typename F, typename... Ts>
    void serialize(Archive& ar, ::hpx::detail::bound_back<F, Ts...>& bound,
        unsigned int const version = 0)
    {
        bound.serialize(ar, version);
    }
}}    // namespace hpx::serialization
