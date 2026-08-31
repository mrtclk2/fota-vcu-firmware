import SwiftUI
import UIKit

/// "Bluetooth bağlantısı" sheet'i — tarama listesi ve bağlanma akışı.
struct BluetoothConnectSheet: View {
    @EnvironmentObject private var ble: BLEManager
    @Environment(\.dismiss) private var dismiss
    @State private var selectedID: UUID?

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 18) {
                    HStack(alignment: .top, spacing: 14) {
                        RoundedRectangle(cornerRadius: 16)
                            .fill(Color.black)
                            .frame(width: 52, height: 52)
                            .overlay(Image(systemName: "wave.3.right")
                                .foregroundStyle(.white).font(.title3))
                        VStack(alignment: .leading, spacing: 4) {
                            Text("Bluetooth bağlantısı").font(.title3.bold())
                            Text("Aracı tarayın ve gateway cihazına bağlanın.")
                                .font(.subheadline).foregroundStyle(.secondary)
                        }
                        Spacer(minLength: 0)
                    }

                    StatusPillView(state: ble.connectionState)
                        .frame(maxWidth: .infinity, alignment: .leading)

                    if ble.connectionState == .scanning && ble.discoveredPeripherals.isEmpty {
                        HStack(spacing: 10) {
                            ProgressView()
                            Text("Yakındaki gateway cihazları taranıyor")
                                .font(.subheadline)
                                .foregroundStyle(.secondary)
                        }
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 14)
                        .background(Color(.secondarySystemBackground))
                        .clipShape(RoundedRectangle(cornerRadius: 14))
                    }

                    ForEach(ble.discoveredPeripherals) { peripheral in
                        DeviceCard(
                            peripheral: peripheral,
                            isSelected: selectedID == peripheral.id,
                            isConnecting: ble.connectionState == .connecting && selectedID == peripheral.id
                        ) {
                            selectedID = peripheral.id
                            ble.connect(to: peripheral.id)
                        }
                    }

                    if ble.discoveredPeripherals.isEmpty && ble.connectionState != .scanning {
                        Text("Cihaz bulunamadı. Aracın açık ve yakında olduğundan emin olun.")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                            .padding(.top, 8)
                    }
                }
                .padding()
            }
            .navigationTitle("")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Kapat") { dismiss() }
                }
                ToolbarItem(placement: .topBarLeading) {
                    Button {
                        ble.startScan()
                    } label: {
                        Image(systemName: "arrow.clockwise")
                    }
                }
            }
        }
        .onAppear { ble.startScan() }
        .onDisappear { ble.stopScan() }
        .onChange(of: ble.connectionState) { _, newValue in
            if newValue == .connected { dismiss() }
        }
    }
}

private struct DeviceCard: View {
    let peripheral: DiscoveredPeripheral
    let isSelected: Bool
    let isConnecting: Bool
    let onConnect: () -> Void

    var body: some View {
        VStack(spacing: 14) {
            HStack(spacing: 12) {
                RoundedRectangle(cornerRadius: 12)
                    .fill(Color.black)
                    .frame(width: 44, height: 44)
                    .overlay(Image(systemName: "cpu").foregroundStyle(.white))

                VStack(alignment: .leading, spacing: 2) {
                    Text(peripheral.name).font(.body.weight(.semibold))
                    Text(shortID(peripheral.id))
                        .font(.footnote.monospaced())
                        .foregroundStyle(.secondary)
                }
                Spacer()
                VStack(alignment: .trailing, spacing: 2) {
                    Text("\(peripheral.rssi) dBm").font(.footnote.weight(.semibold))
                    Text(peripheral.signalLabel)
                        .font(.caption2)
                        .foregroundStyle(peripheral.signalTint)
                }
                .padding(.horizontal, 10).padding(.vertical, 6)
                .background(peripheral.signalTint.opacity(0.15))
                .clipShape(Capsule())
            }

            HStack(spacing: 10) {
                InfoChip(title: "Cihaz kimliği", value: shortID(peripheral.id))
                InfoChip(title: "BLE servisi", value: GatewayProtocol.serviceUUID.uuidString)
            }

            Button(action: onConnect) {
                HStack {
                    if isConnecting {
                        ProgressView().tint(.white)
                    } else {
                        Image(systemName: "bolt.horizontal.fill")
                    }
                    Text(isConnecting ? "Bağlanıyor…" : "Bağlan")
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 12)
            }
            .buttonStyle(.borderedProminent)
            .tint(.black)
            .disabled(isConnecting)
        }
        .padding(14)
        .background(Color(.secondarySystemBackground))
        .clipShape(RoundedRectangle(cornerRadius: 16))
    }

    private func shortID(_ id: UUID) -> String {
        let s = id.uuidString
        guard s.count > 12 else { return s }
        return "\(s.prefix(8))...\(s.suffix(8))"
    }
}

private struct InfoChip: View {
    let title: String
    let value: String

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 4) {
                Text(title).font(.caption).foregroundStyle(.secondary)
                Spacer()
                Button {
                    UIPasteboard.general.string = value
                } label: {
                    Image(systemName: "doc.on.doc").font(.caption2)
                }
            }
            Text(value).font(.footnote.weight(.semibold)).lineLimit(1).minimumScaleFactor(0.7)
        }
        .padding(10)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color(.tertiarySystemFill))
        .clipShape(RoundedRectangle(cornerRadius: 10))
    }
}
