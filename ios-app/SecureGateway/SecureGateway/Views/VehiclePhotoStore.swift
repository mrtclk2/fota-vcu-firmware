import SwiftUI
import UIKit

/// Araç fotoğrafını cihaz diskinde önbelleğe alır (sunucuya yüklenmez).
@MainActor
final class VehiclePhotoStore: ObservableObject {
    @Published private(set) var image: UIImage?

    private var fileURL: URL {
        FileManager.default
            .urls(for: .cachesDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("vehicle_photo.jpg")
    }

    init() {
        load()
    }

    func save(_ image: UIImage) {
        guard let data = image.jpegData(compressionQuality: 0.85) else { return }
        try? data.write(to: fileURL, options: .atomic)
        self.image = image
    }

    private func load() {
        guard let data = try? Data(contentsOf: fileURL) else { return }
        image = UIImage(data: data)
    }
}
