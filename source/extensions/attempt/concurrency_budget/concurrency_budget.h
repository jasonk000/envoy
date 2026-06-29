#pragma once

#include <cstdint>
#include <memory>

#include "envoy/upstream/admission_control.h"
#include "envoy/upstream/upstream.h"

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Attempt {

class ConcurrencyBudget : public Upstream::AttemptAdmissionController {
public:
  ConcurrencyBudget(uint32_t min_retry_concurrency_limit, double budget_percent,
                    Runtime::Loader& runtime, const std::string& budget_percent_key,
                    const std::string& min_retry_concurrency_limit_key,
                    Upstream::ClusterCircuitBreakersStats cb_stats)
      : context_(std::make_shared<ConcurrencyBudgetContext>(
            min_retry_concurrency_limit, budget_percent, runtime, budget_percent_key,
            min_retry_concurrency_limit_key, cb_stats)) {
    uint64_t retries_remaining =
        runtime.snapshot().getInteger(min_retry_concurrency_limit_key, min_retry_concurrency_limit);

    cb_stats.remaining_retries_.set(retries_remaining);
    cb_stats.rq_retry_open_.set(retries_remaining > 0 ? 0 : 1);
  };
  ~ConcurrencyBudget() override = default;

  Upstream::AttemptStreamAdmissionControllerPtr
  createStreamAdmissionController(const StreamInfo::StreamInfo&) override {
    return std::make_unique<StreamAdmissionController>(context_);
  };

private:
  // TODO(kbaichoo): Replace this with a plain atomic. It's just used for
  // the given count of tries/ active retries to compute the retry concurrency limits.
  using PrimitiveGaugeSharedPtr = std::shared_ptr<Stats::PrimitiveGauge>;

  struct ConcurrencyBudgetContext {
  public:
    ConcurrencyBudgetContext(

        uint64_t min_retry_concurrency_limit, double budget_percent, Runtime::Loader& runtime,
        const std::string& budget_percent_key, const std::string& min_retry_concurrency_limit_key,
        Upstream::ClusterCircuitBreakersStats& cb_stats)
        : cb_stats_(cb_stats), runtime_(runtime), budget_percent_key_(budget_percent_key),
          min_retry_concurrency_limit_key_(min_retry_concurrency_limit_key),
          min_retry_concurrency_limit_(min_retry_concurrency_limit),
          budget_percent_(budget_percent) {};

    uint64_t getRetryConcurrencyLimit() const;
    Upstream::ClusterCircuitBreakersStats cb_stats_;
    PrimitiveGaugeSharedPtr active_tries_{std::make_shared<Stats::PrimitiveGauge>()};
    PrimitiveGaugeSharedPtr active_retries_{std::make_shared<Stats::PrimitiveGauge>()};

  private:
    Runtime::Loader& runtime_;
    const std::string budget_percent_key_;
    const std::string min_retry_concurrency_limit_key_;
    const uint64_t min_retry_concurrency_limit_;
    const double budget_percent_;
  };

  using ConcurrencyBudgetContextSharedPtr = std::shared_ptr<ConcurrencyBudgetContext>;

  class StreamAdmissionController : public Upstream::AttemptStreamAdmissionController {
  public:
    StreamAdmissionController(ConcurrencyBudgetContextSharedPtr context)
        : context_(std::move(context)) {};

    ~StreamAdmissionController() override {
      context_->active_retries_->sub(stream_active_retry_attempt_numbers_.size());
      context_->active_tries_->sub(stream_active_tries_);
    };

    void onTryStarted(uint32_t) override;
    void onTrySucceeded(uint32_t attempt_number) override;
    void onSuccessfulTryFinished() override;
    void onTryAborted(uint32_t attempt_number) override;
    bool isAttemptAdmitted(uint32_t prev_attempt_number, uint32_t retry_attempt_number,
                           bool abort_previous_on_retry) override;
    // The stream controller always allows the initial attempt to be compatible with the prior
    // policy.
    bool isInitialAttemptAdmitted() override { return true; }
    absl::string_view initialAttemptDetails() const override { return ""; }

  private:
    void setStats();

    absl::flat_hash_set<uint32_t> stream_active_retry_attempt_numbers_{};
    uint32_t stream_active_tries_{};
    ConcurrencyBudgetContextSharedPtr context_;
  };
  ConcurrencyBudgetContextSharedPtr context_;
};

} // namespace Attempt
} // namespace Extensions
} // namespace Envoy
