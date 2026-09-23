ALTER TABLE telemetry.fault_experiments
    DROP CONSTRAINT IF EXISTS fault_experiments_status_check;

UPDATE telemetry.fault_experiments
SET status = 'active'
WHERE status = 'injecting';

ALTER TABLE telemetry.fault_experiments
    RENAME COLUMN injected_at TO active_started_at;
ALTER TABLE telemetry.fault_experiments
    RENAME COLUMN fault_ended_at TO active_ended_at;

ALTER TABLE telemetry.fault_experiments
    ADD CONSTRAINT fault_experiments_status_check CHECK (
        status IN (
            'baseline',
            'active',
            'recovering',
            'completed',
            'failed',
            'recovery_failed'
        )
    );

DROP INDEX IF EXISTS telemetry.fault_experiments_status_idx;

UPDATE telemetry.fault_experiments
SET status = 'failed',
    failed_at = COALESCE(failed_at, now()),
    error_message = COALESCE(error_message, 'experiment expired before migration'),
    updated_at = now()
WHERE status IN ('baseline', 'active', 'recovering')
  AND expires_at < now();

WITH ranked_active AS (
    SELECT id,
           row_number() OVER (ORDER BY created_at DESC, id DESC) AS position
    FROM telemetry.fault_experiments
    WHERE status IN ('baseline', 'active', 'recovering')
)
UPDATE telemetry.fault_experiments experiment
SET status = 'failed',
    failed_at = COALESCE(experiment.failed_at, now()),
    error_message = COALESCE(
        experiment.error_message,
        'overlapping experiment closed while enabling single-run enforcement'
    ),
    updated_at = now()
FROM ranked_active
WHERE experiment.id = ranked_active.id
  AND ranked_active.position > 1;

CREATE INDEX fault_experiments_status_idx
    ON telemetry.fault_experiments (status, expires_at)
    WHERE status IN ('baseline', 'active', 'recovering');

CREATE UNIQUE INDEX fault_experiments_single_active_idx
    ON telemetry.fault_experiments ((1))
    WHERE status IN ('baseline', 'active', 'recovering');

CREATE TABLE telemetry.experiment_feature_builds (
    experiment_id uuid NOT NULL
        REFERENCES telemetry.fault_experiments(id) ON DELETE CASCADE,
    feature_version smallint NOT NULL CHECK (feature_version > 0),
    status text NOT NULL CHECK (status IN ('building', 'completed', 'failed')),
    started_at timestamptz NOT NULL,
    completed_at timestamptz,
    attempt_count integer NOT NULL DEFAULT 1 CHECK (attempt_count > 0),
    row_count integer NOT NULL DEFAULT 0 CHECK (row_count >= 0),
    error_message text,
    PRIMARY KEY (experiment_id, feature_version)
);

CREATE TABLE telemetry.experiment_feature_buckets (
    experiment_id uuid NOT NULL,
    feature_version smallint NOT NULL,
    phase text NOT NULL CHECK (phase IN ('baseline', 'active', 'recovery')),
    bucket_start timestamptz NOT NULL,
    phase_bucket_index integer NOT NULL CHECK (phase_bucket_index >= 0),
    sequence_index integer NOT NULL CHECK (sequence_index >= 0),
    resource_id bigint NOT NULL REFERENCES telemetry.resources(id),
    resource_kind smallint NOT NULL CHECK (resource_kind BETWEEN 1 AND 4),
    sample_count integer NOT NULL DEFAULT 0 CHECK (sample_count >= 0),

    cpu_usage_average real,
    cpu_usage_maximum real,
    cpu_limit_cores_average real,
    cpu_throttled_usec bigint,
    cpu_pressure_average real,
    cpu_pressure_maximum real,

    memory_current_average_bytes bigint,
    memory_current_maximum_bytes bigint,
    memory_limit_average_bytes bigint,
    memory_usage_average_percent real,
    memory_usage_maximum_percent real,
    memory_high_count integer,
    oom_kill_count integer,
    memory_pressure_average real,
    memory_pressure_maximum real,

    network_rx_average_bytes_per_second bigint,
    network_rx_maximum_bytes_per_second bigint,
    network_tx_average_bytes_per_second bigint,
    network_tx_maximum_bytes_per_second bigint,
    tcp_retransmits bigint,
    disk_read_average_bytes_per_second bigint,
    disk_read_maximum_bytes_per_second bigint,
    disk_write_average_bytes_per_second bigint,
    disk_write_maximum_bytes_per_second bigint,
    io_pressure_average real,
    io_pressure_maximum real,

    restart_count_maximum integer,
    restart_count_delta integer,
    ready_ratio real,
    cpu_request_cores_average real,
    memory_request_average_bytes bigint,
    desired_replicas_maximum integer,
    ready_replicas_minimum integer,
    unavailable_replicas_maximum integer,
    generation_mismatch boolean,
    pod_phase text,
    container_state text,
    last_termination_reason text,

    server_request_count integer NOT NULL DEFAULT 0,
    server_error_count integer NOT NULL DEFAULT 0,
    server_latency_average_ms real,
    server_latency_p50_ms real,
    server_latency_p95_ms real,
    server_latency_p99_ms real,
    server_latency_maximum_ms real,
    client_request_count integer NOT NULL DEFAULT 0,
    client_error_count integer NOT NULL DEFAULT 0,
    client_latency_average_ms real,
    client_latency_p95_ms real,

    warning_event_count integer NOT NULL DEFAULT 0,
    normal_event_count integer NOT NULL DEFAULT 0,
    event_occurrence_count integer NOT NULL DEFAULT 0,

    PRIMARY KEY (
        experiment_id,
        feature_version,
        phase,
        bucket_start,
        resource_id
    ),
    FOREIGN KEY (experiment_id, feature_version)
        REFERENCES telemetry.experiment_feature_builds (
            experiment_id, feature_version
        ) ON DELETE CASCADE
);

CREATE INDEX experiment_feature_buckets_sequence_idx
    ON telemetry.experiment_feature_buckets (
        experiment_id, feature_version, sequence_index, resource_kind, resource_id
    );
