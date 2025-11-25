#!/usr/bin/env bash
set -euo pipefail

# Simple helper to build qt-novnc-platform-plugin RPMs natively on
# RHEL-compatible distributions (RHEL, AlmaLinux, Rocky, etc.).

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
SPEC_FILE="${REPO_ROOT}/qt-novnc-platform-plugin.spec"
RPM_TOPDIR="${REPO_ROOT}/.rpmbuild"
RPM_DIST="${REPO_ROOT}/rpm-dist"

usage() {
    cat <<'EOF'
Usage: scripts/build-rpm.sh [--skip-deps]

Builds qt-novnc-platform-plugin RPMs on the current RHEL-like host.
By default the script installs the required build dependencies via dnf.

Options:
  --skip-deps   Skip dependency installation (assumes they are present).
EOF
}

SKIP_DEPS=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-deps)
            SKIP_DEPS=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ ! -f "${SPEC_FILE}" ]]; then
    echo "Spec file not found at ${SPEC_FILE}" >&2
    exit 1
fi

if [[ ! -f /etc/os-release ]]; then
    echo "/etc/os-release not found; cannot detect distribution" >&2
    exit 1
fi

# shellcheck disable=SC1091
source /etc/os-release
OS_ID=${ID:-}
OS_VERSION=${VERSION_ID:-}

if [[ -z "${OS_ID}" || -z "${OS_VERSION}" ]]; then
    echo "Unable to determine OS information" >&2
    exit 1
fi

MAJOR_VERSION=${OS_VERSION%%.*}

case "${OS_ID}" in
    rhel|almalinux|rocky|ol|centos)
        ;;
    *)
        echo "Warning: ${OS_ID} is not a recognized RHEL-like distribution." >&2
        ;;
esac

DNF_BIN=$(command -v dnf || true)
if [[ -z "${DNF_BIN}" ]]; then
    echo "dnf not found; ensure this is a RHEL-style system with dnf available." >&2
    exit 1
fi

SUDO_BIN=""
if [[ ${EUID} -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO_BIN="sudo"
    else
        echo "This script needs root privileges to install dependencies. Run as root or install sudo." >&2
        exit 1
    fi
fi

enable_crb() {
    if [[ -x /usr/bin/crb ]]; then
        ${SUDO_BIN} crb enable || true
    elif ${DNF_BIN} -q repolist crb >/dev/null 2>&1; then
        ${SUDO_BIN} ${DNF_BIN} config-manager --set-enabled crb || true
    fi
}

install_build_deps() {
    if [[ ${SKIP_DEPS} -eq 1 ]]; then
        return
    fi

    ${SUDO_BIN} ${DNF_BIN} -y install dnf-plugins-core >/dev/null 2>&1 || true

    if (( MAJOR_VERSION < 10 )); then
        enable_crb
    fi

    base_pkgs=(
        rpm-build
        cmake
        make
        gcc-c++
        git
        tar
        zlib-devel
    )

    if (( MAJOR_VERSION < 10 )); then
        qt_pkgs=(
            qt5-qtbase-devel
            qt5-qtbase-private-devel
            qt5-qtbase-static
            qt5-qtwebsockets-devel
        )
    else
        qt_pkgs=(
            qt6-qtbase-devel
            qt6-qtbase-private-devel
            qt6-qtbase-static
            qt6-qtwebsockets-devel
        )
    fi

    echo ">> Installing build dependencies (requires root privileges)"
    ${SUDO_BIN} ${DNF_BIN} -y install "${base_pkgs[@]}" "${qt_pkgs[@]}"
}

create_source_archive() {
    local version archive prefix
    version=$(awk '/^Version:/ { print $2; exit }' "${SPEC_FILE}")
    if [[ -z "${version}" ]]; then
        echo "Unable to determine package version from spec file" >&2
        exit 1
    fi

    archive="${REPO_ROOT}/v${version}.tar.gz"
    prefix="qt-novnc-platform-plugin-${version}"

    echo ">> Creating source archive ${archive}"
    rm -f "${archive}"
    git -C "${REPO_ROOT}" archive \
        --format=tar.gz \
        --prefix="${prefix}/" \
        -o "${archive}" HEAD
}

setup_topdir() {
    mkdir -p "${RPM_TOPDIR}"/{BUILD,RPMS,SRPMS,SOURCES,SPECS} "${RPM_DIST}"
}

run_rpmbuild() {
    echo ">> Running rpmbuild"
    rpmbuild -ba "${SPEC_FILE}" \
        --define "_topdir ${RPM_TOPDIR}" \
        --define "_sourcedir ${REPO_ROOT}" \
        --define "_specdir ${REPO_ROOT}" \
        --define "_srcrpmdir ${RPM_DIST}" \
        --define "_rpmdir ${RPM_DIST}" \
        --define "_builddir ${RPM_TOPDIR}/BUILD"
}

install_build_deps
create_source_archive
setup_topdir
run_rpmbuild

echo ">> RPM artifacts available under ${RPM_DIST}"
