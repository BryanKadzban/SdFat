//  (C) Copyright 2008-10 Anthony Williams
//  (C) Copyright 2011-2015 Vicente J. Botet Escriba
//
//  Distributed under the Boost Software License, Version 1.0.
// Boost Software License - Version 1.0 - August 17th, 2003
// 
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#ifndef BOOST_THREAD_FUTURE_HPP
#define BOOST_THREAD_FUTURE_HPP

#include <vector>
#include <utility>
#include <functional>
#include <type_traits>
#include <memory>
#include <nrf52840.h>

/// API required:
///  already_done_future(uint8_t | bool)
///     -> make_ready_future
///  dma_future, which takes a callback to check the done status and allows setting the value
///     -> future(), then set_done_callback (must allocate state_)
///  wrapped_future, which always leaves done true, but does reinterpret the value
///     -> f.then(||(*fut) { return make_ready_future<bool>(fut->get() == 0); })
///  chained_read_future, which takes a callback to return another future to wait on
///     -> f.then(||(*fut) { return do_next_read(); })

namespace future {
  class unique_irqlock {
    public:
      unique_irqlock() : m_needs_enable(irqs_enabled()) {
        __disable_irq();
      }
      ~unique_irqlock() { if (m_needs_enable) { __enable_irq(); } }
      unique_irqlock(const unique_irqlock& rhs) = delete;
      unique_irqlock& operator=(const unique_irqlock& rhs) = delete;

    private:
      bool irqs_enabled() {
        return (__get_PRIMASK() & 1) == 1;
      }
      bool m_needs_enable;
  };
  template<typename T>
  constexpr inline T volatile_load(const T* target) {
    static_assert(std::is_trivially_copyable<T>::value,
        "Volatile load can only be used with trivially copyable types");
    return *static_cast<const volatile T*>(target);
  }
  template<typename T>
  inline void volatile_store(T* target, T value) {
    static_assert(std::is_trivially_copyable<T>::value,
        "Volatile store can only be used with trivially copyable types");
    *static_cast<volatile T*>(target) = value;
  }
  template <typename T> class future;

  namespace detail {
    struct shared_state {
      std::vector<shared_state*> continuations;  // owned by their parent futures

      shared_state():
          continuations() {}
      shared_state(shared_state const&) = delete;
      shared_state& operator=(shared_state const&) = delete;

      virtual ~shared_state() {}

      virtual void launch_continuation() {}
      virtual bool prereqs_done() { return true; }

      void do_continuation() {
        if (! this->continuations.empty()) {
          std::vector<shared_state*> the_continuations(std::move(this->continuations));
          this->continuations.clear();
          for (auto* cont : the_continuations) {
            cont->launch_continuation();
          }
        }
      }

      void set_continuation_ptr(shared_state* continuation) {
        unique_irqlock lock();
        continuations.push_back(continuation);
      }
    };
  }

  namespace detail {
    template<typename Return, typename PreconditionFuture, typename ContinuationF>
    future<Return>
    make_continuation_future(PreconditionFuture&& f, ContinuationF&& c);

    template<typename Return, typename PreconditionFuture, typename ContinuationF>
    struct continuation_shared_state;
  } // detail

  template <typename R>
  class future {
    private:
      typedef std::unique_ptr<detail::shared_state> state_ptr;

      template<typename Return, typename PreconditionFuture, typename ContinuationF>
      friend future<Return>
      detail::make_continuation_future(PreconditionFuture&& f, ContinuationF&& c);
      template<typename Return, typename PreconditionFuture, typename ContinuationF>
      friend struct detail::continuation_shared_state;

      explicit future(state_ptr&& state)
        : state_(std::move(state)) {}

      bool done_;
      std::function<bool(future*)> done_callback_;
      R result_;
      state_ptr state_;

    public:
      future(future const&) = delete;
      future& operator=(future const&) = delete;
      typedef R value_type;

      constexpr future()
        : state_(std::make_unique<detail::shared_state>()) {}

      ~future() {}

      future(future&& other)
        : done_(std::move(other.done_)),
        done_callback_(std::move(other.done_callback_)),
        result_(std::move(other.result_)),
        state_(std::move(other.state_)) {}

