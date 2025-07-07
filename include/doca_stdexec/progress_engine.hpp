#pragma once
#ifndef DOCA_STDEXEC_PE_HPP
#define DOCA_STDEXEC_PE_HPP

#include <cstdint>
#include "doca_stdexec/context.hpp"
#include "operation.hpp"
#include <doca_pe.h>
#include <stdexec/execution.hpp>
#include <thread>

namespace doca_stdexec {

struct doca_pe_deleter {
  void operator()(doca_pe *pe) {
    printf("Destroying pe\n");
    auto status = doca_pe_destroy(pe);
    check_error(status, "Failed to destroy pe");
  }
};

class ProgressEngine {
public:
  ProgressEngine(doca_pe *pe) : pe_(pe) {}
  ProgressEngine(ProgressEngine &&) = default;
  ProgressEngine() {
    doca_pe *pe;
    auto status = doca_pe_create(&pe);
    check_error(status, "Failed to create pe");
    pe_ = std::unique_ptr<doca_pe, doca_pe_deleter>(pe);
  }
  ~ProgressEngine() = default;

  auto connect_ctx(Context &ctx) {
    auto ctx_ptr = ctx.as_ctx();
    return doca_pe_connect_ctx(pe_.get(), ctx_ptr);
  }

  doca_pe *get() noexcept { return pe_.get(); }

  uint8_t progress() { return doca_pe_progress(pe_.get()); }

private:
  std::unique_ptr<doca_pe, doca_pe_deleter> pe_;
};

/////////////////////////////////////////////////////////////////////////////
// run_loop
namespace loop {
class doca_pe_run_loop;

struct task : immovable {
  task *next = this;

  union {
    task *tail = nullptr;
    void (*execute_)(task *) noexcept;
  };

  void execute() noexcept { (*execute_)(this); }
};

template <class ReceiverId> struct operation {
  using Receiver = stdexec::__t<ReceiverId>;

  struct t : task {
    using id = operation;

    doca_pe_run_loop *loop_;
    [[no_unique_address]] Receiver rcvr_;

    static void execute_impl(task *p) noexcept {
      auto &rcvr = static_cast<t *>(p)->rcvr_;
      try {
        if (stdexec::get_stop_token(stdexec::get_env(rcvr)).stop_requested()) {
          stdexec::set_stopped(static_cast<Receiver &&>(rcvr));
        } else {
          stdexec::set_value(static_cast<Receiver &&>(rcvr));
        }
      } catch (...) {
        stdexec::set_error(static_cast<Receiver &&>(rcvr),
                           std::current_exception());
      }
    }

    explicit t(task *tail) noexcept : task{{}, this, tail} {}

    t(task *next, doca_pe_run_loop *loop, Receiver rcvr)
        : task{{}, next, {}}, loop_{loop},
          rcvr_{static_cast<Receiver &&>(rcvr)} {
      execute_ = &execute_impl;
    }

    void start() & noexcept;
  };
};

class doca_pe_run_loop {
  template <class> friend struct operation;

public:
  struct scheduler {
  private:
    struct schedule_task {
      using t = schedule_task;
      using id = schedule_task;
      using sender_concept = stdexec::sender_t;
      using completion_signatures = stdexec::completion_signatures<
          stdexec::set_value_t(), stdexec::set_error_t(std::exception_ptr),
          stdexec::set_stopped_t()>;

      template <class _Receiver>
      using operation = operation<stdexec::__id<_Receiver>>::t;

      auto
      connect(stdexec::receiver auto rcvr) const -> operation<decltype(rcvr)> {
        return {&loop_->head, loop_, static_cast<decltype(rcvr) &&>(rcvr)};
      }

      template <class... Env>
      friend auto get_completion_signatures(const schedule_task &, Env &&...)
          -> completion_signatures {
        return {};
      }

    private:
      friend scheduler;

      struct env {
        using t = env;
        using id = env;

        doca_pe_run_loop *loop_;

