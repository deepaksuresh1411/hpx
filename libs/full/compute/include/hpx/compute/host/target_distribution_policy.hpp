//  Copyright (c) 2016 Hartmut Kaiser
//
//  SPDX-License-Identifier: BSL-1.0
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

/// \file host/target_distribution_policy.hpp

#pragma once

#include <hpx/config.hpp>

#if defined(HPX_HAVE_DISTRIBUTED_RUNTIME)
#include <hpx/assert.hpp>
#if !defined(HPX_COMPUTE_DEVICE_CODE)
#include <hpx/async_local/dataflow.hpp>
#endif
#include <hpx/actions_base/traits/is_distribution_policy.hpp>
#include <hpx/compute/detail/target_distribution_policy.hpp>
#include <hpx/compute/host/target.hpp>
#include <hpx/futures/future.hpp>
#include <hpx/runtime_components/create_component_helpers.hpp>
#include <hpx/serialization/base_object.hpp>
#include <hpx/type_support/unused.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <type_traits>
#include <utility>
#include <vector>

namespace hpx { namespace compute { namespace host {
    /// A target_distribution_policy used for CPU bound localities.
    struct target_distribution_policy
      : compute::detail::target_distribution_policy<host::target>
    {
        typedef compute::detail::target_distribution_policy<host::target>
            base_type;

        /// Default-construct a new instance of a \a target_distribution_policy.
        /// This policy will represent all devices on the current locality.
        ///
        target_distribution_policy() {}

        /// Create a new \a target_distribution_policy representing the given
        /// set of targets
        ///
        /// \param targets [in] The targets the new instances should represent
        ///
        target_distribution_policy operator()(
            std::vector<target_type> const& targets,
            std::size_t num_partitions = std::size_t(-1)) const
        {
            if (num_partitions == std::size_t(-1))
                num_partitions = targets.size();
            return target_distribution_policy(targets, num_partitions);
        }

        /// Create a new \a target_distribution_policy representing the given
        /// set of targets
        ///
        /// \param targets [in] The targets the new instances should represent
        ///
        target_distribution_policy operator()(
            std::vector<target_type>&& targets,
            std::size_t num_partitions = std::size_t(-1)) const
        {
            if (num_partitions == std::size_t(-1))
                num_partitions = targets.size();
            return target_distribution_policy(
                HPX_MOVE(targets), num_partitions);
        }

        /// Create a new \a target_distribution_policy representing the given
        /// target
        ///
        /// \param target [in] The target the new instances should represent
        ///
        target_distribution_policy operator()(
            target_type const& target, std::size_t num_partitions = 1) const
        {
            std::vector<target_type> targets;
            targets.push_back(target);
            return target_distribution_policy(
                HPX_MOVE(targets), num_partitions);
        }

        /// Create a new \a target_distribution_policy representing the given
        /// target
        ///
        /// \param target [in] The target the new instances should represent
        ///
        target_distribution_policy operator()(
            target_type&& target, std::size_t num_partitions = 1) const
        {
            std::vector<target_type> targets;
            targets.push_back(HPX_MOVE(target));
            return target_distribution_policy(
                HPX_MOVE(targets), num_partitions);
        }

#if !defined(HPX_COMPUTE_DEVICE_CODE)
        /// Create one object on one of the localities associated by
        /// this policy instance
        ///
        /// \param ts  [in] The arguments which will be forwarded to the
        ///            constructor of the new object.
        ///
        /// \note This function is part of the placement policy implemented by
        ///       this class
        ///
        /// \returns A future holding the global address which represents
        ///          the newly created object
        ///
        template <typename Component, typename... Ts>
        hpx::future<hpx::id_type> create(Ts&&... ts) const
        {
            target_type t = this->get_next_target();
            hpx::id_type target_locality = t.get_locality();
            return components::create_async<Component>(
                target_locality, HPX_FORWARD(Ts, ts)..., HPX_MOVE(t));
        }
#endif

        /// \cond NOINTERNAL
        typedef std::pair<hpx::id_type, std::vector<hpx::id_type>>
            bulk_locality_result;
        /// \endcond

        /// Create multiple objects on the localities associated by
        /// this policy instance
        ///
        /// \param count [in] The number of objects to create
        /// \param vs   [in] The arguments which will be forwarded to the
        ///             constructors of the new objects.
        ///
        /// \note This function is part of the placement policy implemented by
        ///       this class
        ///
        /// \returns A future holding the list of global addresses which
        ///          represent the newly created objects
        ///
        template <typename Component, typename... Ts>
        hpx::future<std::vector<bulk_locality_result>> bulk_create(
            std::size_t count,
            Ts&&...
#if !defined(HPX_COMPUTE_DEVICE_CODE)
            ts
#endif
        ) const
        {
#if defined(HPX_COMPUTE_DEVICE_CODE)
            HPX_UNUSED(count);
            HPX_ASSERT(false);
            return hpx::future<std::vector<bulk_locality_result>>();
#else
            // collect all targets per locality
            std::map<hpx::id_type, std::vector<target_type>> m;
            for (target_type const& t : this->targets_)
            {
                m[t.get_locality()].push_back(t);
            }

            std::vector<hpx::id_type> localities;
            localities.reserve(m.size());

            std::vector<hpx::future<std::vector<hpx::id_type>>> objs;
            objs.reserve(m.size());

            auto end = m.end();
            for (auto it = m.begin(); it != end; ++it)
            {
                localities.push_back(HPX_MOVE(it->first));

                std::size_t num_partitions = 0;
                for (target_type const& t : it->second)
                {
                    num_partitions += this->get_num_items(count, t);
                }

                objs.push_back(
                    components::bulk_create_async<Component>(localities.back(),
                        num_partitions, ts..., HPX_MOVE(it->second)));
            }

            return hpx::dataflow(
                [=](std::vector<hpx::future<std::vector<hpx::id_type>>>&&
                        v) mutable -> std::vector<bulk_locality_result> {
                    HPX_ASSERT(localities.size() == v.size());

                    std::vector<bulk_locality_result> result;
                    result.reserve(v.size());

                    for (std::size_t i = 0; i != v.size(); ++i)
                    {
                        result.emplace_back(
                            HPX_MOVE(localities[i]), v[i].get());
                    }

                    return result;
                },
                HPX_MOVE(objs));
#endif
        }

    protected:
        /// \cond NOINTERNAL
        target_distribution_policy(
            std::vector<target_type> const& targets, std::size_t num_partitions)
          : base_type(targets, num_partitions)
        {
        }

        target_distribution_policy(
            std::vector<target_type>&& targets, std::size_t num_partitions)
          : base_type(HPX_MOVE(targets), num_partitions)
        {
        }

        friend class hpx::serialization::access;

        template <typename Archive>
        void serialize(Archive& ar, unsigned int const)
        {
            ar& serialization::base_object<base_type>(*this);
        }
        /// \endcond
    };

    /// A predefined instance of the \a target_distribution_policy for
    /// localities. It will represent all NUMA domains of the given locality
    /// and will place all items to create here.
    static target_distribution_policy const target_layout;
}}}    // namespace hpx::compute::host

/// \cond NOINTERNAL
namespace hpx { namespace traits {
    template <>
    struct is_distribution_policy<compute::host::target_distribution_policy>
      : std::true_type
    {
    };

    template <>
    struct num_container_partitions<compute::host::target_distribution_policy>
    {
        static std::size_t call(
            compute::host::target_distribution_policy const& policy)
        {
            return policy.get_num_partitions();
        }
    };
}}    // namespace hpx::traits
/// \endcond

#endif
