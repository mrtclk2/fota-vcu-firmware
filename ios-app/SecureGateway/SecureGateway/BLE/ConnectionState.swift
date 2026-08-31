import Foundation
import SwiftUI

enum ConnectionState: Equatable {
    case idle
    case scanning
    case connecting
    case connected
    case disconnected

    var label: String {
        switch self {
        case .idle:         return "Beklemede"
        case .scanning:     return "Taranıyor"
        case .connecting:   return "Bağlanıyor"
        case .connected:    return "Bağlı"
        case .disconnected: return "Bağlantı kesildi"
        }
    }

    var tint: Color {
        switch self {
        case .idle, .disconnected: return .secondary
        case .scanning, .connecting: return .orange
        case .connected: return .green
        }
    }
}

struct DiscoveredPeripheral: Identifiable, Equatable {
    let id: UUID
    var name: String
    var rssi: Int

    var signalLabel: String {
        switch rssi {
        case ..<(-80): return "Zayıf sinyal"
        case -80..<(-60): return "Orta sinyal"
        default: return "İyi sinyal"
        }
    }

    var signalTint: Color {
        switch rssi {
        case ..<(-80): return .orange
        case -80..<(-60): return .yellow
        default: return .green
        }
    }
}
