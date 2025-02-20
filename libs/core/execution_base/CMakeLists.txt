# Copyright (c) 2019-2022 The STE||AR-Group
#
# SPDX-License-Identifier: BSL-1.0
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

set(execution_base_headers
    hpx/execution_base/agent_base.hpp
    hpx/execution_base/agent_ref.hpp
    hpx/execution_base/any_sender.hpp
    hpx/execution_base/completion_scheduler.hpp
    hpx/execution_base/completion_signatures.hpp
    hpx/execution_base/context_base.hpp
    hpx/execution_base/detail/spinlock_deadlock_detection.hpp
    hpx/execution_base/execution.hpp
    hpx/execution_base/get_env.hpp
    hpx/execution_base/operation_state.hpp
    hpx/execution_base/receiver.hpp
    hpx/execution_base/resource_base.hpp
    hpx/execution_base/sender.hpp
    hpx/execution_base/this_thread.hpp
    hpx/execution_base/traits/is_executor.hpp
    hpx/execution_base/traits/is_executor_parameters.hpp
)

# cmake-format: off
set(execution_base_compat_headers
    hpx/basic_execution.hpp => hpx/modules/execution_base.hpp
    hpx/parallel/executors/execution_fwd.hpp => hpx/modules/execution_base.hpp
    hpx/util/yield_while.hpp => hpx/modules/execution_base.hpp
    hpx/basic_execution/agent_base.hpp => hpx/modules/execution_base.hpp
    hpx/basic_execution/agent_ref.hpp => hpx/modules/execution_base.hpp
    hpx/basic_execution/context_base.hpp => hpx/modules/execution_base.hpp
    hpx/basic_execution/execution.hpp => hpx/modules/execution_base.hpp
    hpx/basic_execution/resource_base.hpp => hpx/modules/execution_base.hpp
    hpx/basic_execution/this_thread.hpp => hpx/modules/execution_base.hpp
    hpx/traits/is_executor.hpp => hpx/execution.hpp
    hpx/traits/is_executor_parameters.hpp => hpx/execution.hpp
)
# cmake-format: on

set(execution_base_sources agent_ref.cpp any_sender.cpp
                           spinlock_deadlock_detection.cpp this_thread.cpp
)

include(HPX_AddModule)
add_hpx_module(
  core execution_base
  GLOBAL_HEADER_GEN ON
  SOURCES ${execution_base_sources}
  HEADERS ${execution_base_headers}
  COMPAT_HEADERS ${execution_base_compat_headers}
  MODULE_DEPENDENCIES
    hpx_assertion
    hpx_config
    hpx_errors
    hpx_format
    hpx_functional
    hpx_iterator_support
    hpx_lock_registration
    hpx_timing
    hpx_type_support
  CMAKE_SUBDIRS examples tests
)
