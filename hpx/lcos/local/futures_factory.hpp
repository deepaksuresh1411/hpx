//  Copyright (c) 2007-2020 Hartmut Kaiser
//
//  SPDX-License-Identifier: BSL-1.0
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef HPX_LCOS_LOCAL_FUTURES_FACTORY_HPP
#define HPX_LCOS_LOCAL_FUTURES_FACTORY_HPP

#include <hpx/config.hpp>
#include <hpx/allocator_support/allocator_deleter.hpp>
#include <hpx/allocator_support/internal_allocator.hpp>
#include <hpx/assertion.hpp>
#include <hpx/coroutines/thread_enums.hpp>
#include <hpx/errors.hpp>
#include <hpx/functional/deferred_call.hpp>
#include <hpx/lcos/detail/future_data.hpp>
#include <hpx/lcos/future.hpp>
#include <hpx/lcos_fwd.hpp>
#include <hpx/memory/intrusive_ptr.hpp>
#include <hpx/runtime/get_worker_thread_num.hpp>
#include <hpx/runtime/launch_policy.hpp>
#include <hpx/runtime/threads/detail/execute_thread.hpp>
#include <hpx/runtime/threads/thread_data_fwd.hpp>
#include <hpx/runtime/threads/thread_helpers.hpp>
#include <hpx/traits/future_access.hpp>
#include <hpx/util/thread_description.hpp>

