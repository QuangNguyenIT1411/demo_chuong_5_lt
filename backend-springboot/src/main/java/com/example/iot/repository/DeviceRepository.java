package com.example.iot.repository;

import com.example.iot.entity.Device;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Modifying;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;

import java.time.ZonedDateTime;
import java.util.List;
import java.util.Optional;
import java.util.UUID;

public interface DeviceRepository extends JpaRepository<Device, UUID> {
    Optional<Device> findByDeviceId(String deviceId);
    List<Device> findByStatus(String status);

    @Modifying(clearAutomatically = true, flushAutomatically = true)
    @Query("""
            update Device d
               set d.status = 'OFFLINE', d.updatedAt = :now
             where d.status = 'ONLINE'
               and d.deviceId = :deviceId
               and (d.lastSeenAt is null or d.lastSeenAt < :cutoff)
            """)
    int markTimedOutDeviceOffline(@Param("deviceId") String deviceId,
                                  @Param("cutoff") ZonedDateTime cutoff,
                                  @Param("now") ZonedDateTime now);
}
