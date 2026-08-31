import SwiftUI

@main
struct SecureGatewayApp: App {
    @StateObject private var ble = BLEManager()

    var body: some Scene {
        WindowGroup {
            VehicleHomeView()
                .environmentObject(ble)
        }
    }
}
