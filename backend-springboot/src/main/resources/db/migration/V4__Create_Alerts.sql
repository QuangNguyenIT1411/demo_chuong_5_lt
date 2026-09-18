CREATE TABLE alerts (
    id UUID PRIMARY KEY,
    device_id VARCHAR(100) NOT NULL,
    type VARCHAR(50) NOT NULL,
    severity VARCHAR(20) NOT NULL,
    message VARCHAR(500) NOT NULL,
    temperature DOUBLE PRECISION NOT NULL,
    threshold DOUBLE PRECISION NOT NULL,
    status VARCHAR(20) NOT NULL,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP,
    resolved_at TIMESTAMP WITH TIME ZONE,
    CONSTRAINT fk_alerts_device FOREIGN KEY (device_id)
        REFERENCES devices(device_id) ON DELETE CASCADE,
    CONSTRAINT chk_alerts_status CHECK (status IN ('ACTIVE', 'RESOLVED'))
);

CREATE INDEX idx_alerts_device_created ON alerts(device_id, created_at DESC);
CREATE INDEX idx_alerts_status_updated ON alerts(status, updated_at DESC);

-- Database-level protection against duplicate alerts while a cycle is active.
CREATE UNIQUE INDEX uq_alerts_active_device_type
    ON alerts(device_id, type)
    WHERE status = 'ACTIVE';