#include <hpx/parallel/executors/execution.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace hpx { namespace lcos { namespace local {

    ///////////////////////////////////////////////////////////////////////
    namespace detail {
        template <typename Result, typename F, typename Executor,
            typename Base = lcos::detail::task_base<Result>>
        struct task_object;

        template <typename Result, typename F, typename Base>
        struct task_object<Result, F, void, Base> : Base
        {
            typedef Base base_type;
            typedef typename Base::result_type result_type;
            typedef typename Base::init_no_addref init_no_addref;

            F f_;
            threads::thread_id_type runs_as_child_;

            task_object(F const& f)
              : f_(f)
              , runs_as_child_(threads::invalid_thread_id)
            {
            }

            task_object(F&& f)
              : f_(std::move(f))
              , runs_as_child_(threads::invalid_thread_id)
            {
            }

            task_object(init_no_addref no_addref, F const& f)
              : base_type(no_addref)
              , f_(f)
              , runs_as_child_(threads::invalid_thread_id)
            {
            }

            task_object(init_no_addref no_addref, F&& f)
              : base_type(no_addref)
              , f_(std::move(f))
              , runs_as_child_(threads::invalid_thread_id)
            {
            }

            ~task_object() override
            {
                if (runs_as_child_)
                {
                    get_thread_id_data(runs_as_child_)->destroy_thread();
                }
            }

            void do_run() override
            {
                return do_run_impl(std::is_void<Result>());
            }

            // overload get_result_void to be able to run child tasks, if needed
            util::unused_type* get_result_void(error_code& ec = throws) override
            {
                if (runs_as_child_)
                {
                    auto state = this->state_.load(std::memory_order_acquire);

                    // this thread would block on the future, try to
                    // directly execute the child thread in an attempt to
                    // make this future ready
                    if (state == this->empty)
                    {
                        threads::thread_data* child =
                            get_thread_id_data(runs_as_child_);
                        if (threads::detail::execute_thread(child))
                        {
                            // thread terminated, mark as being destroyed
                            runs_as_child_ = threads::invalid_thread_id;
                            child->destroy_thread();
                        }

                        // fall back to possibly suspended wait if necessary
                    }
                }
                return this->base_type::get_result_void(ec);
            }

        private:
            void do_run_impl(/*is_void=*/std::false_type)
            {
                try
                {
                    this->set_value(f_());
                }
                catch (...)
                {
                    this->set_exception(std::current_exception());
                }
            }

            void do_run_impl(/*is_void=*/std::true_type)
            {
                try
                {
                    f_();
                    this->set_value(result_type());
                }
                catch (...)
                {
                    this->set_exception(std::current_exception());
                }
            }

        protected:
            // run in a separate thread
            threads::thread_id_type apply(threads::thread_pool_base* pool,
                const char *annotation,
                launch policy,
                threads::thread_priority priority,
                threads::thread_stacksize stacksize,
                threads::thread_schedule_hint schedulehint,
                error_code& ec) override
            {
                this->check_started();

                typedef typename Base::future_base_type future_base_type;
                future_base_type this_(this);

                if (policy == launch::fork)
                {
                    schedulehint.mode =
                        threads::thread_schedule_hint_mode_thread;
                    schedulehint.hint =
                        static_cast<std::int16_t>(get_worker_thread_num());
                    threads::thread_id_type id =
                        threads::register_thread_nullary(pool,
                            util::deferred_call(
                                &base_type::run_impl, std::move(this_)),
                            util::thread_description(f_, annotation),
                            threads::pending_do_not_schedule, true,
                            threads::thread_priority_boost, schedulehint,
                            stacksize, ec);

                    if (schedulehint.runs_as_child)
                    {
                        runs_as_child_ = id;
                    }
                    return id;
                }

                if (schedulehint.runs_as_child)
                {
                    // create new thread without scheduling it right away
                    threads::thread_id_type id =
                        threads::register_thread_nullary(pool,
                            util::deferred_call(
                                &base_type::run_impl, std::move(this_)),
                            util::thread_description(f_, annotation),
                            threads::suspended, true, priority,
                            schedulehint, stacksize, ec);

                    runs_as_child_ = id;

                    // now schedule the thread
                    threads::set_thread_state(id,threads::pending,
                        threads::wait_signaled, threads::thread_priority_normal,
                        true, ec);
                    return id;
                }

                threads::register_thread_nullary(pool,
                    util::deferred_call(&base_type::run_impl, std::move(this_)),
                    util::thread_description(f_, annotation),
                    threads::pending, false, priority, schedulehint, stacksize,
                    ec);
                return threads::invalid_thread_id;
            }
        };

        template <typename Allocator, typename Result, typename F,
            typename Base>
        struct task_object_allocator : task_object<Result, F, void, Base>
        {
            typedef task_object<Result, F, void, Base> base_type;
            typedef typename Base::result_type result_type;
            typedef typename Base::init_no_addref init_no_addref;

            typedef typename std::allocator_traits<
                Allocator>::template rebind_alloc<task_object_allocator>
                other_allocator;

            task_object_allocator(other_allocator const& alloc, F const& f)
              : base_type(f)
              , alloc_(alloc)
            {
            }

            task_object_allocator(other_allocator const& alloc, F&& f)
              : base_type(std::move(f))
              , alloc_(alloc)
            {
            }

            task_object_allocator(init_no_addref no_addref,
                other_allocator const& alloc, F const& f)
              : base_type(no_addref, f)
              , alloc_(alloc)
            {
            }

            task_object_allocator(
                init_no_addref no_addref, other_allocator const& alloc, F&& f)
              : base_type(no_addref, std::move(f))
              , alloc_(alloc)
            {
            }

        private:
            void destroy() override
            {
                using traits = std::allocator_traits<other_allocator>;

                other_allocator alloc(alloc_);
                traits::destroy(alloc, this);
                traits::deallocate(alloc, this, 1);
            }

            other_allocator alloc_;
        };

        template <typename Result, typename F, typename Executor, typename Base>
        struct task_object : task_object<Result, F, void, Base>
        {
            typedef task_object<Result, F, void, Base> base_type;
            typedef typename Base::result_type result_type;
            typedef typename Base::init_no_addref init_no_addref;

            Executor* exec_;

            task_object(F const& f)
              : base_type(f)
              , exec_(nullptr)
            {
            }

            task_object(F&& f)
              : base_type(std::move(f))
              , exec_(nullptr)
            {
            }

            task_object(Executor& exec, F const& f)
              : base_type(f)
              , exec_(&exec)
            {
            }

            task_object(Executor& exec, F&& f)
              : base_type(std::move(f))
              , exec_(&exec)
            {
            }

            task_object(init_no_addref no_addref, F const& f)
              : base_type(no_addref, f)
              , exec_(nullptr)
            {
            }

            task_object(init_no_addref no_addref, F&& f)
              : base_type(no_addref, std::move(f))
            {
            }

            task_object(Executor& exec, init_no_addref no_addref, F const& f)
              : base_type(no_addref, f)
              , exec_(&exec)
            {
            }

            task_object(Executor& exec, init_no_addref no_addref, F&& f)
              : base_type(no_addref, std::move(f))
              , exec_(&exec)
            {
            }

        protected:
            // run in a separate thread
            threads::thread_id_type apply(threads::thread_pool_base* pool,
                const char *annotation,
                launch policy, threads::thread_priority priority,
                threads::thread_stacksize stacksize,
                threads::thread_schedule_hint schedulehint,
                error_code& ec) override
            {
                if (exec_)
                {
                    this->check_started();

                    typedef typename Base::future_base_type future_base_type;
                    future_base_type this_(this);

                    parallel::execution::post(*exec_,
                        util::deferred_call(
                            &base_type::run_impl, std::move(this_)),
                        schedulehint, annotation);
                    return threads::invalid_thread_id;
                }

                return this->base_type::apply(
                    pool, annotation, policy, priority, stacksize,
                    schedulehint, ec);
            }
        };

        ///////////////////////////////////////////////////////////////////////
        template <typename Result, typename F, typename Executor>
        struct cancelable_task_object;

        template <typename Result, typename F>
        struct cancelable_task_object<Result, F, void>
          : task_object<Result, F, void,
                lcos::detail::cancelable_task_base<Result>>
        {
            typedef task_object<Result, F, void,
                lcos::detail::cancelable_task_base<Result>>
                base_type;
            typedef typename base_type::result_type result_type;
            typedef typename base_type::init_no_addref init_no_addref;

            cancelable_task_object(F const& f)
              : base_type(f)
            {
            }

            cancelable_task_object(F&& f)
              : base_type(std::move(f))
            {
            }

            cancelable_task_object(init_no_addref no_addref, F const& f)
              : base_type(no_addref, f)
            {
            }

            cancelable_task_object(init_no_addref no_addref, F&& f)
              : base_type(no_addref, std::move(f))
            {
            }
        };

        template <typename Allocator, typename Result, typename F>
        struct cancelable_task_object_allocator
          : cancelable_task_object<Result, F, void>
        {
            typedef cancelable_task_object<Result, F, void> base_type;
            typedef typename base_type::result_type result_type;
            typedef typename base_type::init_no_addref init_no_addref;

            typedef typename std::allocator_traits<Allocator>::
                template rebind_alloc<cancelable_task_object_allocator>
                    other_allocator;

            cancelable_task_object_allocator(
                other_allocator const& alloc, F const& f)
              : base_type(f)
              , alloc_(alloc)
            {
            }

            cancelable_task_object_allocator(
                other_allocator const& alloc, F&& f)
              : base_type(std::move(f))
              , alloc_(alloc)
            {
            }

            cancelable_task_object_allocator(init_no_addref no_addref,
                other_allocator const& alloc, F const& f)
              : base_type(no_addref, f)
              , alloc_(alloc)
            {
            }

            cancelable_task_object_allocator(
                init_no_addref no_addref, other_allocator const& alloc, F&& f)
              : base_type(no_addref, std::move(f))
              , alloc_(alloc)
            {
            }

        private:
            void destroy() override
            {
                using traits = std::allocator_traits<other_allocator>;

                other_allocator alloc(alloc_);
                traits::destroy(alloc, this);
                traits::deallocate(alloc, this, 1);
            }

            other_allocator alloc_;
        };

        template <typename Result, typename F, typename Executor>
        struct cancelable_task_object
          : task_object<Result, F, Executor,
                lcos::detail::cancelable_task_base<Result>>
        {
            typedef task_object<Result, F, Executor,
                lcos::detail::cancelable_task_base<Result>>
                base_type;
            typedef typename base_type::result_type result_type;
            typedef typename base_type::init_no_addref init_no_addref;

            cancelable_task_object(F const& f)
              : base_type(f)
            {
            }

            cancelable_task_object(F&& f)
              : base_type(std::move(f))
            {
            }

            cancelable_task_object(Executor& exec, F const& f)
              : base_type(exec, f)
            {
            }

            cancelable_task_object(Executor& exec, F&& f)
              : base_type(exec, std::move(f))
            {
            }

            cancelable_task_object(init_no_addref no_addref, F const& f)
              : base_type(no_addref, f)
            {
            }

            cancelable_task_object(init_no_addref no_addref, F&& f)
              : base_type(no_addref, std::move(f))
            {
            }

            cancelable_task_object(
                Executor& exec, init_no_addref no_addref, F const& f)
              : base_type(exec, no_addref, f)
            {
            }

            cancelable_task_object(
                Executor& exec, init_no_addref no_addref, F&& f)
              : base_type(exec, no_addref, std::move(f))
            {
            }
        };
    }    // namespace detail
}}}      // namespace hpx::lcos::local

