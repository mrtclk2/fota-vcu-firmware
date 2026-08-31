import CoreBluetooth

/// `fota-vcu-firmware/main/ble_handler.c` içindeki GATT tablosuyla birebir
/// eşleşen UUID/komut tanımları. Firmware `BLE_UUID128_INIT` bayt dizisini
/// little-endian (LSB önce) olarak saklar; CoreBluetooth ise standart
/// big-endian UUID string'i bekler, bu yüzden bayt sırası ters çevrilmiştir.
enum GatewayProtocol {
    /// `BLE_UUID16_DECLARE(0x1815)`
    static let serviceUUID = CBUUID(string: "1815")

    /// WiFi credential characteristic — WRITE. Payload: `WIFI:ssid,password`
    static let wifiCharUUID = CBUUID(string: "00001525-1212-EFDE-1523-785FEABCD123")

    /// OTA komut characteristic — WRITE. Bkz. `GatewayCommand`.
    static let otaCharUUID = CBUUID(string: "00001525-1212-EFDE-1523-785FEABCD124")

    /// Araç telemetri characteristic — NOTIFY only. JSON: `VehicleData`.
    static let vehicleCharUUID = CBUUID(string: "00001525-1212-EFDE-1523-785FEABCD125")

    /// Durum metni characteristic — NOTIFY only (WiFi/OTA olay metinleri).
    /// NOT: Bu characteristic depoda az önce eklendi; eski firmware sürümleri
    /// bunu sunmuyorsa app sadece bu bildirimleri almaz, diğer her şey çalışır.
    static let statusCharUUID = CBUUID(string: "00001525-1212-EFDE-1523-785FEABCD126")

    static let deviceNamePrefix = "FOTA_ESP32"

    /// WiFi kimlik bilgisi yazma sırasında tek seferde gönderilecek parça
    /// boyutu. Firmware, gelen chunk'ları `rx_buffer` içinde biriktirip
    /// `WIFI:` başlığını görene kadar bekliyor — küçük parçalar her MTU'da
    /// güvenle çalışır.
    static let wifiWriteChunkSize = 20
}

/// `ota_chr_access()` tarafından tanınan komut metinleri.
enum GatewayCommand {
    static func wifiCredentials(ssid: String, password: String) -> String {
        "WIFI:\(ssid),\(password)"
    }

    static func selfOTA(url: String) -> String { "OTA:URL:\(url)" }

    static func vcuOTA(url: String) -> String { "OTA:VCU:URL:\(url)" }

    static let otaStatus = "OTA:STATUS"
}
