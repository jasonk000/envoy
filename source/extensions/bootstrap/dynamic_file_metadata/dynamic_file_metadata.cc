#include "source/extensions/bootstrap/dynamic_file_metadata/dynamic_file_metadata.h"

#include "source/common/config/datasource.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicFileMetadata {
namespace {

// TODO(): have this plumbed to watch other files.
constexpr absl::string_view MetadataPath = "/tmp/dynamic-metadata.yaml";
// 10MB limit should be sufficient for any context changes here.
constexpr uint64_t MaxMetadataSize = 10 * 1000 * 1000;
} // namespace

void DynamicFileMetadata::onServerInitialized(Server::Instance& server) {
  instance_ = &server;
  if (watcher_ == nullptr) {
    watcher_ = server.dispatcher().createFilesystemWatcher();
    auto status =
        watcher_->addWatch(MetadataPath, Envoy::Filesystem::Watcher::Events::Modified,
                           [this](uint32_t) -> absl::Status { return onMetadataFileChanged(); });
    if (!status.ok()) {
      ENVOY_LOG_MISC(warn, "Failed setting up watchers: {}", status);
    }
  }
}

absl::Status DynamicFileMetadata::onMetadataFileChanged() {
  stats_.metadata_file_change_.inc();
  // This should never happen.
  ENVOY_BUG(instance_ != nullptr, "Should be set during server initialization");

  envoy::config::core::v3::DataSource src;
  src.set_filename(MetadataPath);
  auto contents_or = Config::DataSource::read(src, false, instance_->api(), MaxMetadataSize);
  if (!contents_or.ok()) {
    stats_.failed_reading_file_.inc();
    ENVOY_LOG_MISC(warn, "Failed reading metadata file: {}", contents_or.status().message());
    return contents_or.status();
  }

  // TODO(): when adding additional files support additional keys.
  const auto status = instance_->localInfo().contextProvider().setDynamicContextParam(
      "type.googleapis.com/envoy.config.listener.v3.Listener", "yaml", contents_or.value());
  if (status.ok()) {
    stats_.updated_context_param_.inc();
  }
  return status;
}
} // namespace DynamicFileMetadata
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
