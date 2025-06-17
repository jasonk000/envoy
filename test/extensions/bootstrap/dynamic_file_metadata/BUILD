load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "dynamic_file_metadata_test",
    srcs = ["dynamic_file_metadata_test.cc"],
    extension_names = ["envoy.bootstrap.dynamic_file_metadata"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/bootstrap/dynamic_file_metadata:config",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:status_utility_lib",
    ],
)
