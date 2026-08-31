import SwiftUI

/// "Diyagnostik" ekranı — VCU'dan gelen anlık DTC / HVIL / vites / fren
/// bilgilerini gösterir (kaynak: Vehicle Data notify JSON'u).
struct DiagnosticsView: View {
    @EnvironmentObject private var ble: BLEManager

    private var data: VehicleData { ble.vehicleData }

    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                faultBanner

                grid

                VStack(alignment: .leading, spacing: 8) {
                    Text("Kimlik").font(.headline)
                    row("Firmware sürümü", data.fwVer)
                    row("Bağlı cihaz", ble.connectedPeripheralID?.uuidString ?? "Bağlı değil")
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(16)
                .background(Color(.secondarySystemBackground))
                .clipShape(RoundedRectangle(cornerRadius: 16))
            }
            .padding()
        }
        .navigationTitle("Diyagnostik")
        .navigationBarTitleDisplayMode(.inline)
    }

    private var faultBanner: some View {
        HStack(spacing: 10) {
            Image(systemName: data.hasFault ? "exclamationmark.triangle.fill" : "checkmark.seal.fill")
            Text(data.hasFault ? "Aktif arıza kodu tespit edildi" : "Aktif arıza yok")
                .font(.subheadline.weight(.semibold))
            Spacer()
        }
        .padding(14)
        .foregroundStyle(data.hasFault ? Color.red : Color.green)
        .background((data.hasFault ? Color.red : Color.green).opacity(0.12))
        .clipShape(RoundedRectangle(cornerRadius: 14))
    }

    private var grid: some View {
        LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
            DiagTile(title: "DTC", value: data.dtc, tint: data.dtc == "NONE" ? .secondary : .red)
            DiagTile(title: "HVIL", value: data.hvil, tint: data.hvil == "OK" ? .secondary : .red)
            DiagTile(title: "Vites", value: data.gearLabel, tint: .primary)
            DiagTile(title: "Fren", value: data.brake ? "Basılı" : "Serbest", tint: .primary)
            DiagTile(title: "Gaz pedalı", value: "%\(data.throttle)", tint: .primary)
            DiagTile(title: "Araç modu", value: VehicleState(rawValue: data.state)?.label ?? "Bilinmiyor", tint: .primary)
        }
    }

    private func row(_ title: String, _ value: String) -> some View {
        HStack {
            Text(title).font(.subheadline).foregroundStyle(.secondary)
            Spacer()
            Text(value).font(.subheadline.weight(.semibold)).lineLimit(1).minimumScaleFactor(0.6)
        }
    }
}

private struct DiagTile: View {
    let title: String
    let value: String
    let tint: Color

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title).font(.caption).foregroundStyle(.secondary)
            Text(value).font(.title3.weight(.semibold)).foregroundStyle(tint)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(14)
        .background(Color(.secondarySystemBackground))
        .clipShape(RoundedRectangle(cornerRadius: 14))
    }
}
