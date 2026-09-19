#ifndef INFRASTRUCTURE_EXPORTER_H
#define INFRASTRUCTURE_EXPORTER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

struct InfrastructureExporterOptions {
    std::string ingest_url;
    std::filesystem::path token_file;
    std::filesystem::path spool_directory;
    std::uintmax_t spool_max_bytes = 268435456;
};

class InfrastructureExporter {
public:
    explicit InfrastructureExporter(InfrastructureExporterOptions options);

    void enqueue(const std::string& snapshot_json, std::int64_t timestamp_unix_ms);
    void run(const std::atomic<bool>& stop_requested);
    std::size_t pending_count() const;

private:
    enum class SendResult { accepted, rejected, retry };

    InfrastructureExporterOptions options_;
    std::string token_;
    std::string collector_id_;
    mutable std::mutex spool_mutex_;

    std::string build_payload(const std::vector<std::filesystem::path>& files) const;
    std::vector<std::filesystem::path> pending_files() const;
    std::vector<std::filesystem::path> pending_files_unlocked() const;
    SendResult send(const std::string& payload) const;
    void enforce_spool_limit();
};

#endif
