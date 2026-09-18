package com.example.iot.service;

import com.example.iot.entity.Alert;
import com.example.iot.repository.AlertRepository;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

class AlertServiceTest {
    private final AlertRepository repository = mock(AlertRepository.class);
    private final List<Alert> storedAlerts = new ArrayList<>();
    private AlertService service;

    @BeforeEach
    void setUp() {
        storedAlerts.clear();
        service = new AlertService(repository, 35.0);

        when(repository.findFirstByDeviceIdAndTypeAndStatus(
                "esp32-001", AlertService.HIGH_TEMPERATURE, AlertService.ACTIVE))
                .thenAnswer(invocation -> storedAlerts.stream()
                        .filter(alert -> AlertService.ACTIVE.equals(alert.getStatus()))
                        .findFirst());

        when(repository.save(any(Alert.class))).thenAnswer(invocation -> {
            Alert alert = invocation.getArgument(0);
            if (storedAlerts.stream().noneMatch(existing -> existing.getId().equals(alert.getId()))) {
                storedAlerts.add(alert);
            }
            return alert;
        });
    }

    @Test
    void alertCycleDoesNotDuplicateAndCanTriggerAgainAfterResolution() {
        service.evaluateHighTemperature("esp32-001", 30.0);
        assertThat(storedAlerts).isEmpty();

        service.evaluateHighTemperature("esp32-001", 36.0);
        service.evaluateHighTemperature("esp32-001", 36.2);

        assertThat(storedAlerts).hasSize(1);
        Alert firstCycle = storedAlerts.getFirst();
        assertThat(firstCycle.getStatus()).isEqualTo(AlertService.ACTIVE);
        assertThat(firstCycle.getTemperature()).isEqualTo(36.2);

        service.evaluateHighTemperature("esp32-001", 35.0);
        assertThat(firstCycle.getStatus()).isEqualTo(AlertService.RESOLVED);
        assertThat(firstCycle.getResolvedAt()).isNotNull();

        service.evaluateHighTemperature("esp32-001", 35.5);
        assertThat(storedAlerts).hasSize(2);
        assertThat(storedAlerts.get(1).getStatus()).isEqualTo(AlertService.ACTIVE);
        assertThat(storedAlerts.get(1).getId()).isNotEqualTo(firstCycle.getId());
    }
}
