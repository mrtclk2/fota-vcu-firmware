import SwiftUI

struct SettingsSheet: View {
    @EnvironmentObject private var ble: BLEManager
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                Section("Bağlantı") {
                    LabeledContent("Durum", value: ble.connectionState.label)
                    if let name = ble.connectedPeripheralName {
                        LabeledContent("Cihaz", value: name)
                    }
                    if ble.connectionState == .connected {
                        Button("Bağlantıyı kes", role: .destructive) {
                            ble.disconnect()
                        }
                    }
                }
                Section("Hakkında") {
                    LabeledContent("Uygulama", value: "Secure Gateway")
                    LabeledContent("BLE servisi", value: GatewayProtocol.serviceUUID.uuidString)
                }
            }
            .navigationTitle("Ayarlar")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Kapat") { dismiss() }
                }
            }
        }
    }
}