namespace hpx { namespace traits { namespace detail {
    template <typename Result, typename F, typename Base, typename Allocator>
    struct shared_state_allocator<
        lcos::local::detail::task_object<Result, F, void, Base>, Allocator>
    {
        using type = lcos::local::detail::task_object_allocator<Allocator,
            Result, F, Base>;
    };

    template <typename Result, typename F, typename Allocator>
    struct shared_state_allocator<
        lcos::local::detail::cancelable_task_object<Result, F, void>, Allocator>
    {
        using type =
            lcos::local::detail::cancelable_task_object_allocator<Allocator,
                Result, F>;
    };
}}}    // namespace hpx::traits::detail

namespace hpx { namespace lcos { namespace local {

    ///////////////////////////////////////////////////////////////////////////
    // The futures_factory is very similar to a packaged_task except that it
    // allows for the owner to go out of scope before the future becomes ready.
    // We provide this class to avoid semantic differences to the C++11
    // std::packaged_task, while otoh it is a very convenient way for us to
    // implement hpx::async.
    namespace detail {

        ///////////////////////////////////////////////////////////////////////
        template <typename Result, bool Cancelable, typename Executor = void>
        struct create_task_object;

        template <typename Result>
        struct create_task_object<Result, false, void>
        {
            typedef hpx::intrusive_ptr<lcos::detail::task_base<Result>>
                return_type;
            typedef
                typename lcos::detail::future_data_refcnt_base::init_no_addref
                    init_no_addref;

