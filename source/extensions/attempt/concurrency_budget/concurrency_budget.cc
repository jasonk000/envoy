#include "concurrency_budget.h"

#include <cstdint>

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Attempt {

void ConcurrencyBudget::StreamAdmissionController::onTryStarted(uint32_t attempt_number) {
  if (stream_active_retry_attempt_numbers_.find(attempt_number) !=
      stream_active_retry_attempt_numbers_.end()) {
    // if this is a retry, we've already counted it as active when it was initially scheduled
    return;
  }
  context_->active_tries_->inc();
  stream_active_tries_++;
  setStats();
}

void ConcurrencyBudget::StreamAdmissionController::onTrySucceeded(uint32_t attempt_number) {
  if (stream_active_retry_attempt_numbers_.erase(attempt_number)) {
    // once a retry reaches the "success" phase, it is no longer considered an active retry
    context_->active_retries_->dec();
    setStats();
  }
}

void ConcurrencyBudget::StreamAdmissionController::onSuccessfulTryFinished() {
  context_->active_tries_->dec();
  stream_active_tries_--;
  setStats();
}

void ConcurrencyBudget::StreamAdmissionController::onTryAborted(uint32_t attempt_number) {
  if (stream_active_retry_attempt_numbers_.erase(attempt_number)) {
    context_->active_retries_->dec();
  }
  context_->active_tries_->dec();
  stream_active_tries_--;
  setStats();
}

bool ConcurrencyBudget::StreamAdmissionController::isAttemptAdmitted(uint32_t prev_attempt_number,
                                                                     uint32_t retry_attempt_number,
                                                                     bool abort_previous_on_retry) {
  uint32_t active_retry_diff_on_retry = 1;
  if (abort_previous_on_retry && stream_active_retry_attempt_numbers_.find(prev_attempt_number) !=
                                     stream_active_retry_attempt_numbers_.end()) {
    // if we admit the retry, we will abort the previous try which was a retry,
    // so the total number of active retries will not change
    active_retry_diff_on_retry = 0;
  }
  if (context_->active_retries_->value() + active_retry_diff_on_retry >
      context_->getRetryConcurrencyLimit()) {
    return false;
  }
  context_->active_retries_->add(active_retry_diff_on_retry);
  if (abort_previous_on_retry) {
    // no change to active_tries
    stream_active_retry_attempt_numbers_.erase(prev_attempt_number);
  } else {
    context_->active_tries_->inc();
    stream_active_tries_++;
  }
  stream_active_retry_attempt_numbers_.insert(retry_attempt_number);
  setStats();
  return true;
};

uint64_t ConcurrencyBudget::ConcurrencyBudgetContext::getRetryConcurrencyLimit() const {
  const double budget_percent = runtime_.snapshot().getDouble(budget_percent_key_, budget_percent_);
  const uint64_t min_retry_concurrency_limit = runtime_.snapshot().getInteger(
      min_retry_concurrency_limit_key_, min_retry_concurrency_limit_);
  const uint64_t retry_concurrency_limit = active_tries_->value() * budget_percent / 100.0;
  return std::max(retry_concurrency_limit, min_retry_concurrency_limit);
}

void ConcurrencyBudget::StreamAdmissionController::setStats() {
  const uint64_t retry_concurrency_limit = context_->getRetryConcurrencyLimit();
  const uint64_t latched_active_retries = context_->active_retries_->value();
  const uint64_t retries_remaining = latched_active_retries > retry_concurrency_limit
                                         ? 0
                                         : retry_concurrency_limit - latched_active_retries;
  context_->cb_stats_.remaining_retries_.set(retries_remaining);
  context_->cb_stats_.rq_retry_open_.set(retries_remaining > 0 ? 0 : 1);
}

} // namespace Attempt
} // namespace Extensions
} // namespace Envoy
