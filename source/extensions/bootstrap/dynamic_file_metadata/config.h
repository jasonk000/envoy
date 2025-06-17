#pragma once

#include "envoy/extensions/bootstrap/dynamic_file_metadata/v3/dynamic_file_metadata.pb.validate.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/server/instance.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicFileMetadata {

class DynamicFileMetadataFactory : public Server::Configuration::BootstrapExtensionFactory {
public:
  std::string name() const override { return "envoy.bootstrap.dynamic_file_metadata"; }
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message&,
                           Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::bootstrap::dynamic_file_metadata::v3::DynamicFileMetadata>();
  }
};

} // namespace DynamicFileMetadata
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