            template <typename F>
            static return_type call(F&& f)
            {
                return return_type(new task_object<Result, F, void>(
                                       init_no_addref{}, std::forward<F>(f)),
                    false);
            }

            template <typename R>
            static return_type call(R (*f)())
            {
                return return_type(new task_object<Result, Result (*)(), void>(
                                       init_no_addref{}, f),
                    false);
            }

            template <typename Allocator, typename F>
            static return_type call(Allocator const& a, F&& f)
            {
                using base_allocator = Allocator;
                using shared_state =
                    typename traits::detail::shared_state_allocator<
                        task_object<Result, F, void>, base_allocator>::type;

                using other_allocator = typename std::allocator_traits<
                    base_allocator>::template rebind_alloc<shared_state>;
                using traits = std::allocator_traits<other_allocator>;

                using init_no_addref = typename shared_state::init_no_addref;

                using unique_ptr = std::unique_ptr<shared_state,
                    util::allocator_deleter<other_allocator>>;

                other_allocator alloc(a);
                unique_ptr p(traits::allocate(alloc, 1),
                    util::allocator_deleter<other_allocator>{alloc});
                traits::construct(alloc, p.get(), init_no_addref{}, alloc,
                    std::forward<F>(f));

                return return_type(p.release(), false);
            }

            template <typename Allocator, typename R>
            static return_type call(Allocator const& a, R (*f)())
            {
                using base_allocator = Allocator;
                using shared_state =
                    typename traits::detail::shared_state_allocator<
                        task_object<Result, Result (*)(), void>,
                        base_allocator>::type;

                using other_allocator = typename std::allocator_traits<
                    base_allocator>::template rebind_alloc<shared_state>;
                using traits = std::allocator_traits<other_allocator>;

                using init_no_addref = typename shared_state::init_no_addref;

                using unique_ptr = std::unique_ptr<shared_state,
                    util::allocator_deleter<other_allocator>>;

                other_allocator alloc(a);
                unique_ptr p(traits::allocate(alloc, 1),
                    util::allocator_deleter<other_allocator>{alloc});
                traits::construct(alloc, p.get(), init_no_addref{}, alloc, f);

                return return_type(p.release(), false);
            }
        };

        template <typename Result, typename Executor>
        struct create_task_object<Result, false, Executor>
          : create_task_object<Result, false, void>
        {
            typedef hpx::intrusive_ptr<lcos::detail::task_base<Result>>
                return_type;
            typedef
                typename lcos::detail::future_data_refcnt_base::init_no_addref
                    init_no_addref;

            template <typename F>
            static return_type call(Executor& exec, F&& f)
            {
                return return_type(new task_object<Result, F, Executor>(exec,
                                       init_no_addref{}, std::forward<F>(f)),
                    false);
            }

            template <typename R>
            static return_type call(Executor& exec, R (*f)())
            {
                return return_type(
                    new task_object<Result, Result (*)(), Executor>(
                        exec, init_no_addref{}, f),
                    false);
            }
        };

