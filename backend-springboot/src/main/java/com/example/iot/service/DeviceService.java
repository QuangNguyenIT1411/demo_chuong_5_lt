package com.example.iot.service;

import com.example.iot.dto.StatusPayload;
import com.example.iot.entity.Device;
import com.example.iot.repository.DeviceRepository;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.Duration;
import java.time.ZonedDateTime;
import java.util.List;
import java.util.Locale;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;

@Slf4j
@Service
@RequiredArgsConstructor
public class DeviceService {

    private final DeviceRepository deviceRepository;

    @Value("${device.presence.timeout-ms:4000}")
    private long presenceTimeoutMs;

    @Value("${device.presence.legacy-timeout-ms:7000}")
    private long legacyPresenceTimeoutMs;

    @Value("${device.presence.heartbeat-confirmation-gap-ms:3000}")
    private long heartbeatConfirmationGapMs;

    private final ConcurrentMap<String, HeartbeatState> heartbeatStates = new ConcurrentHashMap<>();

    @Transactional
    public void processStatus(StatusPayload payload) {
        if (payload.getDeviceId() == null || payload.getStatus() == null) return;

        String status = payload.getStatus().toUpperCase(Locale.ROOT);
        if (!"ONLINE".equals(status) && !"OFFLINE".equals(status)) {
            log.warn("Ignoring invalid status '{}' for device {}", payload.getStatus(), payload.getDeviceId());
            return;
        }

        // Presence always uses server receive time. A Last Will timestamp is
        // created when the client connects, so it must not drive lastSeen.
        ZonedDateTime receivedAt = ZonedDateTime.now();

        if ("ONLINE".equals(status)) {
            recordOnlineHeartbeat(payload.getDeviceId(), receivedAt);
        } else {
            heartbeatStates.remove(payload.getDeviceId());
        }
        
        Optional<Device> deviceOpt = deviceRepository.findByDeviceId(payload.getDeviceId());
        if (deviceOpt.isPresent()) {
            Device device = deviceOpt.get();
            String previousStatus = device.getStatus();
            device.setStatus(status);
            device.setLastSeenAt(receivedAt);
            device.setUpdatedAt(receivedAt);
            deviceRepository.save(device);
            if (!status.equals(previousStatus)) {
                log.info("Device {} status updated from {} to {}",
                        device.getDeviceId(), previousStatus, status);
            } else {
                log.debug("Presence refreshed for device {} ({})", device.getDeviceId(), status);
            }
        } else {
            // Auto register device if needed, or just log
            log.warn("Status received for unknown device: {}", payload.getDeviceId());
            Device device = new Device();
            device.setId(UUID.randomUUID());
            device.setDeviceId(payload.getDeviceId());
            device.setName("Auto-registered " + payload.getDeviceId());
            device.setType("UNKNOWN");
            device.setStatus(status);
            device.setLedState(false);
            device.setLastSeenAt(receivedAt);
            device.setCreatedAt(receivedAt);
            device.setUpdatedAt(receivedAt);
            deviceRepository.save(device);
        }
    }

    @Scheduled(fixedDelayString = "${device.presence.check-interval-ms:500}")
    @Transactional
    public void markTimedOutDevicesOffline() {
        ZonedDateTime now = ZonedDateTime.now();
        for (Device device : deviceRepository.findByStatus("ONLINE")) {
            HeartbeatState heartbeatState = heartbeatStates.get(device.getDeviceId());
            boolean fastHeartbeatConfirmed = heartbeatState != null && heartbeatState.confirmed();
            long timeoutMs = fastHeartbeatConfirmed ? presenceTimeoutMs : legacyPresenceTimeoutMs;
            ZonedDateTime cutoff = now.minus(Duration.ofMillis(timeoutMs));

            int updated = deviceRepository.markTimedOutDeviceOffline(
                    device.getDeviceId(), cutoff, now);
            if (updated > 0) {
                heartbeatStates.remove(device.getDeviceId());
                log.warn("Device {} marked OFFLINE after {} ms without heartbeat/telemetry/ACK ({})",
                        device.getDeviceId(), timeoutMs,
                        fastHeartbeatConfirmed ? "fast heartbeat" : "legacy fallback");
            }
        }
    }

    private void recordOnlineHeartbeat(String deviceId, ZonedDateTime receivedAt) {
        heartbeatStates.compute(deviceId, (id, previous) -> {
            boolean confirmed = previous != null &&
                    (previous.confirmed() ||
                            Math.abs(Duration.between(previous.receivedAt(), receivedAt).toMillis())
                                    <= heartbeatConfirmationGapMs);
            return new HeartbeatState(receivedAt, confirmed);
        });
    }

    private record HeartbeatState(ZonedDateTime receivedAt, boolean confirmed) {
    }

    public List<Device> getAllDevices() {
        return deviceRepository.findAll();
    }

    public Device getDeviceByDeviceId(String deviceId) {
        return deviceRepository.findByDeviceId(deviceId)
                .orElseThrow(() -> new RuntimeException("Device not found"));
    }
}