      future& operator=(future&& other) {
        done_ = std::move(other.done_);
        done_callback_ = std::move(other.done_callback_);
        result_ = std::move(other.result_);
        state_ = std::move(other.state_);
        return *this;
      }
      bool is_done() {
        // Ruleset:
        //  If we've been given a callback, use and latch it; higher futures don't get a callback
        //  If we have prereqs (in the state), make sure all of them are done first.
        //  If during that process our alt_callback gets inited, make sure it's done.
        //  If all of them are done, wait four our own flag to be set.
        if (done_callback_) {
          if (!done_callback_(this)) {
            return false;
          }
          set_done();
          return true;
        }
        if (state_ && !state_->prereqs_done()) { return false; }
        return volatile_load(&done_);
      }
      void set_done() {
        volatile_store(&done_, true);
      }
      template<typename F>
      void set_done_callback(F&& f) {
        done_callback_ = std::move(f);
      }
      void wait() {
        // TODO: should we wfi here?  will that break particle deviceos?
        while (!is_done()) {}
        //waiters.wait(lk, boost::bind(&shared_state_base::is_done, boost::ref(*this)));
      }

      void mark_finished_with_result(R&& result) {
        result_ = std::move(result);
        set_done();
        state_->do_continuation();
      }

      R get() {
        wait();
        return std::move(result_);
      }

      void set_continuation_ptr(detail::shared_state* continuation) {
        state_->set_continuation_ptr(continuation);
        if (is_done()) {
          state_->do_continuation();
        }
      }

      template<typename Func>
      inline future<typename std::result_of<Func(future)>::type::value_type>
      then(Func&& func) {
        typedef typename std::result_of<Func(future)>::type::value_type then_future_ret;

        return detail::make_continuation_future<then_future_ret>(
            std::move(*this), std::forward<Func>(func)
        );
      }
      void set_value(R&& r) {
        mark_finished_with_result(std::move(r));
      }
  };

  ////////////////////////////////
  // make_ready_future
  ////////////////////////////////
  template <class T>
  future<T> make_ready_future(typename std::remove_reference<T>::type & x) {
    future<T> f;
    f.set_value(x);
    return f;
  }

  template <class T>
  future<T> make_ready_future(typename std::remove_reference<T>::type&& x) {
    future<T> f;
    f.set_value(std::forward<typename std::remove_reference<T>::type>(x));
    return f;
  }

  namespace detail {
    //////////////////////
    template<typename Return, typename PreconditionFuture, typename ContinuationF>
    struct continuation_shared_state: shared_state {
      PreconditionFuture precondition_;
      ContinuationF continuation_;
      future<Return> alt_precondition_;

      continuation_shared_state(PreconditionFuture&& precondition, ContinuationF&& c)
      : precondition_(std::move(precondition)),
        continuation_(std::move(c)),
        alt_precondition_(std::unique_ptr<shared_state>()) {}

      void init() {
        precondition_.set_continuation_ptr(this);
      }

      ~continuation_shared_state() override {}

      // NB: There's no need for synchronization between launch and prereqs_done,
      // because the latter always happens in non-interrupt code (we never set
      // the result from the DMA IRQ, always from an is_done invoked callback),
      // and the former is always called when polling as well.
      bool prereqs_done() override {
        if (precondition_.state_) {
          return precondition_.is_done();
        }
        if (alt_precondition_.state_) {
          return alt_precondition_.is_done();
        }
        return true;
      }
      void launch_continuation() override {
        alt_precondition_ = continuation_(std::move(precondition_));
        precondition_ = PreconditionFuture(std::unique_ptr<shared_state>()); // be sure to clear it
      }
    };

    ////////////////////////////////
    // make_continuation_future
    ////////////////////////////////
    template<typename Return, typename PreconditionFuture, typename ContinuationF>
    future<Return>
    make_continuation_future(PreconditionFuture&& f, ContinuationF&& c) {
      auto h = std::make_unique<continuation_shared_state<Return, PreconditionFuture, ContinuationF> >(std::move(f), std::move(c));
      h->init();

      return future<Return>(std::move(h));
    }
  }
}

#endif // header
