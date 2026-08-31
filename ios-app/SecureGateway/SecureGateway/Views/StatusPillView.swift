import SwiftUI

struct StatusPillView: View {
    let state: ConnectionState

    var body: some View {
        HStack(spacing: 6) {
            Circle().fill(state.tint).frame(width: 8, height: 8)
            Text(state.label).font(.footnote.weight(.semibold))
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(.ultraThinMaterial)
        .clipShape(Capsule())
        .overlay(Capsule().strokeBorder(state.tint.opacity(0.3), lineWidth: 1))
    }
}
