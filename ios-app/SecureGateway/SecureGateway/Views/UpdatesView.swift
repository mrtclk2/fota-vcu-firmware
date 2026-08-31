import SwiftUI

/// "Güncellemeler" ekranı — kendi (ESP32 gateway) firmware'ini ve VCU
/// firmware'ini OTA characteristic üzerinden tetikler.
struct UpdatesView: View {
    @EnvironmentObject private var ble: BLEManager

    @State private var selfURL = ""
    @State private var vcuURL = ""
    @State private var isBusy = false

    var body: some View {
        ScrollView {
            VStack(spacing: 18) {
                if let progress = ble.otaProgress {
                    OTAProgressCard(progress: progress)
                }

                updateCard(
                    icon: "cpu",
                    title: "Gateway (ESP32) firmware",
                    subtitle: "Kartın kendi yazılımını Wi-Fi üzerinden HTTPS ile indirir ve günceller.",
                    url: $selfURL,
                    buttonTitle: "Gateway'i güncelle"
                ) {
                    await ble.startSelfOTA(url: selfURL)
                }

                updateCard(
                    icon: "car.side",
                    title: "VCU firmware",
                    subtitle: "Firmware Wi-Fi ile indirilir, ardından CAN/UDS hattı üzerinden VCU'ya flashlanır.",
                    url: $vcuURL,
                    buttonTitle: "VCU'yu güncelle"
                ) {
                    await ble.startVCUOTA(url: vcuURL)
                }

                Button {
                    Task { await ble.requestOTAStatus() }
                } label: {
                    Label("Durumu sorgula", systemImage: "arrow.clockwise")
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 10)
                }
                .buttonStyle(.bordered)

                statusLogCard
            }
            .padding()
        }
        .navigationTitle("Güncellemeler")
        .navigationBarTitleDisplayMode(.inline)
        .disabled(ble.connectionState != .connected)
        .overlay {
            if ble.connectionState != .connected {
                ContentUnavailableCompat(
                    title: "Gateway'e bağlı değilsiniz",
                    message: "Güncelleme başlatmak için önce Bluetooth ile bağlanın."
                )
            }
        }
    }

    private func updateCard(icon: String, title: String, subtitle: String,
                             url: Binding<String>, buttonTitle: String,
                             action: @escaping () async -> Void) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 10) {
                Image(systemName: icon)
                Text(title).font(.headline)
            }
            Text(subtitle).font(.footnote).foregroundStyle(.secondary)
            TextField("https://.../firmware.bin", text: url)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .keyboardType(.URL)
                .padding(10)
                .background(Color(.tertiarySystemFill))
                .clipShape(RoundedRectangle(cornerRadius: 10))

            Button {
                isBusy = true
                Task {
                    await action()
                    isBusy = false
                }
            } label: {
                Text(buttonTitle).frame(maxWidth: .infinity).padding(.vertical, 10)
            }
            .buttonStyle(.borderedProminent)
            .tint(.black)
            .disabled(url.wrappedValue.trimmingCharacters(in: .whitespaces).count < 10 || isBusy)
        }
        .padding(16)
        .background(Color(.secondarySystemBackground))
        .clipShape(RoundedRectangle(cornerRadius: 16))
    }

    private var statusLogCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Kart durumları").font(.headline)
            if ble.statusLog.isEmpty {
                Text("Henüz bir durum bildirimi alınmadı.")
                    .font(.footnote).foregroundStyle(.secondary)
            } else {
                ForEach(Array(ble.statusLog.suffix(10).reversed()), id: \.self) { line in
                    Text(line).font(.footnote.monospaced())
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(16)
        .background(Color(.secondarySystemBackground))
        .clipShape(RoundedRectangle(cornerRadius: 16))
    }
}

private struct OTAProgressCard: View {
    let progress: Double

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("OTA ilerliyor").font(.headline)
            ProgressView(value: progress)
                .tint(.black)
            Text("%\(Int(progress * 100))")
                .font(.footnote).foregroundStyle(.secondary)
        }
        .padding(16)
        .background(Color(.secondarySystemBackground))
        .clipShape(RoundedRectangle(cornerRadius: 16))
    }
}

/// iOS 16 uyumluluğu için basit bir `ContentUnavailableView` yerine geçen görünüm.
private struct ContentUnavailableCompat: View {
    let title: String
    let message: String

    var body: some View {
        VStack(spacing: 8) {
            Image(systemName: "antenna.radiowaves.left.and.right.slash")
                .font(.largeTitle)
                .foregroundStyle(.secondary)
            Text(title).font(.headline)
            Text(message).font(.footnote).foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
        }
        .padding(24)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(.regularMaterial)
    }
}