        ///////////////////////////////////////////////////////////////////////
        template <typename Result>
        struct create_task_object<Result, true, void>
        {
            typedef hpx::intrusive_ptr<lcos::detail::task_base<Result>>
                return_type;
            typedef
                typename lcos::detail::future_data_refcnt_base::init_no_addref
                    init_no_addref;

            template <typename F>
            static return_type call(F&& f)
            {
                return return_type(new cancelable_task_object<Result, F, void>(
                                       init_no_addref{}, std::forward<F>(f)),
                    false);
            }

            template <typename R>
            static return_type call(R (*f)())
            {
                return return_type(
                    new cancelable_task_object<Result, Result (*)(), void>(
                        init_no_addref{}, f),
                    false);
            }

            template <typename Allocator, typename F>
            static return_type call(Allocator const& a, F&& f)
            {
                using base_allocator = Allocator;
                using shared_state =
                    typename traits::detail::shared_state_allocator<
                        cancelable_task_object<Result, F, void>,
                        base_allocator>::type;

                using other_allocator = typename std::allocator_traits<
                    base_allocator>::template rebind_alloc<shared_state>;
                using traits = std::allocator_traits<other_allocator>;

                using init_no_addref = typename shared_state::init_no_addref;

                using unique_ptr = std::unique_ptr<shared_state,
                    util::allocator_deleter<other_allocator>>;

                other_allocator alloc(a);
                unique_ptr p(traits::allocate(alloc, 1),
                    util::allocator_deleter<other_allocator>{alloc});
                traits::construct(alloc, p.get(), init_no_addref{}, alloc,
                    std::forward<F>(f));

                return return_type(p.release(), false);
            }

            template <typename Allocator, typename R>
            static return_type call(Allocator const& a, R (*f)())
            {
                using base_allocator = Allocator;
                using shared_state =
                    typename traits::detail::shared_state_allocator<
                        cancelable_task_object<Result, Result (*)(), void>,
                        base_allocator>::type;

                using other_allocator = typename std::allocator_traits<
                    base_allocator>::template rebind_alloc<shared_state>;
                using traits = std::allocator_traits<other_allocator>;

                using init_no_addref = typename shared_state::init_no_addref;

                using unique_ptr = std::unique_ptr<shared_state,
                    util::allocator_deleter<other_allocator>>;

                other_allocator alloc(a);
                unique_ptr p(traits::allocate(alloc, 1),
                    util::allocator_deleter<other_allocator>{alloc});
                traits::construct(alloc, p.get(), init_no_addref{}, alloc, f);

                return return_type(p.release(), false);
            }
        };

        template <typename Result, typename Executor>
        struct create_task_object<Result, true, Executor>
          : create_task_object<Result, true, void>
        {
            typedef hpx::intrusive_ptr<lcos::detail::task_base<Result>>
                return_type;
            typedef
                typename lcos::detail::future_data_refcnt_base::init_no_addref
                    init_no_addref;

            template <typename F>
            static return_type call(Executor& exec, F&& f)
            {
                return return_type(
                    new cancelable_task_object<Result, F, Executor>(
                        exec, init_no_addref{}, std::forward<F>(f)),
                    false);
            }

            template <typename R>
            static return_type call(Executor& exec, R (*f)())
            {
                return return_type(
                    new cancelable_task_object<Result, Result (*)(), Executor>(
                        exec, init_no_addref{}, f),
                    false);
            }
        };
    }    // namespace detail

    template <typename Result, bool Cancelable>
    class futures_factory<Result(), Cancelable>
    {
    protected:
        typedef lcos::detail::task_base<Result> task_impl_type;

    public:
        // construction and destruction
        futures_factory()
          : future_obtained_(false)
        {
        }

        template <typename Executor, typename F>
        explicit futures_factory(Executor& exec, F&& f)
          : task_(
                detail::create_task_object<Result, Cancelable, Executor>::call(
                    exec, std::forward<F>(f)))
          , future_obtained_(false)
        {
        }

