// Benchmarks the main-thread ParentHistogramImpl::merge() path. The benchmark intentionally uses
// TLS histogram objects created on one thread: worker-thread scheduling and synchronization are
// outside the scope of this benchmark.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <utility>
#include <vector>

#include "envoy/stats/histogram.h"

#include "source/common/stats/allocator.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/thread_local_store.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace {

enum class ValuePattern { Overlapping, Disjoint };
enum class AllocationPattern { Packed, Scattered };

class ParentHistogramMergeBenchmark {
public:
  ParentHistogramMergeBenchmark(size_t source_count, size_t non_empty_sources,
                                size_t values_per_source, ValuePattern value_pattern,
                                AllocationPattern allocation_pattern, bool production_layout = false)
      : symbol_table_(), allocator_(symbol_table_), store_(allocator_),
        values_per_source_(values_per_source), production_layout_(production_layout),
        value_pattern_(value_pattern) {
    auto& histogram = store_.rootScope()->histogramFromString(
        "merge_histogram", Stats::Histogram::Unit::Unspecified);
    parent_ = &static_cast<Stats::ParentHistogramImpl&>(histogram);

    tls_histograms_.reserve(source_count);
    for (size_t i = 0; i < source_count; ++i) {
      if (allocation_pattern == AllocationPattern::Scattered) {
        allocateNoise();
      }
      Stats::TlsHistogramSharedPtr tls_histogram(new Stats::ThreadLocalHistogramImpl(
          parent_->statName(), parent_->unit(), parent_->statName(), Stats::StatNameTagSpan{},
          symbol_table_, parent_->bins()));
      parent_->addTlsHistogram(tls_histogram);
      tls_histograms_.push_back(std::move(tls_histogram));
    }

    non_empty_sources_ = std::min(non_empty_sources, source_count);
    if (production_layout_) {
      // Model 154K production histograms: 75% have one value, 24% have eight, and 1% has 16.
      production_value_counts_.resize(source_count);
      for (size_t i = 0; i < source_count; ++i) {
        if (i < source_count * 75 / 100) {
          production_value_counts_[i] = 1;
        } else if (i < source_count * 99 / 100) {
          production_value_counts_[i] = 8;
        } else {
          production_value_counts_[i] = 16;
        }
      }
      std::mt19937 random(0x5eed);
      std::shuffle(production_value_counts_.begin(), production_value_counts_.end(), random);
    }

    // Prime the parent so that subsequent empty intervals still execute merge().
    if (!tls_histograms_.empty()) {
      tls_histograms_[0]->recordValue(1);
      for (const auto& tls_histogram : tls_histograms_) {
        tls_histogram->beginMerge();
      }
      parent_->merge();
    }
  }

  void prepareInterval() {
    for (size_t i = 0; i < tls_histograms_.size(); ++i) {
      const size_t value_count = production_layout_
                                     ? production_value_counts_[i]
                                     : i < non_empty_sources_ ? values_per_source_ : 0;
      if (value_count != 0) {
        // recordValue() uses hist_insert_intscale(..., scale=0). Circllhist normalizes these
        // values to two significant digits plus an exponent, so the 100-wide steps below map to
        // distinct buckets for the ranges used by this benchmark. Disjoint sources are separated
        // by 10000, which changes the exponent and keeps their buckets distinct.
        for (size_t value_index = 0; value_index < value_count; ++value_index) {
          const uint64_t source_offset = value_pattern_ == ValuePattern::Disjoint ? i * 10000 : 0;
          tls_histograms_[i]->recordValue(1000 + source_offset + value_index * 100);
        }
      }
      tls_histograms_[i]->beginMerge();
    }
  }

  void merge() { parent_->merge(); }

private:
  Stats::SymbolTableImpl symbol_table_;
  Stats::Allocator allocator_;
  Stats::ThreadLocalStoreImpl store_;
  Stats::ParentHistogramImpl* parent_{nullptr};
  std::vector<Stats::TlsHistogramSharedPtr> tls_histograms_;
  size_t non_empty_sources_{0};
  size_t values_per_source_{0};
  bool production_layout_{false};
  std::vector<size_t> production_value_counts_;
  void allocateNoise() {
    const size_t noise_size = tls_histograms_.size() < 1000 ? 64 * 1024 : 4 * 1024;
    auto noise = std::make_unique<std::byte[]>(noise_size);
    for (size_t offset = 0; offset < noise_size; offset += 4096) {
      noise[offset] = std::byte{1};
    }
    allocation_noise_.push_back(std::move(noise));
  }

