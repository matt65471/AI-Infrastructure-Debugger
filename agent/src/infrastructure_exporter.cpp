#include "infrastructure_exporter.h"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kMaximumBatchSnapshots = 10;
constexpr std::size_t kMaximumBatchBytes = 8 * 1024 * 1024;

std::size_t discard_response(char*, std::size_t size, std::size_t count, void*) {
    return size * count;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("failed to read " + path.string());
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    std::string value = stream.str();
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

std::string read_machine_id() {
    try {
        const std::string value = read_text("/etc/machine-id");
        if (!value.empty()) {
            return value;
        }
    } catch (const std::exception&) {
    }
    return "unknown-host";
}

std::string json_string(const std::string& value) {
    std::ostringstream stream;
    stream << '"';
    for (const char character : value) {
        switch (character) {
            case '"': stream << "\\\""; break;
            case '\\': stream << "\\\\"; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default: stream << character;
        }
    }
    stream << '"';
    return stream.str();
}

}  // namespace

InfrastructureExporter::InfrastructureExporter(InfrastructureExporterOptions options)
    : options_(std::move(options)),
      token_(read_text(options_.token_file)),
      collector_id_(read_machine_id()) {
    if (options_.ingest_url.empty() || token_.empty() || options_.spool_directory.empty()) {
        throw std::runtime_error("ingest URL, token file, and spool directory are required");
    }
    std::filesystem::create_directories(options_.spool_directory);
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void InfrastructureExporter::enqueue(
    const std::string& snapshot_json,
    std::int64_t timestamp_unix_ms
) {
    const std::lock_guard<std::mutex> lock(spool_mutex_);
    static std::atomic<unsigned long long> sequence{0};
    const std::string name = std::to_string(timestamp_unix_ms) + "-" +
        std::to_string(sequence.fetch_add(1)) + ".json";
    const auto destination = options_.spool_directory / name;
    const auto temporary = destination.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file.is_open()) {
            throw std::runtime_error("failed to create spool file " + temporary);
        }
        file << snapshot_json << '\n';
        if (!file) {
            throw std::runtime_error("failed to write spool file " + temporary);
        }
    }
    std::filesystem::rename(temporary, destination);
    enforce_spool_limit();
}

std::vector<std::filesystem::path> InfrastructureExporter::pending_files() const {
    const std::lock_guard<std::mutex> lock(spool_mutex_);
    return pending_files_unlocked();
}

std::vector<std::filesystem::path> InfrastructureExporter::pending_files_unlocked() const {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(options_.spool_directory)) {
        return files;
    }
    for (const auto& entry : std::filesystem::directory_iterator(options_.spool_directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::size_t InfrastructureExporter::pending_count() const {
    return pending_files().size();
}

std::string InfrastructureExporter::build_payload(
    const std::vector<std::filesystem::path>& files
) const {
    std::ostringstream payload;
    payload << "{\"schema_version\":1,\"collector_id\":"
            << json_string(collector_id_) << ",\"snapshots\":[";
    for (std::size_t index = 0; index < files.size(); ++index) {
        if (index) {
            payload << ',';
        }
        payload << read_text(files[index]);
    }
    payload << "]}";
    return payload.str();
}

InfrastructureExporter::SendResult InfrastructureExporter::send(
    const std::string& payload
) const {
    CURL* handle = curl_easy_init();
    if (!handle) {
        return SendResult::retry;
    }
    struct curl_slist* headers = nullptr;
    const std::string authorization = "Authorization: Bearer " + token_;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, authorization.c_str());
    curl_easy_setopt(handle, CURLOPT_URL, options_.ingest_url.c_str());
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, payload.data());
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(payload.size()));
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, 5000L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, discard_response);
    const CURLcode result = curl_easy_perform(handle);
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
    if (result != CURLE_OK) {
        std::cerr << "infrastructure export failed: " << curl_easy_strerror(result) << '\n';
        return SendResult::retry;
    }
    if (status >= 200 && status < 300) {
        return SendResult::accepted;
    }
    if (status == 400 || status == 413 || status == 422) {
        std::cerr << "infrastructure batch rejected with HTTP " << status << '\n';
        return SendResult::rejected;
    }
    std::cerr << "infrastructure export retrying after HTTP " << status << '\n';
    return SendResult::retry;
}

void InfrastructureExporter::enforce_spool_limit() {
    auto files = pending_files_unlocked();
    std::uintmax_t total = 0;
    for (const auto& file : files) {
        total += std::filesystem::file_size(file);
    }
    while (total > options_.spool_max_bytes && !files.empty()) {
        const auto oldest = files.front();
        const auto bytes = std::filesystem::file_size(oldest);
        std::filesystem::remove(oldest);
        files.erase(files.begin());
        total = total > bytes ? total - bytes : 0;
        std::cerr << "WARNING: telemetry spool full; dropped oldest snapshot "
                  << oldest.filename().string() << '\n';
    }
}

void InfrastructureExporter::run(const std::atomic<bool>& stop_requested) {
    unsigned int failures = 0;
    std::mt19937 random(std::random_device{}());
    while (!stop_requested.load()) {
        std::vector<std::filesystem::path> batch;
        std::string payload;
        try {
            const std::lock_guard<std::mutex> lock(spool_mutex_);
            auto files = pending_files_unlocked();
            std::size_t bytes = 0;
            for (const auto& file : files) {
                const auto file_bytes = static_cast<std::size_t>(std::filesystem::file_size(file));
                if (!batch.empty() && (batch.size() >= kMaximumBatchSnapshots || bytes + file_bytes > kMaximumBatchBytes)) {
                    break;
                }
                batch.push_back(file);
                bytes += file_bytes;
            }
            if (!batch.empty()) {
                payload = build_payload(batch);
            }
        } catch (const std::exception& error) {
            std::cerr << "failed to prepare infrastructure batch: " << error.what() << '\n';
        }
        if (batch.empty() || payload.empty()) {
            failures = 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        const SendResult result = send(payload);
        if (result == SendResult::accepted || result == SendResult::rejected) {
            const std::lock_guard<std::mutex> lock(spool_mutex_);
            for (const auto& file : batch) {
                std::error_code ignored;
                std::filesystem::remove(file, ignored);
            }
            failures = 0;
            continue;
        }
        failures = std::min(failures + 1, 5U);
        const unsigned int base_seconds = std::min(1U << (failures - 1), 30U);
        std::uniform_int_distribution<unsigned int> jitter(0, 500);
        const auto delay = std::chrono::milliseconds(base_seconds * 1000 + jitter(random));
        const auto deadline = std::chrono::steady_clock::now() + delay;
        while (!stop_requested.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
