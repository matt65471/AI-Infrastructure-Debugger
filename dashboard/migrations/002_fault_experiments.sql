CREATE TABLE IF NOT EXISTS telemetry.fault_experiments (
    id uuid PRIMARY KEY,
    fault_type text NOT NULL,
    namespace_name text NOT NULL,
    target_kind text NOT NULL,
    target_name text NOT NULL,
    parameters jsonb NOT NULL DEFAULT '{}'::jsonb,
    status text NOT NULL CHECK (
        status IN (
            'baseline',
            'injecting',
            'recovering',
            'completed',
            'failed',
            'recovery_failed'
        )
    ),
    created_at timestamptz NOT NULL DEFAULT now(),
    baseline_started_at timestamptz NOT NULL,
    injected_at timestamptz,
    fault_ended_at timestamptz,
    recovery_completed_at timestamptz,
    failed_at timestamptz,
    expires_at timestamptz NOT NULL,
    error_message text,
    traffic_summary jsonb NOT NULL DEFAULT '{}'::jsonb,
    updated_at timestamptz NOT NULL DEFAULT now(),
    CHECK (expires_at > baseline_started_at)
);

CREATE INDEX IF NOT EXISTS fault_experiments_time_idx
    ON telemetry.fault_experiments (baseline_started_at DESC);
CREATE INDEX IF NOT EXISTS fault_experiments_target_idx
    ON telemetry.fault_experiments (
        namespace_name,
        target_kind,
        target_name,
        baseline_started_at DESC
    );
CREATE INDEX IF NOT EXISTS fault_experiments_status_idx
    ON telemetry.fault_experiments (status, expires_at)
    WHERE status IN ('baseline', 'injecting', 'recovering');