        template <typename Executor>
        explicit futures_factory(Executor& exec, Result (*f)())
          : task_(
                detail::create_task_object<Result, Cancelable, Executor>::call(
                    exec, f))
          , future_obtained_(false)
        {
        }

        template <typename F,
            typename Enable = typename std::enable_if<
                !std::is_same<typename hpx::util::decay<F>::type,
                    futures_factory>::value>::type>
        explicit futures_factory(F&& f)
          : task_(detail::create_task_object<Result, Cancelable>::call(
                hpx::util::internal_allocator<>{}, std::forward<F>(f)))
          , future_obtained_(false)
        {
        }

        explicit futures_factory(Result (*f)())
          : task_(detail::create_task_object<Result, Cancelable>::call(
                hpx::util::internal_allocator<>{}, f))
          , future_obtained_(false)
        {
        }

        ~futures_factory() = default;

        futures_factory(futures_factory const& rhs) = delete;
        futures_factory& operator=(futures_factory const& rhs) = delete;

        futures_factory(futures_factory&& rhs)
          : task_(std::move(rhs.task_))
          , future_obtained_(rhs.future_obtained_)
        {
            rhs.task_.reset();
            rhs.future_obtained_ = false;
        }

        futures_factory& operator=(futures_factory&& rhs)
        {
            if (this != &rhs)
            {
                task_ = std::move(rhs.task_);
                future_obtained_ = rhs.future_obtained_;

                rhs.task_.reset();
                rhs.future_obtained_ = false;
            }
            return *this;
        }

        // synchronous execution
        void operator()() const
        {
            if (!task_)
            {
                HPX_THROW_EXCEPTION(task_moved,
                    "futures_factory<Result()>::operator()",
                    "futures_factory invalid (has it been moved?)");
                return;
            }
            task_->run();
        }

        // asynchronous execution
        threads::thread_id_type apply(
            const char *annotation = "futures_factory::apply",
            launch policy = launch::async,
            threads::thread_priority priority =
                threads::thread_priority_default,
            threads::thread_stacksize stacksize =
                threads::thread_stacksize_default,
            threads::thread_schedule_hint schedulehint =
                threads::thread_schedule_hint(),
            error_code& ec = throws) const
        {
            return apply(threads::detail::get_self_or_default_pool(),
                annotation, policy,
                priority, stacksize, schedulehint, ec);
        }

        threads::thread_id_type apply(threads::thread_pool_base* pool,
            const char *annotation = "futures_factory::apply",
            launch policy = launch::async,
            threads::thread_priority priority =
                threads::thread_priority_default,
            threads::thread_stacksize stacksize =
                threads::thread_stacksize_default,
            threads::thread_schedule_hint schedulehint =
                threads::thread_schedule_hint(),
            error_code& ec = throws) const
        {
            if (!task_)
            {
                HPX_THROW_EXCEPTION(task_moved,
                    "futures_factory<Result()>::apply()",
                    "futures_factory invalid (has it been moved?)");
                return threads::invalid_thread_id;
            }
            return task_->apply(
                pool, annotation, policy, priority, stacksize, schedulehint, ec);
        }

        // This is the same as get_future, except that it moves the
        // shared state into the returned future.
        lcos::future<Result> get_future(error_code& ec = throws)
        {
            if (!task_)
            {
                HPX_THROWS_IF(ec, task_moved,
                    "futures_factory<Result()>::get_future",
                    "futures_factory invalid (has it been moved?)");
                return lcos::future<Result>();
            }
            if (future_obtained_)
            {
                HPX_THROWS_IF(ec, future_already_retrieved,
                    "futures_factory<Result()>::get_future",
                    "future already has been retrieved from this factory");
                return lcos::future<Result>();
            }

            future_obtained_ = true;

            using traits::future_access;
            return future_access<future<Result>>::create(std::move(task_));
        }

        bool valid() const noexcept
        {
            return !!task_;
        }

        void set_exception(std::exception_ptr const& e)
        {
            if (!task_)
            {
                HPX_THROW_EXCEPTION(task_moved,
                    "futures_factory<Result()>::set_exception",
                    "futures_factory invalid (has it been moved?)");
                return;
            }
            task_->set_exception(e);
        }

    protected:
        hpx::intrusive_ptr<task_impl_type> task_;
        bool future_obtained_;
    };
}}}    // namespace hpx::lcos::local

#endif /*HPX_LCOS_LOCAL_FUTURES_FACTORY_HPP*/