  ValuePattern value_pattern_{ValuePattern::Disjoint};
  std::vector<std::unique_ptr<std::byte[]>> allocation_noise_;
};

void BM_ParentHistogramMerge(benchmark::State& state, ValuePattern value_pattern,
                             AllocationPattern allocation_pattern) {
  const size_t source_count = state.range(0);
  const size_t non_empty_sources = state.range(1);
  const size_t values_per_source = state.range(2);
  ParentHistogramMergeBenchmark benchmark(source_count, non_empty_sources, values_per_source,
                                          value_pattern, allocation_pattern);

  for (auto _ : state) {
    state.PauseTiming();
    benchmark.prepareInterval();
    state.ResumeTiming();
    benchmark.merge();
  }
}

BENCHMARK_CAPTURE(BM_ParentHistogramMerge, Overlapping, ValuePattern::Overlapping,
                  AllocationPattern::Packed)
    ->Args({8, 0, 1})
    ->Args({32, 0, 1})
    ->Args({64, 0, 1})
    ->Args({96, 0, 1})
    ->Args({8, 1, 1})
    ->Args({32, 1, 1})
    ->Args({64, 1, 1})
    ->Args({96, 1, 1})
    ->Args({32, 2, 4})
    ->Args({64, 8, 4})
    ->Args({96, 32, 4})
    ->Args({32, 32, 16})
    ->Args({64, 64, 16});

BENCHMARK_CAPTURE(BM_ParentHistogramMerge, Disjoint, ValuePattern::Disjoint,
                  AllocationPattern::Packed)
    ->Args({8, 0, 1})
    ->Args({32, 0, 1})
    ->Args({64, 0, 1})
    ->Args({96, 0, 1})
    ->Args({8, 1, 1})
    ->Args({32, 1, 1})
    ->Args({64, 1, 1})
    ->Args({96, 1, 1})
    ->Args({32, 2, 4})
    ->Args({64, 8, 4})
    ->Args({96, 32, 4})
    ->Args({32, 32, 16})
    ->Args({64, 64, 16});

BENCHMARK_CAPTURE(BM_ParentHistogramMerge, ScatteredOverlapping,
                  ValuePattern::Overlapping, AllocationPattern::Scattered)
    ->Args({32, 0, 1})
    ->Args({64, 0, 1})
    ->Args({96, 0, 1})
    ->Args({32, 1, 1})
    ->Args({64, 1, 1})
    ->Args({96, 1, 1})
    ->Args({64, 8, 4})
    ->Args({96, 32, 4})
    ->Args({5000, 0, 1})
    ->Args({5000, 1, 1});

BENCHMARK_CAPTURE(BM_ParentHistogramMerge, ScatteredDisjoint,
                  ValuePattern::Disjoint, AllocationPattern::Scattered)
    ->Args({32, 0, 1})
    ->Args({64, 0, 1})
    ->Args({96, 0, 1})
    ->Args({32, 1, 1})
    ->Args({64, 1, 1})
    ->Args({96, 1, 1})
    ->Args({64, 8, 4})
    ->Args({96, 32, 4})
    ->Args({5000, 0, 1})
    ->Args({5000, 1, 1});

void BM_ParentHistogramMergeProduction(benchmark::State& state) {
  ParentHistogramMergeBenchmark benchmark(state.range(0), 0, 0, ValuePattern::Overlapping,
                                          AllocationPattern::Scattered, true);

  for (auto _ : state) {
    state.PauseTiming();
    benchmark.prepareInterval();
    state.ResumeTiming();
    benchmark.merge();
  }
}

BENCHMARK(BM_ParentHistogramMergeProduction)->Arg(154000);

} // namespace
} // namespace Envoy
