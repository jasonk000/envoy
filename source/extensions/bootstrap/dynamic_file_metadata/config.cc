#include "source/extensions/bootstrap/dynamic_file_metadata/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/bootstrap/dynamic_file_metadata/dynamic_file_metadata.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicFileMetadata {

Server::BootstrapExtensionPtr DynamicFileMetadataFactory::createBootstrapExtension(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  const auto& message = MessageUtil::downcastAndValidate<
      const envoy::extensions::bootstrap::dynamic_file_metadata::v3::DynamicFileMetadata&>(
      config, context.messageValidationVisitor());
  return std::make_unique<DynamicFileMetadata>(context, message);
}

//
// Static registration for the DynamicFileMetadataFactory. @see RegistryFactory.
//
REGISTER_FACTORY(DynamicFileMetadataFactory, Server::Configuration::BootstrapExtensionFactory);

} // namespace DynamicFileMetadata
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
