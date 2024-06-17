# Maintainer: dalto <dalto at fastmail.com>

pkgname=btrfs-assistant-bkmo
pkgver=2.1.4
pkgrel=1
pkgdesc="An application for managing BTRFS subvolumes and Snapper snapshots"
arch=('x86_64' 'aarch64')
url="https://github.com/bkmo/$pkgname"
license=('GPL3')
depends=('qt6-base' 'qt6-svg' 'ttf-font' 'polkit' 'util-linux' 'btrfs-progs' 'diffutils')
optdepends=('snapper' 'btrfsmaintenance')
makedepends=('git' 'cmake' 'qt6-tools')
conflicts=('btrfs-assistant' 'btrfs-assistant-git')
provides=('btrfs-assistant')
backup=(etc/btrfs-assistant.conf)
source=("$pkgname-$pkgver.tar.gz::$url/archive/refs/tags/$pkgver.tar.gz")
sha256sums=('SKIP')
build() {
	cd "$srcdir"
	cmake -B build -S "$pkgname-$pkgver" -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE='Release'
	make -C build
}

package() {
	make -C build DESTDIR="$pkgdir" install
}

