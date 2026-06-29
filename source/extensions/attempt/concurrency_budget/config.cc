#include "source/extensions/attempt/concurrency_budget/config.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Attempt {

namespace {
const uint32_t DefaultMinConcurrencyLimit = 3;
const double DefaultBudgetPercent = 20.0;
} // namespace

Upstream::AttemptAdmissionControllerSharedPtr ConcurrencyBudgetFactory::createAdmissionController(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor,
    Runtime::Loader& runtime, std::string runtime_key_prefix,
    const Upstream::ClusterCircuitBreakersStats& cb_stats, Stats::Scope& /*stats*/) {
  const auto& concurrency_budget_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::attempt::concurrency_budget::v3::ConcurrencyBudgetConfig&>(
      config, validation_visitor);

  // These use the retry_budget namespace to be compatible with the core retry-budget
  // implementations.
  const std::string retry_budget_key = absl::StrCat(runtime_key_prefix, "retry_budget.");
  const std::string budget_percent_key = absl::StrCat(retry_budget_key, "budget_percent");
  const std::string min_retry_concurrency_limit_key =
      absl::StrCat(retry_budget_key, "min_retry_concurrency");

  uint32_t min_concurrent_retry_limit = DefaultMinConcurrencyLimit;
  double budget_percent = DefaultBudgetPercent;

  if (concurrency_budget_config.has_retry_budget()) {
    const auto& retry_budget = concurrency_budget_config.retry_budget();

    min_concurrent_retry_limit = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
        retry_budget, min_retry_concurrency, DefaultMinConcurrencyLimit);
    budget_percent =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(retry_budget, budget_percent, DefaultBudgetPercent);
  }

  return std::make_shared<ConcurrencyBudget>(min_concurrent_retry_limit, budget_percent, runtime,
                                             budget_percent_key, min_retry_concurrency_limit_key,
                                             cb_stats);
}

REGISTER_FACTORY(ConcurrencyBudgetFactory, Upstream::AttemptAdmissionControllerFactory);

} // namespace Attempt
} // namespace Extensions
} // namespace Envoy
