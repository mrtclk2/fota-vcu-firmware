# Secure Gateway iOS App

`main/ble_handler.c` içindeki NimBLE GATT servisine (0x1815) BLE üzerinden
bağlanan, SwiftUI + CoreBluetooth tabanlı iPhone uygulaması. Ekran tasarımı
paylaşılan mock-up'larla birebir eşleşecek şekilde hazırlandı: Aracım ana
ekranı, araç fotoğrafı seçimi, Bluetooth tarama/bağlanma sheet'i, Wi-Fi
kimlik bilgisi gönderimi, OTA/VCU güncelleme ekranı, Diyagnostik ekranı ve
canlı "Kart durumları" bildirim akışı.

Kod, gerçek bir Xcode projesi olarak değil (pbxproj dosyaları elle
oluşturulduğunda küçük bir hata Xcode'un projeyi hiç açamamasına yol
açabiliyor, bu riskli), **kaynak dosyalar** halinde `SecureGateway/` altında
duruyor. Aşağıdaki adımlarla 5 dakikada çalışan bir Xcode projesine
dönüştürebilirsiniz.

## 1) Firmware tarafında ne değişti?

Ekran görüntülerindeki "Kart durumları" kutusu (`OTA: OTA_PROGRESS (%20)`,
`Wi-Fi: WiFi bağlandı. IP: ...`, `OTA: VCU CAN flash hatasi` gibi anlık
metinler) eskiden **sadece seri log'a** yazılıyordu, BLE üzerinden hiç
gönderilmiyordu. Bu commit'te `main/ble_handler.c`'ye 4. bir characteristic
(NOTIFY, `...cd126`) eklendi ve `wifi_handler.c` / `ota_handler.c` bu yeni
`ble_notify_status()` fonksiyonunu WiFi bağlantı sonucu, self-OTA ilerlemesi
ve VCU CAN flash sonucu için çağırıyor. Uygulamanın "Kart durumları" ve OTA
ilerleme çubuğunun canlı çalışması için **gateway'in bu güncel firmware ile
yeniden flashlanması gerekir**. WiFi gönderimi, OTA tetikleme ve araç
telemetrisi (soc/tork/vites/DTC) zaten eski firmware ile de çalışır.

## 2) GATT protokolü (referans)

| Characteristic | UUID | Yön | İçerik |
|---|---|---|---|
| Service | `1815` | — | Primary service |
| WiFi | `00001525-1212-EFDE-1523-785FEABCD123` | Write | `WIFI:ssid,password` |
| OTA | `00001525-1212-EFDE-1523-785FEABCD124` | Write | `OTA:URL:<url>` / `OTA:VCU:URL:<url>` / `OTA:STATUS` |
| Vehicle | `00001525-1212-EFDE-1523-785FEABCD125` | Notify | `{"soc":.., "state":.., "gear":"D", "torque":.., "throttle":.., "brake":.., "dtc":"NONE", "hvil":"OK", "fw_ver":"v1.0.0"}` |
| Status | `00001525-1212-EFDE-1523-785FEABCD126` | Notify | İnsan-okunur durum metni |

Advertising sadece cihaz adını (`FOTA_ESP32`) yayınlıyor, servis UUID
listesi yaymıyor — bu yüzden uygulama `scanForPeripherals(withServices: nil)`
ile tarayıp adına göre filtreliyor (`BLEManager.swift`).

## 3) Xcode projesi oluşturma (~5 dk)

1. Xcode → **File → New → Project → iOS → App**
   - Product Name: `SecureGateway`
   - Interface: **SwiftUI**, Language: **Swift**
   - Minimum Deployments: **iOS 17.0**
2. Proje oluşunca Xcode'un otomatik ürettiği `ContentView.swift` ve
   `SecureGatewayApp.swift` dosyalarını **silin** (bizim kendi
   `App/SecureGatewayApp.swift` dosyamız `@main` içeriyor, ikisi birden
   olursa derleme hatası verir).
3. Finder'da bu klasördeki `SecureGateway/SecureGateway/App`, `BLE`,
   `Models`, `Views` klasörlerini Xcode proje gezgininde uygulama
   hedefinizin üstüne sürükleyin. "Copy items if needed" ve target
   membership (SecureGateway) işaretli olsun.
4. Target → **Info** sekmesine şu 4 anahtarı ekleyin (veya
   `Resources/Info.plist` içeriğini referans alarak Custom iOS Target
   Properties'e ekleyin):
   - `Privacy - Bluetooth Always Usage Description`
   - `Privacy - Camera Usage Description`
   - `Privacy - Photo Library Usage Description`
   - `Required background modes` → `App communicates using CoreBluetooth`
5. **Signing & Capabilities**'te kendi Apple ID / geliştirici takımınızı
   seçin.
6. Gerçek bir iPhone'a bağlayıp **⌘R** ile çalıştırın.

   > CoreBluetooth Simulator'de çalışmaz — mutlaka fiziksel iPhone gerekir.

## 4) Dosya haritası

```
SecureGateway/SecureGateway/
├── App/SecureGatewayApp.swift        // @main giriş noktası
├── BLE/GatewayProtocol.swift         // UUID + komut sabitleri
├── BLE/ConnectionState.swift         // Bağlantı durumu enum'u
├── BLE/BLEManager.swift              // CoreBluetooth central/peripheral mantığı
├── Models/VehicleData.swift          // Telemetri JSON modeli
├── Views/VehicleHomeView.swift       // "Aracım" ana ekranı
├── Views/VehiclePhotoSheet.swift     // Araç fotoğrafı seçim sheet'i
├── Views/CameraPicker.swift          // Kamera köprüsü (UIImagePickerController)
├── Views/VehiclePhotoStore.swift     // Fotoğrafı cihazda önbelleğe alma
├── Views/BluetoothConnectSheet.swift // Tarama/bağlanma sheet'i
├── Views/WifiCredentialsSheet.swift  // SSID/şifre formu
├── Views/UpdatesView.swift           // Gateway + VCU OTA tetikleme
├── Views/DiagnosticsView.swift       // DTC/HVIL/vites/fren
├── Views/SettingsSheet.swift         // Bağlantıyı kes vb.
└── Resources/Info.plist              // İzin metinleri (referans)
```

## 5) Bilinçli olarak eklenmeyenler

- **Kilidi aç / Kilitle**: Firmware'de (CAN handler / UDS client) buna
  karşılık gelen bir komut yok. Butonlar arayüzde duruyor ama basınca
  "desteklenmiyor" uyarısı veriyor — sahte bir işlevmiş gibi göstermek
  yerine dürüst davranıldı. VCU tarafında bir kilit CAN mesajı/ID
  tanımlanırsa (örn. `can_handler.h`'ye yeni bir `CAN_ID_LOCK_CMD`), bu
  buton kolayca gerçek bir komuta bağlanabilir.
