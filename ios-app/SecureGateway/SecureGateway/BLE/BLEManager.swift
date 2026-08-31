import Foundation
import CoreBluetooth
import Combine

enum GatewayError: LocalizedError {
    case notConnected
    case missingCharacteristic
    case payloadTooLarge(max: Int)
    case bluetoothUnavailable

    var errorDescription: String? {
        switch self {
        case .notConnected:          return "Gateway'e bağlı değilsiniz."
        case .missingCharacteristic: return "Gerekli BLE servisi bulunamadı."
        case .payloadTooLarge(let max):
            return "Mesaj çok uzun (BLE limiti \(max) byte). Daha kısa bir URL deneyin."
        case .bluetoothUnavailable:  return "Bluetooth kapalı veya izin verilmedi."
        }
    }
}

@MainActor
final class BLEManager: NSObject, ObservableObject {
    // MARK: - Published state

    @Published private(set) var connectionState: ConnectionState = .idle
    @Published private(set) var discoveredPeripherals: [DiscoveredPeripheral] = []
    @Published private(set) var vehicleData: VehicleData = .unknown
    @Published private(set) var lastStatus: String?
    @Published private(set) var statusLog: [String] = []
    @Published private(set) var otaProgress: Double?
    @Published private(set) var connectedPeripheralID: UUID?
    @Published private(set) var connectedPeripheralName: String?
    @Published private(set) var bluetoothPoweredOn = false
    @Published var errorMessage: String?

    // MARK: - CoreBluetooth internals

    private var central: CBCentralManager!
    private var connectedPeripheral: CBPeripheral?
    private var wifiChar: CBCharacteristic?
    private var otaChar: CBCharacteristic?
    private var vehicleChar: CBCharacteristic?
    private var statusChar: CBCharacteristic?
    private var rawPeripherals: [UUID: CBPeripheral] = [:]
    private var pendingWriteContinuation: CheckedContinuation<Void, Error>?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    // MARK: - Scanning

    func startScan() {
        guard bluetoothPoweredOn else {
            errorMessage = GatewayError.bluetoothUnavailable.localizedDescription
            return
        }
        discoveredPeripherals.removeAll()
        rawPeripherals.removeAll()
        connectionState = .scanning
        central.scanForPeripherals(withServices: nil, options: [
            CBCentralManagerScanOptionAllowDuplicatesKey: true
        ])
    }

    func stopScan() {
        central.stopScan()
        if connectionState == .scanning {
            connectionState = connectedPeripheral == nil ? .idle : .connected
        }
    }

    // MARK: - Connect / disconnect

    func connect(to id: UUID) {
        guard let peripheral = rawPeripherals[id] else { return }
        stopScan()
        connectionState = .connecting
        central.connect(peripheral, options: nil)
    }

    func disconnect() {
        guard let peripheral = connectedPeripheral else { return }
        central.cancelPeripheralConnection(peripheral)
    }

    // MARK: - Commands

