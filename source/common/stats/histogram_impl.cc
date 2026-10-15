#include "source/common/stats/histogram_impl.h"

#include <algorithm>
#include <limits>
#include <string>

#include "source/common/common/utility.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Stats {

namespace {
const ConstSupportedBuckets default_buckets{};

PreparedSummaryQueryPtr prepareQuery(const ConstSupportedBuckets& bounds, double scale = 1.0) {
  std::vector<double> scaled_bounds;
  scaled_bounds.reserve(bounds.size());
  for (const double bound : bounds) {
    scaled_bounds.push_back(bound * scale);
  }
  PreparedSummaryQuery* query =
      hist_prepared_bounds_alloc(scaled_bounds.data(), static_cast<int>(scaled_bounds.size()));
  RELEASE_ASSERT(query != nullptr, "Invalid prepared histogram summary query");
  return PreparedSummaryQueryPtr(query);
}
} // namespace

HistogramStatisticsImpl::HistogramStatisticsImpl()
    : supported_buckets_(default_buckets), owned_prepared_query_(nullptr),
      prepared_query_(HistogramSettingsImpl::defaultPreparedQuery(Histogram::Unit::Unspecified)),
      computed_quantiles_(supportedQuantiles().size(), 0.0) {}

HistogramStatisticsImpl::HistogramStatisticsImpl(const histogram_t* histogram_ptr,
                                                 Histogram::Unit unit,
                                                 ConstSupportedBuckets& supported_buckets,
                                                 const PreparedSummaryQuery* prepared_query)
    : supported_buckets_(supported_buckets), owned_prepared_query_(nullptr),
      prepared_query_(prepared_query),
      computed_quantiles_(HistogramStatisticsImpl::supportedQuantiles().size(), 0.0), unit_(unit) {
  if (prepared_query_ == nullptr) {
    owned_prepared_query_ = prepareQuery(
        supported_buckets_, unit == Histogram::Unit::Percent ? Histogram::PercentScale : 1.0);
    prepared_query_ = owned_prepared_query_.get();
  }
  refresh(histogram_ptr);
}

const std::vector<double>& HistogramStatisticsImpl::supportedQuantiles() const {
  CONSTRUCT_ON_FIRST_USE(std::vector<double>,
                         {0, 0.25, 0.5, 0.75, 0.90, 0.95, 0.99, 0.995, 0.999, 1});
}

std::vector<uint64_t> HistogramStatisticsImpl::computeDisjointBuckets() const {
  std::vector<uint64_t> buckets;
  buckets.reserve(computed_buckets_.size());
  uint64_t previous_computed_bucket = 0;
  for (uint64_t computed_bucket : computed_buckets_) {
    buckets.push_back(computed_bucket - previous_computed_bucket);
    previous_computed_bucket = computed_bucket;
  }
  return buckets;
}

std::string HistogramStatisticsImpl::quantileSummary() const {
  std::vector<std::string> summary;
  const std::vector<double>& supported_quantiles = supportedQuantiles();
  summary.reserve(supported_quantiles.size());
  for (size_t i = 0; i < supported_quantiles.size(); ++i) {
    summary.push_back(
        fmt::format("P{:g}: {:g}", 100 * supported_quantiles[i], computed_quantiles_[i]));
  }
  return absl::StrJoin(summary, ", ");
}

std::string HistogramStatisticsImpl::bucketSummary() const {
  std::vector<std::string> bucket_summary;
  ConstSupportedBuckets& supported_buckets = supportedBuckets();
  bucket_summary.reserve(supported_buckets.size());
  for (size_t i = 0; i < supported_buckets.size(); ++i) {
    bucket_summary.push_back(fmt::format("B{:g}: {}", supported_buckets[i], computed_buckets_[i]));
  }
  return absl::StrJoin(bucket_summary, ", ");
}

void HistogramStatisticsImpl::reset() {
  std::fill(computed_quantiles_.begin(), computed_quantiles_.end(),
            std::numeric_limits<double>::quiet_NaN());
  computed_buckets_.assign(supported_buckets_.size(), 0);
  sample_count_ = 0;
  sample_sum_ = 0;
  out_of_bound_count_ = 0;
}

/**
 * Clears the old computed values and refreshes it with values computed from passed histogram.
 */
