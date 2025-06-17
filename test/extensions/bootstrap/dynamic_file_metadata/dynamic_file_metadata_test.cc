#include "source/extensions/bootstrap/dynamic_file_metadata/config.h"

#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicFileMetadata {

namespace {

using testing::Return;

class DynamicFileMetadataTest : public testing::Test {
public:
  DynamicFileMetadataTest() = default;

protected:
  void setupExtension() {
    auto factory =
        Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::getFactory(
            "envoy.bootstrap.dynamic_file_metadata");
    ASSERT_NE(factory, nullptr);
    extension_ = factory->createBootstrapExtension(config_, server_factory_);
  }
  envoy::extensions::bootstrap::dynamic_file_metadata::v3::DynamicFileMetadata config_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_;
  NiceMock<Server::MockInstance> instance_;
  Server::BootstrapExtensionPtr extension_;
};

TEST_F(DynamicFileMetadataTest, SetsUpWatcherOnInitialization) {
  setupExtension();

  auto* watcher = new Filesystem::MockWatcher();
  Filesystem::Watcher::OnChangedCb metadata_change_cb;
  EXPECT_CALL(instance_.dispatcher_, createFilesystemWatcher_()).WillOnce(Return(watcher));
  EXPECT_CALL(*watcher, addWatch(_, _, _))
      .WillOnce(Invoke(
          [&metadata_change_cb](absl::string_view, uint32_t, Filesystem::Watcher::OnChangedCb cb) {
            metadata_change_cb = cb;
            return absl::OkStatus();
          }));
  extension_->onServerInitialized(instance_);
}

TEST_F(DynamicFileMetadataTest, WarnsIfFailsToSetupWatcherOnInitialization) {
  setupExtension();

  auto* watcher = new Filesystem::MockWatcher();
  EXPECT_CALL(instance_.dispatcher_, createFilesystemWatcher_()).WillOnce(Return(watcher));
  EXPECT_CALL(*watcher, addWatch(_, _, _))
      .WillOnce(Invoke([](absl::string_view, uint32_t, Filesystem::Watcher::OnChangedCb) {
        return absl::NotFoundError("Missing file to set watch on");
      }));
  EXPECT_LOG_CONTAINS("warning", "Failed setting up watchers",
                      extension_->onServerInitialized(instance_));
}

TEST_F(DynamicFileMetadataTest, CanReadAndUpdateMetadata) {
  setupExtension();

  auto* watcher = new Filesystem::MockWatcher();
  Filesystem::Watcher::OnChangedCb metadata_change_cb;
  EXPECT_CALL(instance_.dispatcher_, createFilesystemWatcher_()).WillOnce(Return(watcher));
  EXPECT_CALL(*watcher, addWatch(_, _, _))
      .WillOnce(Invoke(
          [&metadata_change_cb](absl::string_view, uint32_t, Filesystem::Watcher::OnChangedCb cb) {
            metadata_change_cb = cb;
            return absl::OkStatus();
          }));
  extension_->onServerInitialized(instance_);

  // Signal Metadata change.
  EXPECT_CALL(instance_.api_.file_system_, fileExists(_)).WillOnce(Return(true));
  EXPECT_CALL(instance_.api_.file_system_, fileSize(_)).WillOnce(Return(1));
  EXPECT_CALL(instance_.api_.file_system_, fileReadToEnd(_)).WillOnce(Return("a"));
  EXPECT_CALL(instance_.local_info_.context_provider_, setDynamicContextParam(_, "yaml", "a"));
  EXPECT_OK(metadata_change_cb(0));

  EXPECT_EQ(1,
            server_factory_.store_.counter("dynamic_file_metadata.metadata_file_change").value());
  EXPECT_EQ(1,
            server_factory_.store_.counter("dynamic_file_metadata.updated_context_param").value());
  EXPECT_EQ(0, server_factory_.store_.counter("dynamic_file_metadata.failed_reading_file").value());
}

TEST_F(DynamicFileMetadataTest, ShouldGracefullyHandleFileReadErrors) {
  setupExtension();

  auto* watcher = new Filesystem::MockWatcher();
  Filesystem::Watcher::OnChangedCb metadata_change_cb;
  EXPECT_CALL(instance_.dispatcher_, createFilesystemWatcher_()).WillOnce(Return(watcher));
  EXPECT_CALL(*watcher, addWatch(_, _, _))
      .WillOnce(Invoke(
          [&metadata_change_cb](absl::string_view, uint32_t, Filesystem::Watcher::OnChangedCb cb) {
            metadata_change_cb = cb;
            return absl::OkStatus();
          }));
  extension_->onServerInitialized(instance_);

  EXPECT_CALL(instance_.api_.file_system_, fileExists(_)).WillOnce(Return(true));
  // Have the file size be beyond the limit.
  EXPECT_CALL(instance_.api_.file_system_, fileSize(_)).WillOnce(Return(1000 * 1000 * 1000));
  EXPECT_THAT(metadata_change_cb(0),
              StatusHelpers::HasStatusCode(absl::StatusCode::kInvalidArgument));

  EXPECT_EQ(1,
            server_factory_.store_.counter("dynamic_file_metadata.metadata_file_change").value());
  EXPECT_EQ(0,
            server_factory_.store_.counter("dynamic_file_metadata.updated_context_param").value());
  EXPECT_EQ(1, server_factory_.store_.counter("dynamic_file_metadata.failed_reading_file").value());
}

TEST_F(DynamicFileMetadataTest, CanUpdateContextMultipleTimes) {
  setupExtension();

  auto* watcher = new Filesystem::MockWatcher();
  Filesystem::Watcher::OnChangedCb metadata_change_cb;
  EXPECT_CALL(instance_.dispatcher_, createFilesystemWatcher_()).WillOnce(Return(watcher));
  EXPECT_CALL(*watcher, addWatch(_, _, _))
      .WillOnce(Invoke(
          [&metadata_change_cb](absl::string_view, uint32_t, Filesystem::Watcher::OnChangedCb cb) {
            metadata_change_cb = cb;
            return absl::OkStatus();
          }));
  extension_->onServerInitialized(instance_);

  // Signal Metadata change.
  EXPECT_CALL(instance_.api_.file_system_, fileExists(_)).WillOnce(Return(true));
  EXPECT_CALL(instance_.api_.file_system_, fileSize(_)).WillOnce(Return(1));
  EXPECT_CALL(instance_.api_.file_system_, fileReadToEnd(_)).WillOnce(Return("a"));
  EXPECT_CALL(instance_.local_info_.context_provider_, setDynamicContextParam(_, "yaml", "a"));
  EXPECT_OK(metadata_change_cb(0));

  EXPECT_EQ(1,
            server_factory_.store_.counter("dynamic_file_metadata.metadata_file_change").value());
  EXPECT_EQ(1,
            server_factory_.store_.counter("dynamic_file_metadata.updated_context_param").value());
  EXPECT_EQ(0, server_factory_.store_.counter("dynamic_file_metadata.failed_reading_file").value());

  // Signal Metadata change.
  EXPECT_CALL(instance_.api_.file_system_, fileExists(_)).WillOnce(Return(true));
  EXPECT_CALL(instance_.api_.file_system_, fileSize(_)).WillOnce(Return(1));
  EXPECT_CALL(instance_.api_.file_system_, fileReadToEnd(_)).WillOnce(Return("b"));
  EXPECT_CALL(instance_.local_info_.context_provider_, setDynamicContextParam(_, "yaml", "b"));
  EXPECT_OK(metadata_change_cb(0));

  EXPECT_EQ(2,
            server_factory_.store_.counter("dynamic_file_metadata.metadata_file_change").value());
  EXPECT_EQ(2,
            server_factory_.store_.counter("dynamic_file_metadata.updated_context_param").value());
  EXPECT_EQ(0, server_factory_.store_.counter("dynamic_file_metadata.failed_reading_file").value());
}

TEST_F(DynamicFileMetadataTest, CanHandleErrorsWhenContextParamFailsToSet) {
  setupExtension();

  auto* watcher = new Filesystem::MockWatcher();
  Filesystem::Watcher::OnChangedCb metadata_change_cb;
  EXPECT_CALL(instance_.dispatcher_, createFilesystemWatcher_()).WillOnce(Return(watcher));
  EXPECT_CALL(*watcher, addWatch(_, _, _))
      .WillOnce(Invoke(
          [&metadata_change_cb](absl::string_view, uint32_t, Filesystem::Watcher::OnChangedCb cb) {
            metadata_change_cb = cb;
            return absl::OkStatus();
          }));
  extension_->onServerInitialized(instance_);

  // Signal Metadata change.
  EXPECT_CALL(instance_.api_.file_system_, fileExists(_)).WillOnce(Return(true));
  EXPECT_CALL(instance_.api_.file_system_, fileSize(_)).WillOnce(Return(1));
  EXPECT_CALL(instance_.api_.file_system_, fileReadToEnd(_)).WillOnce(Return("a"));
  EXPECT_CALL(instance_.local_info_.context_provider_, setDynamicContextParam(_, "yaml", "a"))
      .WillOnce(Return(absl::InternalError("Context update failed")));

  EXPECT_THAT(metadata_change_cb(0), StatusHelpers::HasStatusCode(absl::StatusCode::kInternal));

  EXPECT_EQ(1,
            server_factory_.store_.counter("dynamic_file_metadata.metadata_file_change").value());
  // Update failed.
  EXPECT_EQ(0,
            server_factory_.store_.counter("dynamic_file_metadata.updated_context_param").value());
  EXPECT_EQ(0, server_factory_.store_.counter("dynamic_file_metadata.failed_reading_file").value());
}

} // namespace
} // namespace DynamicFileMetadata
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