    func sendWifiCredentials(ssid: String, password: String) async {
        guard let characteristic = wifiChar else {
            errorMessage = GatewayError.missingCharacteristic.localizedDescription
            return
        }
        let payload = GatewayCommand.wifiCredentials(ssid: ssid, password: password)
        let bytes = Array(payload.utf8)
        do {
            for chunk in bytes.chunked(into: GatewayProtocol.wifiWriteChunkSize) {
                try await writeAsync(Data(chunk), to: characteristic)
            }
            recordLocalStatus("Wi-Fi bilgileri gönderildi, bağlanılıyor…")
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    func startSelfOTA(url: String) async {
        await sendOTACommand(GatewayCommand.selfOTA(url: url), progressReset: true)
    }

    func startVCUOTA(url: String) async {
        await sendOTACommand(GatewayCommand.vcuOTA(url: url), progressReset: true)
    }

    func requestOTAStatus() async {
        await sendOTACommand(GatewayCommand.otaStatus, progressReset: false)
    }

    private func sendOTACommand(_ command: String, progressReset: Bool) async {
        guard let characteristic = otaChar else {
            errorMessage = GatewayError.missingCharacteristic.localizedDescription
            return
        }
        let data = Data(command.utf8)
        let maxLen = connectedPeripheral?.maximumWriteValueLength(for: .withResponse) ?? 20
        guard data.count <= maxLen else {
            errorMessage = GatewayError.payloadTooLarge(max: maxLen).localizedDescription
            return
        }
        do {
            if progressReset { otaProgress = 0 }
            try await writeAsync(data, to: characteristic)
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    // MARK: - Write helper

    private func writeAsync(_ data: Data, to characteristic: CBCharacteristic) async throws {
        guard let peripheral = connectedPeripheral else { throw GatewayError.notConnected }
        try await withCheckedThrowingContinuation { continuation in
            self.pendingWriteContinuation = continuation
            peripheral.writeValue(data, for: characteristic, type: .withResponse)
        }
    }

    // MARK: - Status handling

    private func recordLocalStatus(_ text: String) {
        lastStatus = text
        statusLog.append(text)
        if statusLog.count > 50 { statusLog.removeFirst(statusLog.count - 50) }
    }

    private func handleStatusNotification(_ text: String) {
        recordLocalStatus(text)
        if let percent = Self.progressPercent(in: text) {
            otaProgress = Double(percent) / 100.0
        } else if text.contains("Basarili") || text.contains("başarılı")
                    || text.lowercased().contains("hata") || text.contains("Tamamlanamadi") {
            otaProgress = nil
        }
    }

    private static func progressPercent(in text: String) -> Int? {
        guard let range = text.range(of: #"\(%(\d{1,3})\)"#, options: .regularExpression) else {
            return nil
        }
        let match = String(text[range])
        let digits = match.filter(\.isNumber)
        return Int(digits)
    }
}

// MARK: - CBCentralManagerDelegate

extension BLEManager: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        Task { @MainActor in
            self.bluetoothPoweredOn = central.state == .poweredOn
            if central.state != .poweredOn {
                self.connectionState = .idle
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                     didDiscover peripheral: CBPeripheral,
                                     advertisementData: [String: Any],
                                     rssi RSSI: NSNumber) {
        let name = peripheral.name
            ?? (advertisementData[CBAdvertisementDataLocalNameKey] as? String)
        guard let name, name.hasPrefix(GatewayProtocol.deviceNamePrefix) else { return }

        Task { @MainActor in
            self.rawPeripherals[peripheral.identifier] = peripheral
            if let index = self.discoveredPeripherals.firstIndex(where: { $0.id == peripheral.identifier }) {
                self.discoveredPeripherals[index].rssi = RSSI.intValue
            } else {
                self.discoveredPeripherals.append(
                    DiscoveredPeripheral(id: peripheral.identifier, name: name, rssi: RSSI.intValue)
                )
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        Task { @MainActor in
            self.connectedPeripheral = peripheral
            self.connectedPeripheralID = peripheral.identifier
            self.connectedPeripheralName = peripheral.name
            self.connectionState = .connected
            peripheral.delegate = self
            peripheral.discoverServices([GatewayProtocol.serviceUUID])
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                     didFailToConnect peripheral: CBPeripheral,
                                     error: Error?) {
        Task { @MainActor in
            self.connectionState = .disconnected
            self.errorMessage = error?.localizedDescription ?? "Bağlantı başarısız."
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager,
                                     didDisconnectPeripheral peripheral: CBPeripheral,
                                     error: Error?) {
        Task { @MainActor in
            self.connectedPeripheral = nil
            self.connectedPeripheralID = nil
            self.connectedPeripheralName = nil
            self.wifiChar = nil
            self.otaChar = nil
            self.vehicleChar = nil
            self.statusChar = nil
            self.connectionState = .disconnected
            self.vehicleData = .unknown
            self.otaProgress = nil
        }
    }
}

// MARK: - CBPeripheralDelegate

extension BLEManager: CBPeripheralDelegate {
    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                 didDiscoverServices error: Error?) {
        guard error == nil, let services = peripheral.services else { return }
        for service in services where service.uuid == GatewayProtocol.serviceUUID {
            peripheral.discoverCharacteristics(nil, for: service)
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                 didDiscoverCharacteristicsFor service: CBService,
                                 error: Error?) {
        guard error == nil, let characteristics = service.characteristics else { return }
        Task { @MainActor in
            for characteristic in characteristics {
                switch characteristic.uuid {
                case GatewayProtocol.wifiCharUUID:
                    self.wifiChar = characteristic
                case GatewayProtocol.otaCharUUID:
                    self.otaChar = characteristic
                case GatewayProtocol.vehicleCharUUID:
                    self.vehicleChar = characteristic
                    peripheral.setNotifyValue(true, for: characteristic)
                case GatewayProtocol.statusCharUUID:
                    self.statusChar = characteristic
                    peripheral.setNotifyValue(true, for: characteristic)
                default:
                    break
                }
            }
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                 didUpdateValueFor characteristic: CBCharacteristic,
                                 error: Error?) {
        guard error == nil, let data = characteristic.value else { return }
        Task { @MainActor in
            switch characteristic.uuid {
            case GatewayProtocol.vehicleCharUUID:
                if let decoded = try? JSONDecoder().decode(VehicleData.self, from: data) {
                    self.vehicleData = decoded
                }
            case GatewayProtocol.statusCharUUID:
                if let text = String(data: data, encoding: .utf8) {
                    self.handleStatusNotification(text)
                }
            default:
                break
            }
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral,
                                 didWriteValueFor characteristic: CBCharacteristic,
                                 error: Error?) {
        Task { @MainActor in
            guard let continuation = self.pendingWriteContinuation else { return }
            self.pendingWriteContinuation = nil
            if let error {
                continuation.resume(throwing: error)
            } else {
                continuation.resume()
            }
        }
    }
}

private extension Array {
    func chunked(into size: Int) -> [[Element]] {
        guard size > 0 else { return [self] }
        return stride(from: 0, to: count, by: size).map {
            Array(self[$0..<Swift.min($0 + size, count)])
        }
    }
}
