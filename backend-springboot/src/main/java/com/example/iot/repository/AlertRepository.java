package com.example.iot.repository;

import com.example.iot.entity.Alert;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.JpaRepository;

import java.util.List;
import java.util.Optional;
import java.util.UUID;

public interface AlertRepository extends JpaRepository<Alert, UUID> {
    Optional<Alert> findFirstByDeviceIdAndTypeAndStatus(
            String deviceId, String type, String status);

    List<Alert> findByStatusOrderByUpdatedAtDesc(String status);

    Page<Alert> findAllByOrderByCreatedAtDesc(Pageable pageable);

    Page<Alert> findByDeviceIdOrderByCreatedAtDesc(String deviceId, Pageable pageable);
}
