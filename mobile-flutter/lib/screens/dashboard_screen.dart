import 'dart:async';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/api_service.dart';

class DashboardScreen extends StatefulWidget {
  const DashboardScreen({super.key});

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen>
    with WidgetsBindingObserver {
  Map<String, dynamic>? _device;
  Map<String, dynamic>? _telemetry;
  List<Map<String, dynamic>> _alerts = [];
  Timer? _statusTimer;
  Timer? _telemetryTimer;
  bool _fetchingDevice = false;
  bool _fetchingTelemetry = false;
  bool _fetchingAlerts = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _refreshAll();
    _statusTimer = Timer.periodic(
      const Duration(milliseconds: 500),
      (_) => _fetchDevice(),
    );
    _telemetryTimer = Timer.periodic(
      const Duration(seconds: 5),
      (_) => _fetchMonitoringData(),
    );
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _statusTimer?.cancel();
    _telemetryTimer?.cancel();
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) {
      _refreshAll();
    }
  }

  Future<void> _refreshAll() async {
    await Future.wait([_fetchDevice(), _fetchTelemetry(), _fetchAlerts()]);
  }

  Future<void> _fetchMonitoringData() async {
    await Future.wait([_fetchTelemetry(), _fetchAlerts()]);
  }

  Future<void> _fetchDevice() async {
    if (_fetchingDevice) return;
    _fetchingDevice = true;
    final api = context.read<ApiService>();
    try {
      final device = await api.getDevice('esp32-001');
      if (mounted && device != null) {
        setState(() => _device = device);
      }
    } finally {
      _fetchingDevice = false;
    }
  }

  Future<void> _fetchTelemetry() async {
    if (_fetchingTelemetry) return;
    _fetchingTelemetry = true;
    final api = context.read<ApiService>();
    try {
      final telemetry = await api.getLatestTelemetry('esp32-001');
      if (mounted && telemetry != null) {
        setState(() => _telemetry = telemetry);
      }
    } finally {
      _fetchingTelemetry = false;
    }
  }

  Future<void> _fetchAlerts() async {
    if (_fetchingAlerts) return;
    _fetchingAlerts = true;
    final api = context.read<ApiService>();
    try {
      final alerts = await api.getDeviceAlerts('esp32-001');
      if (mounted && alerts != null) {
        setState(() => _alerts = alerts);
      }
    } finally {
      _fetchingAlerts = false;
    }
  }

  Future<void> _refreshAfterCommand() async {
    await Future<void>.delayed(const Duration(milliseconds: 300));
    if (mounted) {
      await _fetchDevice();
      await _fetchTelemetry();
      await _fetchAlerts();
    }
  }

  Future<void> _toggleLed() async {
    if (_device == null) return;
    final api = context.read<ApiService>();
    final action = _device!['ledState'] == true ? 'LED_OFF' : 'LED_ON';
    final sent = await api.sendCommand('esp32-001', action);
    if (sent) {
      await _refreshAfterCommand();
    }
  }

  @override
  Widget build(BuildContext context) {
    final api = context.watch<ApiService>();
    Map<String, dynamic>? activeTemperatureAlert;
    for (final alert in _alerts) {
      if (alert['type'] == 'HIGH_TEMPERATURE' && alert['status'] == 'ACTIVE') {
        activeTemperatureAlert = alert;
        break;
      }
    }

    return Scaffold(
      appBar: AppBar(
        title: Text('Dashboard (${api.role})'),
        actions: [
          IconButton(
            icon: const Icon(Icons.logout),
            onPressed: () => api.logout(),
          ),
        ],
      ),
      body: _device == null
          ? const Center(child: CircularProgressIndicator())
          : ListView(
              padding: const EdgeInsets.all(16.0),
              children: [
                if (activeTemperatureAlert != null) ...[
                  Card(
                    color: Colors.orange.shade100,
                    child: Padding(
                      padding: const EdgeInsets.all(16),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          const Row(
                            children: [
                              Icon(Icons.warning_amber_rounded,
                                  color: Colors.deepOrange),
                              SizedBox(width: 8),
                              Text(
                                'High Temperature',
                                style: TextStyle(
                                    fontSize: 18, fontWeight: FontWeight.bold),
                              ),
                            ],
                          ),
                          const SizedBox(height: 8),
                          Text(
                              'Temperature: ${activeTemperatureAlert['temperature']} °C'),
                          Text(
                              'Threshold: ${activeTemperatureAlert['threshold']} °C'),
                          Text('Device: ${activeTemperatureAlert['deviceId']}'),
                        ],
                      ),
                    ),
                  ),
                  const SizedBox(height: 16),
                ],
                Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text('Device: ${_device!['name']}',
                        style: const TextStyle(fontSize: 20)),
                    Text('Status: ${_device!['status']}',
                        style: TextStyle(
                            color: _device!['status'] == 'ONLINE'
                                ? Colors.green
                                : Colors.red,
                            fontWeight: FontWeight.bold)),
                    const SizedBox(height: 20),
                    if (_telemetry != null) ...[
                      Text('Temperature: ${_telemetry!['temperature']} °C'),
                      Text('Humidity: ${_telemetry!['humidity']} %'),
                    ],
                    const SizedBox(height: 20),
                    if (api.role != 'VIEWER')
                      Row(
                        children: [
                          const Text('LED Control: '),
                          Switch(
                            value: _device!['ledState'] ?? false,
                            onChanged: (_) => _toggleLed(),
                          ),
                        ],
                      ),
                    if (api.role == 'VIEWER')
                      Text(
                          'LED is ${_device!['ledState'] == true ? 'ON' : 'OFF'}'),
                  ],
                ),
                const SizedBox(height: 24),
                const Text(
                  'Recent Alerts',
                  style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
                ),
                const SizedBox(height: 8),
                if (_alerts.isEmpty)
                  const Text('No alerts recorded.')
                else
                  ..._alerts.take(5).map(
                        (alert) => Card(
                          child: ListTile(
                            leading: Icon(
                              alert['status'] == 'ACTIVE'
                                  ? Icons.warning_amber_rounded
                                  : Icons.check_circle_outline,
                              color: alert['status'] == 'ACTIVE'
                                  ? Colors.orange
                                  : Colors.green,
                            ),
                            title: Text(
                                'High Temperature · ${alert['temperature']} °C'),
                            subtitle:
                                Text('Threshold ${alert['threshold']} °C'),
                            trailing: Text(
                              alert['status'] as String,
                              style: TextStyle(
                                fontWeight: FontWeight.bold,
                                color: alert['status'] == 'ACTIVE'
                                    ? Colors.orange.shade800
                                    : Colors.green,
                              ),
                            ),
                          ),
                        ),
                      ),
              ],
            ),
    );
  }
}
