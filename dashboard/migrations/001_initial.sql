CREATE SCHEMA IF NOT EXISTS telemetry;

CREATE TABLE IF NOT EXISTS telemetry.resources (
    id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    resource_key uuid NOT NULL UNIQUE,
    kind smallint NOT NULL CHECK (kind BETWEEN 1 AND 5),
    service_name text,
    node_name text,
    namespace_name text,
    deployment_name text,
    kubernetes_uid text,
    pod_uid text,
    pod_name text,
    container_id text,
    container_name text,
    parent_resource_id bigint REFERENCES telemetry.resources(id),
    first_seen_at timestamptz NOT NULL,
    last_seen_at timestamptz NOT NULL,
    attributes jsonb NOT NULL DEFAULT '{}'::jsonb
);

CREATE INDEX IF NOT EXISTS resources_pod_uid_idx
    ON telemetry.resources (pod_uid);
CREATE INDEX IF NOT EXISTS resources_container_id_idx
    ON telemetry.resources (container_id);
CREATE INDEX IF NOT EXISTS resources_deployment_idx
    ON telemetry.resources (namespace_name, deployment_name);

CREATE TABLE IF NOT EXISTS telemetry.spans (
    started_at timestamptz NOT NULL,
    trace_id bytea NOT NULL CHECK (octet_length(trace_id) = 16),
    span_id bytea NOT NULL CHECK (octet_length(span_id) = 8),
    parent_span_id bytea,
    resource_id bigint NOT NULL REFERENCES telemetry.resources(id),
    operation_name text NOT NULL,
    span_kind smallint NOT NULL,
    duration_us bigint NOT NULL CHECK (duration_us >= 0),
    status_code smallint NOT NULL,
    http_method text,
    http_route text,
    http_status_code smallint,
    error_type text,
    attributes jsonb NOT NULL DEFAULT '{}'::jsonb,
    PRIMARY KEY (started_at, trace_id, span_id)
) PARTITION BY RANGE (started_at);

CREATE INDEX IF NOT EXISTS spans_trace_id_idx
    ON telemetry.spans (trace_id);
CREATE INDEX IF NOT EXISTS spans_resource_time_idx
    ON telemetry.spans (resource_id, started_at DESC);
CREATE TABLE IF NOT EXISTS telemetry.infra_samples (
    sampled_at timestamptz NOT NULL,
    resource_id bigint NOT NULL REFERENCES telemetry.resources(id),
    cpu_usage_percent real,
    cpu_limit_cores real,
    cpu_throttled_usec_delta bigint,
    memory_current_bytes bigint,
    memory_limit_bytes bigint,
    memory_usage_percent real,
    memory_high_delta integer,
    oom_kill_delta integer,
    network_rx_bytes_per_second bigint,
    network_tx_bytes_per_second bigint,
    tcp_retransmits_delta integer,
    disk_read_bytes_per_second bigint,
    disk_write_bytes_per_second bigint,
    cpu_pressure_avg10 real,
    memory_pressure_full_avg10 real,
    io_pressure_full_avg10 real,
    restart_count integer,
    ready boolean,
    extra_metrics jsonb NOT NULL DEFAULT '{}'::jsonb,
    PRIMARY KEY (sampled_at, resource_id)
) PARTITION BY RANGE (sampled_at);

CREATE INDEX IF NOT EXISTS infra_samples_resource_time_idx
    ON telemetry.infra_samples (resource_id, sampled_at DESC);

CREATE TABLE IF NOT EXISTS telemetry.events (
    id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    event_key text NOT NULL UNIQUE,
    first_seen_at timestamptz NOT NULL,
    last_seen_at timestamptz NOT NULL,
    resource_id bigint REFERENCES telemetry.resources(id),
    source smallint NOT NULL,
    severity smallint NOT NULL,
    reason text NOT NULL,
    message text,
    occurrence_count integer NOT NULL DEFAULT 1,
    trace_id bytea,
    details jsonb NOT NULL DEFAULT '{}'::jsonb
);

CREATE INDEX IF NOT EXISTS events_recent_idx
    ON telemetry.events (last_seen_at DESC);
CREATE INDEX IF NOT EXISTS events_resource_time_idx
    ON telemetry.events (resource_id, last_seen_at DESC);
CREATE INDEX IF NOT EXISTS events_trace_idx
    ON telemetry.events (trace_id) WHERE trace_id IS NOT NULL;

CREATE TABLE IF NOT EXISTS telemetry.service_rollups_1m (
    bucket timestamptz NOT NULL,
    namespace_name text NOT NULL,
    deployment_name text NOT NULL,
    service_name text NOT NULL,
    http_route text NOT NULL DEFAULT '',
    request_count bigint NOT NULL DEFAULT 0,
    error_count bigint NOT NULL DEFAULT 0,
    average_latency_ms real,
    p50_latency_ms real,
    p95_latency_ms real,
    p99_latency_ms real,
    maximum_latency_ms real,
    average_cpu_percent real,
    maximum_cpu_percent real,
    average_memory_bytes bigint,
    maximum_memory_bytes bigint,
    throttled_usec bigint,
    oom_kill_count integer,
    restart_count integer,
    PRIMARY KEY (
        bucket,
        namespace_name,
        deployment_name,
        service_name,
        http_route
    )
);

CREATE INDEX IF NOT EXISTS service_rollups_bucket_idx
    ON telemetry.service_rollups_1m (bucket DESC);
CREATE INDEX IF NOT EXISTS service_rollups_deployment_idx
    ON telemetry.service_rollups_1m (
        namespace_name,
        deployment_name,
        bucket DESC
    );
