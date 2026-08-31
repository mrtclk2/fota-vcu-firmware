import SwiftUI
import UIKit
import PhotosUI

/// "Araç fotoğrafı" seçim sheet'i — ekran görüntüsündeki tasarımla birebir.
struct VehiclePhotoSheet: View {
    @EnvironmentObject private var photoStore: VehiclePhotoStore
    @Environment(\.dismiss) private var dismiss

    @State private var photosPickerItem: PhotosPickerItem?
    @State private var showCamera = false

    var body: some View {
        VStack(spacing: 24) {
            Capsule()
                .fill(.tertiary)
                .frame(width: 40, height: 5)
                .padding(.top, 8)

            HStack(alignment: .top, spacing: 14) {
                RoundedRectangle(cornerRadius: 16)
                    .fill(Color.black)
                    .frame(width: 52, height: 52)
                    .overlay(
                        Image(systemName: "camera.fill")
                            .foregroundStyle(.white)
                            .font(.title3)
                    )
                VStack(alignment: .leading, spacing: 4) {
                    Text("Araç fotoğrafı")
                        .font(.title3.bold())
                    Text("Kamerayı kullanın veya galeriden bir görsel seçin. Fotoğraf bu cihazda önbelleğe alınır.")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                }
                Spacer(minLength: 0)
            }
            .padding(.horizontal)

            VStack(spacing: 0) {
                PhotosPicker(selection: $photosPickerItem, matching: .images) {
                    optionRow(icon: "photo.on.rectangle",
                              title: "Galeriden seç",
                              subtitle: "Cihazınızdaki mevcut bir görseli seçin.")
                }
                Divider().padding(.leading, 70)
                Button {
                    showCamera = true
                } label: {
                    optionRow(icon: "camera",
                              title: "Foto çek",
                              subtitle: "Kamerayı açın ve aracınızın fotoğrafını çekin.")
                }
            }
            .background(Color(.secondarySystemBackground))
            .clipShape(RoundedRectangle(cornerRadius: 18))
            .padding(.horizontal)

            Button("Vazgeç") { dismiss() }
                .font(.body.weight(.semibold))
                .padding(.top, 4)

            Spacer(minLength: 8)
        }
        .presentationDetents([.fraction(0.42)])
        .sheet(isPresented: $showCamera) {
            CameraPicker { image in
                photoStore.save(image)
            }
            .ignoresSafeArea()
        }
        .onChange(of: photosPickerItem) { _, newItem in
            Task {
                if let data = try? await newItem?.loadTransferable(type: Data.self),
                   let image = UIImage(data: data) {
                    photoStore.save(image)
                    dismiss()
                }
            }
        }
    }

    private func optionRow(icon: String, title: String, subtitle: String) -> some View {
        HStack(spacing: 14) {
            RoundedRectangle(cornerRadius: 12)
                .fill(Color(.tertiarySystemFill))
                .frame(width: 44, height: 44)
                .overlay(Image(systemName: icon).foregroundStyle(.primary))
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.body.weight(.semibold)).foregroundStyle(.primary)
                Text(subtitle).font(.footnote).foregroundStyle(.secondary)
            }
            Spacer()
            Image(systemName: "chevron.right")
                .font(.footnote.weight(.semibold))
                .foregroundStyle(.tertiary)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 14)
        .contentShape(Rectangle())
    }
}
