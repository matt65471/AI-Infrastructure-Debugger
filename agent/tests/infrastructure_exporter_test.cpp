#include "infrastructure_exporter.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}  // namespace

int main() {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    const auto root = std::filesystem::temp_directory_path() /
        ("infrastructure-exporter-test-" + unique);
    const auto spool = root / "spool";
    const auto token = root / "token";
    std::filesystem::create_directories(root);
    {
        std::ofstream file(token);
        file << "test-token\n";
    }

    InfrastructureExporterOptions options;
    options.ingest_url = "http://127.0.0.1:1/v1/infra-snapshots";
    options.token_file = token;
    options.spool_directory = spool;
    options.spool_max_bytes = 1024;
    InfrastructureExporter exporter(options);
    exporter.enqueue("{\"node\":{}}", 1000);
    exporter.enqueue("{\"node\":{}}", 2000);
    require(exporter.pending_count() == 2, "snapshots should be queued atomically");

    InfrastructureExporterOptions bounded_options = options;
    bounded_options.spool_directory = root / "bounded";
    bounded_options.spool_max_bytes = 20;
    InfrastructureExporter bounded(bounded_options);
    bounded.enqueue("{\"snapshot\":1}", 1000);
    bounded.enqueue("{\"snapshot\":2}", 2000);
    require(bounded.pending_count() == 1, "spool limit should discard the oldest file");

    InfrastructureExporter restarted(options);
    require(restarted.pending_count() == 2, "queued snapshots should survive exporter restart");

    std::filesystem::remove_all(root);
    std::cout << "infrastructure exporter tests passed\n";
    return 0;
}
