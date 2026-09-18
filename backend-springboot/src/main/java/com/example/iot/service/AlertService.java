package com.example.iot.service;

import com.example.iot.entity.Alert;
import com.example.iot.repository.AlertRepository;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.ZonedDateTime;
import java.util.List;
import java.util.Locale;
import java.util.UUID;

@Slf4j
@Service
public class AlertService {
    public static final String HIGH_TEMPERATURE = "HIGH_TEMPERATURE";
    public static final String WARNING = "WARNING";
    public static final String ACTIVE = "ACTIVE";
    public static final String RESOLVED = "RESOLVED";

    private final AlertRepository alertRepository;
    private final double highTemperatureThreshold;

    public AlertService(
            AlertRepository alertRepository,
            @Value("${iot.alert.high-temperature-threshold:31.0}") double highTemperatureThreshold) {
        this.alertRepository = alertRepository;
        this.highTemperatureThreshold = highTemperatureThreshold;
    }

    /**
     * Advances the alert state machine for one valid telemetry sample.
     * This method is synchronized in addition to the database partial unique index,
     * so repeated MQTT samples cannot create duplicate ACTIVE alerts.
     */
    @Transactional
    public synchronized void evaluateHighTemperature(String deviceId, Double temperature) {
        if (deviceId == null || temperature == null || !Double.isFinite(temperature)) {
            return;
        }

        ZonedDateTime now = ZonedDateTime.now();
        var activeAlert = alertRepository.findFirstByDeviceIdAndTypeAndStatus(
                deviceId, HIGH_TEMPERATURE, ACTIVE);

        if (temperature > highTemperatureThreshold) {
            if (activeAlert.isPresent()) {
                Alert alert = activeAlert.get();
                alert.setTemperature(temperature);
                alert.setUpdatedAt(now);
                alertRepository.save(alert);
                return;
            }

            Alert alert = new Alert();
            alert.setId(UUID.randomUUID());
            alert.setDeviceId(deviceId);
            alert.setType(HIGH_TEMPERATURE);
            alert.setSeverity(WARNING);
            alert.setMessage(String.format(
                    Locale.US,
                    "Nhiệt độ thiết bị %s vượt ngưỡng %.1f°C",
                    deviceId,
                    highTemperatureThreshold));
            alert.setTemperature(temperature);
            alert.setThreshold(highTemperatureThreshold);
            alert.setStatus(ACTIVE);
            alert.setCreatedAt(now);
            alert.setUpdatedAt(now);
            alertRepository.save(alert);
            log.warn("Created HIGH_TEMPERATURE alert for device {}: {}°C > {}°C",
                    deviceId, temperature, highTemperatureThreshold);
            return;
        }

        activeAlert.ifPresent(alert -> {
            alert.setTemperature(temperature);
            alert.setStatus(RESOLVED);
            alert.setUpdatedAt(now);
            alert.setResolvedAt(now);
            alertRepository.save(alert);
            log.info("Resolved HIGH_TEMPERATURE alert for device {} at {}°C",
                    deviceId, temperature);
        });
    }

    @Transactional(readOnly = true)
    public List<Alert> getActiveAlerts() {
        return alertRepository.findByStatusOrderByUpdatedAtDesc(ACTIVE);
    }

    @Transactional(readOnly = true)
    public Page<Alert> getAlertHistory(Pageable pageable) {
        return alertRepository.findAllByOrderByCreatedAtDesc(pageable);
    }

    @Transactional(readOnly = true)
    public Page<Alert> getDeviceAlertHistory(String deviceId, Pageable pageable) {
        return alertRepository.findByDeviceIdOrderByCreatedAtDesc(deviceId, pageable);
    }

    public double getHighTemperatureThreshold() {
        return highTemperatureThreshold;
    }
}
