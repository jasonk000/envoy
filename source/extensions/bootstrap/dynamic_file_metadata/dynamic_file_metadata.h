#pragma once

#include "envoy/extensions/bootstrap/dynamic_file_metadata/v3/dynamic_file_metadata.pb.validate.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/server/instance.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicFileMetadata {

// TODO(): add more stats
#define DYNAMIC_FILE_METADATA_STATS(COUNTER)                                                       \
  COUNTER(metadata_file_change)                                                                    \
  COUNTER(failed_reading_file)                                                                     \
  COUNTER(updated_context_param)

struct DynamicFileMetadataStats {
  DYNAMIC_FILE_METADATA_STATS(GENERATE_COUNTER_STRUCT);
};

class DynamicFileMetadata : public Server::BootstrapExtension {
public:
  // TODO(): use the config to provide multiple paths
  DynamicFileMetadata(Server::Configuration::ServerFactoryContext& context,
                      const envoy::extensions::bootstrap::dynamic_file_metadata::v3::
                          DynamicFileMetadata& /*config*/)
      : scope_(context.scope().createScope("dynamic_file_metadata.")),
        stats_{DYNAMIC_FILE_METADATA_STATS(POOL_COUNTER(*scope_))} {}
  void onServerInitialized(Server::Instance& server) override;
  void onWorkerThreadInitialized() override {}
  // Bridges the file changes to the node's context parameters.
  absl::Status onMetadataFileChanged();

private:
  Stats::ScopeSharedPtr scope_;
  DynamicFileMetadataStats stats_;

  Server::Instance* instance_{nullptr};
  // TODO(): support multiple watchers
  Filesystem::WatcherPtr watcher_;
};

} // namespace DynamicFileMetadata
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
