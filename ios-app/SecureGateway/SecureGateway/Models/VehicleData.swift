import Foundation

/// ESP32 secure gateway'in Vehicle Data characteristic'i (NOTIFY) üzerinden
/// gönderdiği JSON ile birebir eşleşir. Bkz. `main/vehicle_data.c` →
/// `vehicle_data_to_json()`.
struct VehicleData: Codable, Equatable {
    var soc: Int
    var state: Int
    var gear: String
    var torque: Int
    var throttle: Int
    var brake: Bool
    var dtc: String
    var hvil: String
    var fwVer: String

    enum CodingKeys: String, CodingKey {
        case soc, state, gear, torque, throttle, brake, dtc, hvil
        case fwVer = "fw_ver"
    }

    static let unknown = VehicleData(
        soc: 0, state: 1, gear: "N", torque: 0, throttle: 0,
        brake: false, dtc: "NONE", hvil: "OK", fwVer: "Bilinmiyor"
    )

    var hasFault: Bool {
        dtc != "NONE" || hvil == "ALARM" || state == VehicleState.fault.rawValue
    }

    var gearLabel: String {
        switch gear {
        case "D": return "İleri (D)"
        case "R": return "Geri (R)"
        default:  return "Boşta (N)"
        }
    }
}

enum VehicleState: Int {
    case standby = 1
    case drive   = 2
    case reverse = 3
    case fault   = 4
    case charge  = 5

    var label: String {
        switch self {
        case .standby: return "Beklemede"
        case .drive:   return "Sürüşte"
        case .reverse: return "Geri Vites"
        case .fault:   return "Arıza"
        case .charge:  return "Şarj Oluyor"
        }
    }
}
