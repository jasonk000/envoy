load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    deps = [
        ":dynamic_file_metadata_lib",
        "//envoy/registry",
        "//envoy/server:bootstrap_extension_config_interface",
        "//envoy/server:instance_interface",
        "@envoy_api//envoy/extensions/bootstrap/dynamic_file_metadata/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "dynamic_file_metadata_lib",
    srcs = [
        "dynamic_file_metadata.cc",
    ],
    hdrs = [
        "dynamic_file_metadata.h",
    ],
    deps = [
        "//envoy/server:bootstrap_extension_config_interface",
        "//envoy/server:instance_interface",
        "//source/common/config:datasource_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/bootstrap/dynamic_file_metadata/v3:pkg_cc_proto",
    ],
)
