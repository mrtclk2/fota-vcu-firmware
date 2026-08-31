import SwiftUI
import UIKit

struct VehicleHomeView: View {
    @EnvironmentObject private var ble: BLEManager
    @StateObject private var photoStore = VehiclePhotoStore()

    @State private var showPhotoSheet = false
    @State private var showBluetoothSheet = false
    @State private var showWifiSheet = false
    @State private var showSettingsSheet = false
    @State private var showLockNotSupported = false

    var body: some View {
        NavigationStack {
            ZStack(alignment: .bottomTrailing) {
                ScrollView {
                    VStack(spacing: 20) {
                        vehicleCard
                        tileRow
                        lockRow
                        actionRow
                        wifiCard
                        statusCard
                        identityCard
                    }
                    .padding()
                }
                .navigationTitle("Aracım")
                .navigationBarTitleDisplayMode(.inline)

                Button {
                    showSettingsSheet = true
                } label: {
                    Image(systemName: "gearshape.fill")
                        .font(.title3)
                        .foregroundStyle(.white)
                        .frame(width: 52, height: 52)
                        .background(Color.black)
                        .clipShape(Circle())
                        .shadow(radius: 4)
                }
                .padding()
            }
        }
        .environmentObject(photoStore)
        .sheet(isPresented: $showPhotoSheet) { VehiclePhotoSheet() }
        .sheet(isPresented: $showBluetoothSheet) { BluetoothConnectSheet() }
        .sheet(isPresented: $showWifiSheet) { WifiCredentialsSheet() }
        .sheet(isPresented: $showSettingsSheet) { SettingsSheet() }
        .alert("Desteklenmiyor", isPresented: $showLockNotSupported) {
            Button("Tamam", role: .cancel) {}
        } message: {
            Text("Kilit kontrolü şu anki gateway firmware'inde henüz uygulanmadı (CAN komutu tanımlı değil).")
        }
        .alert("Hata", isPresented: Binding(
            get: { ble.errorMessage != nil },
            set: { if !$0 { ble.errorMessage = nil } }
        )) {
            Button("Tamam", role: .cancel) {}
        } message: {
            Text(ble.errorMessage ?? "")
        }
    }

    // MARK: - Vehicle card

    private var vehicleCard: some View {
        VStack {
            ZStack(alignment: .top) {
                RoundedRectangle(cornerRadius: 20)
                    .fill(Color(.secondarySystemBackground))

                Group {
                    if let image = photoStore.image {
                        Image(uiImage: image)
                            .resizable()
                            .scaledToFill()
                    } else {
                        Image(systemName: "car.side.fill")
                            .resizable()
                            .scaledToFit()
                            .padding(48)
                            .foregroundStyle(.secondary)
                    }
                }
                .frame(height: 260)
                .clipShape(RoundedRectangle(cornerRadius: 20))

                HStack {
                    Button {
                        showPhotoSheet = true
                    } label: {
                        Image(systemName: "camera.fill")
                            .padding(10)
                            .background(.thinMaterial)
                            .clipShape(Circle())
                    }
                    Spacer()
                    Button {
                        showBluetoothSheet = true
                    } label: {
                        StatusPillView(state: ble.connectionState)
                    }
                }
                .padding(14)
            }
        }
        .frame(height: 260)
    }

    // MARK: - Telemetry tiles

