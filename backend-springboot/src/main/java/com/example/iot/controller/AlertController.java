package com.example.iot.controller;

import com.example.iot.entity.Alert;
import com.example.iot.service.AlertService;
import lombok.RequiredArgsConstructor;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.PageRequest;
import org.springframework.http.CacheControl;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

import java.util.List;

@RestController
@RequestMapping("/api/v1")
@RequiredArgsConstructor
public class AlertController {
    private final AlertService alertService;

    @GetMapping("/alerts")
    public ResponseEntity<Page<Alert>> getAlertHistory(
            @RequestParam(defaultValue = "0") int page,
            @RequestParam(defaultValue = "20") int size) {
        return ResponseEntity.ok()
                .cacheControl(CacheControl.noStore())
                .body(alertService.getAlertHistory(PageRequest.of(page, size)));
    }

    @GetMapping("/alerts/active")
    public ResponseEntity<List<Alert>> getActiveAlerts() {
        return ResponseEntity.ok()
                .cacheControl(CacheControl.noStore())
                .body(alertService.getActiveAlerts());
    }

    @GetMapping("/devices/{deviceId}/alerts")
    public ResponseEntity<Page<Alert>> getDeviceAlertHistory(
            @PathVariable String deviceId,
            @RequestParam(defaultValue = "0") int page,
            @RequestParam(defaultValue = "20") int size) {
        return ResponseEntity.ok()
                .cacheControl(CacheControl.noStore())
                .body(alertService.getDeviceAlertHistory(deviceId, PageRequest.of(page, size)));
    }
}