        template <class CPO>
        auto query(stdexec::get_completion_scheduler_t<CPO>) const noexcept
            -> scheduler {
          return loop_->get_scheduler();
        }
      };

      explicit schedule_task(doca_pe_run_loop *loop) noexcept : loop_(loop) {}

      doca_pe_run_loop *const loop_;

    public:
      [[nodiscard]]
      auto get_env() const noexcept {
        return env{loop_};
      }
    };

    friend doca_pe_run_loop;

    explicit scheduler(doca_pe_run_loop *loop) noexcept : loop_(loop) {}

    doca_pe_run_loop *loop_;

  public:
    using t = scheduler;
    using id = scheduler;
    auto operator==(const scheduler &) const noexcept -> bool = default;

    [[nodiscard]]
    auto schedule() const noexcept -> schedule_task {
      return schedule_task{loop_};
    }

    [[nodiscard]]
    auto query(stdexec::get_forward_progress_guarantee_t) const noexcept
        -> stdexec::forward_progress_guarantee {
      return stdexec::forward_progress_guarantee::concurrent;
    }

    // BUGBUG NOT TO SPEC
    [[nodiscard]]
    auto query(stdexec::execute_may_block_caller_t) const noexcept -> bool {
      return false;
    }
  };

  doca_pe_run_loop(ProgressEngine pe) noexcept : pe(std::move(pe)) {}

  auto get_scheduler() noexcept -> scheduler { return scheduler{this}; }

  auto connect_ctx(std::shared_ptr<Context> ctx) {
    return stdexec::starts_on(
        get_scheduler(), stdexec::just(ctx) | stdexec::then([&](auto ctx) {
                           auto status =
                               doca_pe_connect_ctx(pe.get(), ctx->as_ctx());
                           check_error(status, "Failed to connect ctx");
                         }));
  }

  void run();

  void run_some();

  void finish();

public:
  ProgressEngine pe;

private:
  void push_back_(task *task);
  auto pop_front_() -> task *;

  std::mutex mutex_;
  std::condition_variable cv_;
  task head{{}, &head, {&head}};
  bool stop_ = false;
};

template <class ReceiverId>
inline void operation<ReceiverId>::t::start() & noexcept {
  try {
    loop_->push_back_(this);
  } catch (...) {
    stdexec::set_error(static_cast<Receiver &&>(rcvr_),
                       std::current_exception());
  }
}

inline void doca_pe_run_loop::run() {
  while (!stop_) {
    run_some();
    while (pe.progress()) {
    }
  }
}

inline void doca_pe_run_loop::run_some() {
  for (task *task; (task = pop_front_()) != &head;) {
    printf("Executing task\n");
    task->execute();
    printf("Executed task\n");
  }
}

inline void doca_pe_run_loop::finish() { stop_ = true; }

inline void doca_pe_run_loop::push_back_(task *task) {
  std::unique_lock lock(mutex_);
  task->next = &head;
  head.tail = head.tail->next = task;
}

inline auto doca_pe_run_loop::pop_front_() -> task * {
  std::unique_lock lock(mutex_);
  if (head.tail == head.next)
    head.tail = &head;
  return std::exchange(head.next, head.next->next);
}

} // namespace loop

using run_loop = loop::doca_pe_run_loop;

class doca_pe_context {
  run_loop loop_;
  std::thread thread_;

public:
  doca_pe_context(ProgressEngine pe) noexcept
      : loop_(std::move(pe)), thread_([this]() { loop_.run(); }) {}

  doca_pe_context() : doca_pe_context(ProgressEngine{}) {}

  auto &get_pe() noexcept { return loop_.pe; }

  auto get_scheduler() noexcept { return loop_.get_scheduler(); }

  auto connect_ctx(std::shared_ptr<Context> ctx) {
    return loop_.connect_ctx(ctx);
  }

  void join() { thread_.join(); }
};

}; // namespace doca_stdexec

#endif // DOCA_STDEXEC_PE_HPP