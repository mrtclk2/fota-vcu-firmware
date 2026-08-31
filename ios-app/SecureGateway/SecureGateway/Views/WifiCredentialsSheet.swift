import SwiftUI

/// SSID / şifre giriş formu. Gönderildiğinde `WIFI:ssid,pass` komutu
/// BLE üzerinden gateway'e yazılır (bkz. `ble_handler.c: wifi_chr_access`).
struct WifiCredentialsSheet: View {
    @EnvironmentObject private var ble: BLEManager
    @Environment(\.dismiss) private var dismiss

    @State private var ssid = ""
    @State private var password = ""
    @State private var isSending = false

    private var canSend: Bool {
        !ssid.trimmingCharacters(in: .whitespaces).isEmpty && password.count >= 8
    }

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    TextField("Ağ adı (SSID)", text: $ssid)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    SecureField("Şifre (en az 8 karakter)", text: $password)
                } footer: {
                    Text("Seçilen ağ `WIFI:ssid,pass` formatında BLE üzerinden gönderilir.")
                }

                if let lastStatus = ble.lastStatus {
                    Section("Son durum") {
                        Text(lastStatus).font(.footnote).foregroundStyle(.secondary)
                    }
                }
            }
            .navigationTitle("Wi-Fi bilgileri")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button("Vazgeç") { dismiss() }
                }
                ToolbarItem(placement: .topBarTrailing) {
                    Button {
                        Task {
                            isSending = true
                            await ble.sendWifiCredentials(ssid: ssid, password: password)
                            isSending = false
                            dismiss()
                        }
                    } label: {
                        if isSending { ProgressView() } else { Text("Gönder") }
                    }
                    .disabled(!canSend || isSending)
                }
            }
        }
    }
}
