# Secure Gateway iOS App

`fota-vcu-firmware` deposundaki ESP32 secure gateway'in NimBLE GATT servisine
(0x1815) BLE üzerinden bağlanan, SwiftUI + CoreBluetooth tabanlı bağımsız bir
iPhone uygulaması. Bu repo **hem VCU hem de secure gateway firmware
kodundan bağımsızdır** — sadece BLE üzerinden konuştuğu protokolü bilir.

Ekran tasarımı: Aracım ana ekranı, araç fotoğrafı seçimi, Bluetooth
tarama/bağlanma sheet'i, Wi-Fi kimlik bilgisi gönderimi, OTA/VCU güncelleme
ekranı, Diyagnostik ekranı ve canlı "Kart durumları" bildirim akışı.

## Mac'iniz, Xcode'unuz veya Apple Developer hesabınız yoksa (Windows kullanıcıları)

Bu repo, **hiç Mac açmadan** çalışan bir kurulum içeriyor:

1. **Derleme** → GitHub Actions'ın barındırdığı bulut Mac'i (`.github/workflows/build-ipa.yml`)
   her push'ta `xcodegen` ile projeyi üretir, `xcodebuild` ile **imzasız**
   bir `.ipa` derler ve Actions sekmesinde indirilebilir bir artifact olarak
   bırakır. Siz hiçbir zaman Xcode açmıyorsunuz.
2. **Kurulum** → Windows'ta ücretsiz [Sideloadly](https://sideloadly.io)
   programını kullanarak, ücretsiz bir Apple ID ile, telefonunuzu USB'ye
   takıp bu `.ipa`'yı doğrudan iPhone'unuza kurarsınız. TestFlight'a,
   Xcode'a veya $99/yıllık Developer Program üyeliğine gerek yok.

### Adım adım

1. **GitHub'da derlemeyi çalıştırın**: Repo → **Actions** sekmesi →
   "Build unsigned IPA" workflow'u → **Run workflow** (veya `main`'e her
   push otomatik tetikler). Bitince açılan run'ın altındaki
   **Artifacts** bölümünden `SecureGateway-unsigned-ipa` dosyasını indirin
   (bir `.zip` içinde `.ipa` gelir).
2. **Sideloadly'yi kurun**: [sideloadly.io](https://sideloadly.io) →
   Windows sürümünü indirip kurun. Ayrıca bilgisayarınızda
   [iTunes/Apple Devices uygulaması](https://apps.microsoft.com/detail/9NP83LWLPZ9K)
   kurulu olmalı (Sideloadly, iPhone'u tanımak için Apple'ın USB
   sürücülerine ihtiyaç duyar).
3. **Ücretsiz bir Apple ID hazırlayın** (mevcut Apple ID'niz de olur,
   sadece kredi kartı/ödeme bilgisi gerekmez — tamamen ücretsiz "Personal
   Team" imzalama kullanılacak).
4. **iPhone'u USB ile bilgisayara bağlayın**, telefonda "Bu bilgisayara
   güven" uyarısını onaylayın.
5. Sideloadly'yi açın, üstte cihazınızı seçin, indirdiğiniz `.ipa`
   dosyasını sürükleyip bırakın, Apple ID'nizi girin, **Start** deyin.
   Uygulama telefona kurulur.
6. İlk açılışta telefonda **Ayarlar → Genel → VPN ve Cihaz Yönetimi**
   altından geliştirici profilinize "Güven" demeniz gerekir (ücretsiz
   imzalı her uygulamada bir kereye mahsus).
7. **Önemli kısıt**: Ücretsiz Apple ID ile imzalanan uygulamalar **7 gün
   sonra otomatik olarak açılmaz hale gelir**. Sideloadly'yi tekrar açıp
   aynı `.ipa` ile "Start" demeniz yeterli (yeni bir derleme gerekmez,
   1 dakika sürer). Kalıcı bir çözüm isterseniz Apple Developer Program'a
   ($99/yıl) üye olup gerçek TestFlight/ad-hoc dağıtımına geçebilirsiniz.
8. Bluetooth (CoreBluetooth central rolü — tarama/bağlanma/okuma/yazma) 
   ücretsiz imzalama ile **tam çalışır**, özel bir Apple yetkilendirmesi
   (entitlement) gerektirmez.

## Mac'iniz varsa: normal Xcode akışı

1. Xcode'da bu klasörü açın: **File → Open** → `project.yml`'in içinde
   bulunduğu klasörü seçmek yerine, önce bir kez
   `xcodegen generate` çalıştırın (Homebrew: `brew install xcodegen`) —
   bu, `SecureGateway.xcodeproj` dosyasını üretir. Ardından o `.xcodeproj`
   dosyasını Xcode'da açın.
2. **Signing & Capabilities**'te kendi Apple ID / geliştirici takımınızı
   seçin (`project.yml` içindeki `DEVELOPMENT_TEAM` boş bırakıldı, Xcode
   otomatik dolduracak).
3. Gerçek bir iPhone'a bağlayıp **⌘R** ile çalıştırın.

   > CoreBluetooth Simulator'de çalışmaz — mutlaka fiziksel iPhone gerekir.

## Firmware tarafında ne değişti? (`fota-vcu-firmware` reposunda)

Ekran görüntülerindeki "Kart durumları" kutusu (`OTA: OTA_PROGRESS (%20)`,
`Wi-Fi: WiFi bağlandı. IP: ...`, `OTA: VCU CAN flash hatasi` gibi anlık
metinler) eskiden **sadece seri log'a** yazılıyordu, BLE üzerinden hiç
gönderilmiyordu. `fota-vcu-firmware` reposundaki `main/ble_handler.c`'ye
4. bir characteristic (NOTIFY, `...cd126`) eklendi ve `wifi_handler.c` /
`ota_handler.c` bu yeni `ble_notify_status()` fonksiyonunu WiFi bağlantı
sonucu, self-OTA ilerlemesi ve VCU CAN flash sonucu için çağırıyor. Bu
uygulamanın "Kart durumları" ve OTA ilerleme çubuğunun canlı çalışması
için **gateway'in bu güncel firmware ile yeniden flashlanması gerekir**.
WiFi gönderimi, OTA tetikleme ve araç telemetrisi (soc/tork/vites/DTC)
zaten eski firmware ile de çalışır.

## GATT protokolü (referans)

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

## Dosya haritası

```
.
├── project.yml                        // XcodeGen spesifikasyonu (.xcodeproj bundan üretilir)
├── .github/workflows/build-ipa.yml    // Bulut Mac'te imzasız .ipa derleyen CI
└── SecureGateway/SecureGateway/
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
    └── Resources/Info.plist              // İzin metinleri + arkaplan modu
```

## Bilinçli olarak eklenmeyenler

- **Kilidi aç / Kilitle**: Firmware'de (CAN handler / UDS client) buna
  karşılık gelen bir komut yok. Butonlar arayüzde duruyor ama basınca
  "desteklenmiyor" uyarısı veriyor — sahte bir işlevmiş gibi göstermek
  yerine dürüst davranıldı. VCU tarafında bir kilit CAN mesajı/ID
  tanımlanırsa (örn. `can_handler.h`'ye yeni bir `CAN_ID_LOCK_CMD`), bu
  buton kolayca gerçek bir komuta bağlanabilir.