void HistogramStatisticsImpl::refresh(const histogram_t* new_histogram_ptr) {
  // Convert to double once to avoid needing to cast it on every use. Use a double
  // to ensure the compiler doesn't try to convert the expression to integer math.
  constexpr double percent_scale = Histogram::PercentScale;

  std::fill(computed_quantiles_.begin(), computed_quantiles_.end(), 0.0);
  ASSERT(supportedQuantiles().size() == computed_quantiles_.size());
  hist_approx_quantile(new_histogram_ptr, supportedQuantiles().data(), supportedQuantiles().size(),
                       computed_quantiles_.data());
  if (unit_ == Histogram::Unit::Percent) {
    for (double& val : computed_quantiles_) {
      val /= percent_scale;
    }
  }

  ConstSupportedBuckets& supported_buckets = supportedBuckets();
  computed_buckets_.resize(supported_buckets.size());
  ASSERT(prepared_query_ != nullptr);
  const hist_approx_summary_t summary = hist_approx_summary_prepared(
      new_histogram_ptr, prepared_query_, computed_buckets_.data(), &out_of_bound_count_);
  ASSERT(summary.status == 0);
  sample_count_ = summary.sample_count;
  sample_sum_ = summary.sample_sum;
  if (unit_ == Histogram::Unit::Percent) {
    sample_sum_ /= percent_scale;
  }
}

HistogramSettingsImpl::HistogramSettingsImpl(const envoy::config::metrics::v3::StatsConfig& config,
                                             Server::Configuration::CommonFactoryContext& context)
    : configs_([&config, &context]() {
        std::vector<Config> configs;
        for (const auto& matcher : config.histogram_bucket_settings()) {
          std::vector<double> buckets{matcher.buckets().begin(), matcher.buckets().end()};
          std::sort(buckets.begin(), buckets.end());
          Config config_entry{Matchers::StringMatcherImpl(matcher.match(), context),
                              buckets.empty()
                                  ? std::nullopt
                                  : std::make_optional<ConstSupportedBuckets>(std::move(buckets)),
                              nullptr, nullptr, PROTOBUF_GET_OPTIONAL_WRAPPED(matcher, bins)};
          if (config_entry.buckets_.has_value()) {
            config_entry.prepared_query_ = prepareQuery(config_entry.buckets_.value());
            config_entry.prepared_percent_query_ =
                prepareQuery(config_entry.buckets_.value(), Histogram::PercentScale);
          }
          configs.push_back(std::move(config_entry));
        }

        return configs;
      }()) {}

const ConstSupportedBuckets& HistogramSettingsImpl::buckets(absl::string_view stat_name) const {
  for (const auto& config : configs_) {
    if (config.matcher_.match(stat_name) && config.buckets_.has_value()) {
      return config.buckets_.value();
    }
  }
  return defaultBuckets();
}

const PreparedSummaryQuery* HistogramSettingsImpl::preparedQuery(absl::string_view stat_name,
                                                                 Histogram::Unit unit) const {
  for (const auto& config : configs_) {
    if (config.matcher_.match(stat_name) && config.prepared_query_ != nullptr) {
      return unit == Histogram::Unit::Percent ? config.prepared_percent_query_.get()
                                              : config.prepared_query_.get();
    }
  }
  return defaultPreparedQuery(unit);
}

std::optional<uint32_t> HistogramSettingsImpl::bins(absl::string_view stat_name) const {
  for (const auto& config : configs_) {
    if (config.matcher_.match(stat_name) && config.bins_.has_value()) {
      return config.bins_;
    }
  }
  return {};
}

const ConstSupportedBuckets& HistogramSettingsImpl::defaultBuckets() {
  CONSTRUCT_ON_FIRST_USE(ConstSupportedBuckets,
                         {0.5, 1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000, 30000,
                          60000, 300000, 600000, 1800000, 3600000});
}

const PreparedSummaryQuery* HistogramSettingsImpl::defaultPreparedQuery(Histogram::Unit unit) {
  if (unit == Histogram::Unit::Percent) {
    static const PreparedSummaryQueryPtr query =
        prepareQuery(defaultBuckets(), Histogram::PercentScale);
    return query.get();
  }
  static const PreparedSummaryQueryPtr query = prepareQuery(defaultBuckets());
  return query.get();
}

} // namespace Stats
} // namespace Envoy
