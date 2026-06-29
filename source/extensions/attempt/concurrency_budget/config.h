#pragma once

#include <memory>

#include "envoy/extensions/attempt/concurrency_budget/v3/concurrency_budget_config.pb.validate.h"
#include "envoy/upstream/admission_control.h"

#include "source/extensions/attempt/concurrency_budget/concurrency_budget.h"

namespace Envoy {
namespace Extensions {
namespace Attempt {

class ConcurrencyBudgetFactory : public Upstream::AttemptAdmissionControllerFactory {
public:
  Upstream::AttemptAdmissionControllerSharedPtr createAdmissionController(
      const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor,
      Runtime::Loader& runtime, std::string runtime_key_prefix,
      const Upstream::ClusterCircuitBreakersStats& cb_stats, Stats::Scope& /*stats*/) override;

  std::string name() const override { return "envoy.attempt.concurrency_budget"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::attempt::concurrency_budget::v3::ConcurrencyBudgetConfig>();
  }
};

} // namespace Attempt
} // namespace Extensions
} // namespace Envoy
