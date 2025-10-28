/*
 * Copyright 2025 Mitchell Matsumori-Kelly
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <freertos/FreeRTOS.h>

#include <functional>
#include <variant>

//* TODO
#define TSK_USE_STD_FUNCTION

namespace tsk {
    namespace detail {

        template<typename M>
        struct MemberPtr {
            static_assert(false, "not a member pointer");
        };

        template<typename C, typename R, typename... P>
        struct MemberPtr<R (C::*)(P...)> {
            using Return   = R;
            using Class    = C;
            using Params   = std::tuple<P...>;
            using FullType = R (C::*)(P...);

            FullType pointer;
        };

        template<typename C, typename T>
        struct MemberPtr<T C::*> {
        public:
            using Class    = C;
            using Type     = T;
            using FullType = T C::*;

            FullType pointer;
        };

    } // namespace detail

    class TskFn {
        using InnerVariant = std::variant<void (*)(), std::function<void()>>;

    public:
        TskFn(std::function<void()>&& fn) : m_fn(std::move(m_fn)) {}

        TskFn(void (*fn)()) : m_fn(fn) {}

        InnerVariant& inner() {
            return m_fn;
        }

        void invoke() {
            switch (m_fn.index()) {
                case 0:
                    std::get<0>(m_fn)();
                    break;
                case 1:
                    std::get<1>(m_fn)();
                    break;
                default:
                    abort();
            }
        }

        void operator()() {
            invoke();
        }

    private:
        std::variant<void (*)(), std::function<void()>> m_fn;
    };

    class TskStatus {
    public:
        TskStatus(TaskStatus_t status) : m_status(status) {}

        bool is_running() const {
            eTaskState state = m_status.eCurrentState;
            return (state == eTaskState::eRunning)
                   || (state == eTaskState::eBlocked);
        }

        bool is_alive() const {
            eTaskState state = m_status.eCurrentState;
            return (state != eTaskState::eInvalid)
                   && (state != eTaskState::eDeleted);
        }

    private:
        TaskStatus_t m_status;
    };

    class TskHandle {
    public:
        TskHandle(TaskHandle_t inner) : m_handle(inner) {}

        bool is_running() const {
            TaskStatus_t status = freertos_status(false, true);
            return (status.eCurrentState == eTaskState::eRunning)
                   || (status.eCurrentState == eTaskState::eSuspended);
        }

        TaskStatus_t freertos_status(bool free_stack_space, bool state) const {
            TaskStatus_t status;
            vTaskGetInfo(
                m_handle,
                &status,
                free_stack_space,
                state ? eTaskState::eInvalid : eTaskState::eSuspended
            );
            if (!state) {
                status.eCurrentState = eTaskState::eInvalid;
            }
            return status;
        }

    private:
        TaskHandle_t m_handle;
    };

    struct TskConfig {
        uint32_t stack_depth = 1000;
        uint32_t priority    = 5;
    };

    namespace detail {
        class TskRunner {
        public:
            TskRunner(TskFn&& fn) : m_fn(std::move(fn)) {}

            virtual ~TskRunner() {}

            virtual void run() {
                m_fn.invoke();
            }

        protected:
            TskFn m_fn;

            static void freertos_hook(void* ctx) {
                static_cast<TskRunner*>(ctx)->run();
            }
        };

        class OnceTask: public TskRunner {
        public:
            using TskRunner::TskRunner;
        };

    } // namespace detail

    namespace detail {
        struct RunType {};
    } // namespace detail

    struct Once: detail::RunType {
        using Context = TskFn;

        static void run(void* ctx) {
            Context* ctx_ = static_cast<Context*>(ctx);
            ctx_->invoke();
            delete ctx_;
        }
    };

    struct Repeat: detail::RunType {
        struct Context {
            uint32_t interval_ms;
            uint32_t last_tick;
        };

        static void run(void* ctx) {
            Context*   ctx_                  = static_cast<Context*>(ctx);
            TickType_t now                   = 0;
            TickType_t prior_invocation_tick = 0;
        }
    };

    template<typename T>
    class TskBuilder {
    public:
        TskBuilder(TskFnWrap fn) : m_fn(std::move(fn)) {}

        void once();
        void repeat_interval(uint32_t ms);
        void repeat_frequency(uint32_t hz);

        [[nodiscard]]
        TskBuilder& stack_depth(uint32_t stack_depth) {
            m_config.stack_depth = stack_depth;
            return *this;
        }

        [[nodiscard]]
        TskBuilder& with_priority(uint16_t priority) {
            m_config.priority = priority;
            return *this;
        }

    private:
        TskFnWrap m_fn;
        TskConfig m_config;
    };

    template<auto* M>
    void member_wrap(void* instance) {
        using C = detail::MemberPtr<M>::Class;
        (static_cast<C>(instance)->*M)();
    }

    void my_test_task() {}

    void test() {
        tsk::Tsk<Once> test = tsk::Builder(my_test_task).with_priority();
    }

} // namespace tsk