    private var tileRow: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 12) {
                Tile(icon: "bolt.fill", tint: .green, value: "\(ble.vehicleData.soc)%", label: "Şarj durumu")
                Tile(icon: "gauge.with.dots.needle.50percent", tint: .mint, value: "\(ble.vehicleData.torque) Nm", label: "Tork")
                Tile(icon: "speedometer", tint: .blue, value: "%\(ble.vehicleData.throttle)", label: "Gaz pedalı")
                Tile(icon: "arrow.up.arrow.down.circle", tint: .purple, value: ble.vehicleData.gear, label: "Vites")
            }
        }
        .scrollClipDisabled()
    }

    // MARK: - Lock row

    private var lockRow: some View {
        HStack(spacing: 12) {
            Button {
                showLockNotSupported = true
            } label: {
                Label("Kilidi aç", systemImage: "lock.open.fill")
                    .frame(maxWidth: .infinity).padding(.vertical, 12)
            }
            .buttonStyle(.bordered)

            Button {
                showLockNotSupported = true
            } label: {
                Label("Kilitle", systemImage: "lock.fill")
                    .frame(maxWidth: .infinity).padding(.vertical, 12)
            }
            .buttonStyle(.bordered)
        }
    }

    // MARK: - Diagnostics / Updates row

    private var actionRow: some View {
        HStack(spacing: 12) {
            NavigationLink {
                DiagnosticsView()
            } label: {
                VStack(spacing: 8) {
                    Image(systemName: ble.vehicleData.hasFault ? "exclamationmark.circle.fill" : "checkmark.circle")
                        .font(.title2)
                        .foregroundStyle(ble.vehicleData.hasFault ? .red : .primary)
                    Text("Diyagnostik").font(.footnote.weight(.semibold))
                }
                .frame(maxWidth: .infinity).padding(.vertical, 16)
                .background(Color(.secondarySystemBackground))
                .clipShape(RoundedRectangle(cornerRadius: 16))
            }

            NavigationLink {
                UpdatesView()
            } label: {
                VStack(spacing: 8) {
                    Image(systemName: "arrow.down.circle")
                        .font(.title2)
                    Text("Güncellemeler").font(.footnote.weight(.semibold))
                }
                .frame(maxWidth: .infinity).padding(.vertical, 16)
                .background(Color(.secondarySystemBackground))
                .clipShape(RoundedRectangle(cornerRadius: 16))
            }
        }
        .foregroundStyle(.primary)
    }

    // MARK: - Wi-Fi card

    private var wifiCard: some View {
        InfoCard(
            icon: "wifi",
            title: "Kişisel erişim noktası",
            subtitle: "Kişisel erişim noktası bilgilerini karta gönderin. Seçilen ağ `WIFI:ssid,pass` formatında BLE üzerinden gönderilir."
        ) {
            Button {
                showWifiSheet = true
            } label: {
                Label("Wi-Fi bilgilerini gönder", systemImage: "wifi")
                    .frame(maxWidth: .infinity).padding(.vertical, 12)
            }
            .buttonStyle(.borderedProminent)
            .tint(.black)
            .disabled(ble.connectionState != .connected)
        }
    }

    // MARK: - Status card

    private var statusCard: some View {
        InfoCard(
            icon: "bluetooth",
            title: "Kart durumları",
            subtitle: "ESP32 tarafından BLE bildirimleriyle gönderilen son durum."
        ) {
            if let status = ble.lastStatus {
                HStack(spacing: 8) {
                    Image(systemName: "info.circle")
                    Text(status).font(.footnote)
                    Spacer()
                }
                .padding(10)
                .background(Color(.tertiarySystemFill))
                .clipShape(RoundedRectangle(cornerRadius: 10))

                if let progress = ble.otaProgress {
                    ProgressView(value: progress).tint(.black)
                }
            } else {
                Text("Henüz bildirim alınmadı.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
    }

    // MARK: - Identity card

    private var identityCard: some View {
        InfoCard(
            icon: "person.text.rectangle",
            title: "Kimlik",
            subtitle: "Araç ve gateway tarafından bildirilen değerler."
        ) {
            IdentityRow(icon: "cpu", title: "Firmware sürümü",
                        value: ble.connectionState == .connected ? ble.vehicleData.fwVer : "Bilinmiyor")
            IdentityRow(icon: "wave.3.right", title: "Bağlı cihaz",
                        value: ble.connectedPeripheralID?.uuidString ?? "—")
        }
    }
}

// MARK: - Reusable pieces

private struct Tile: View {
    let icon: String
    let tint: Color
    let value: String
    let label: String

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Image(systemName: icon).foregroundStyle(tint)
            Text(value).font(.title3.weight(.semibold))
            Text(label).font(.caption).foregroundStyle(.secondary)
        }
        .padding(14)
        .frame(width: 132, alignment: .leading)
        .background(Color(.secondarySystemBackground))
        .clipShape(RoundedRectangle(cornerRadius: 16))
    }
}

private struct InfoCard<Content: View>: View {
    let icon: String
    let title: String
    let subtitle: String
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(alignment: .top, spacing: 12) {
                RoundedRectangle(cornerRadius: 14)
                    .fill(Color.black)
                    .frame(width: 44, height: 44)
                    .overlay(Image(systemName: icon).foregroundStyle(.white))
                VStack(alignment: .leading, spacing: 2) {
                    Text(title).font(.headline)
                    Text(subtitle).font(.footnote).foregroundStyle(.secondary)
                }
            }
            content
        }
        .padding(16)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color(.secondarySystemBackground).opacity(0.4))
        .overlay(RoundedRectangle(cornerRadius: 18).strokeBorder(Color(.separator).opacity(0.3)))
        .clipShape(RoundedRectangle(cornerRadius: 18))
    }
}

private struct IdentityRow: View {
    let icon: String
    let title: String
    let value: String

    var body: some View {
        HStack(spacing: 10) {
            RoundedRectangle(cornerRadius: 10)
                .fill(Color(.tertiarySystemFill))
                .frame(width: 36, height: 36)
                .overlay(Image(systemName: icon).font(.footnote))
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.caption).foregroundStyle(.secondary)
                Text(value).font(.subheadline.weight(.semibold))
                    .lineLimit(1).minimumScaleFactor(0.6)
            }
            Spacer()
        }
        .padding(.vertical, 12).padding(.horizontal, 12)
        .background(Color(.tertiarySystemFill).opacity(0.5))
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }
}
