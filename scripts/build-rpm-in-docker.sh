#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SPEC_FILE="${PROJECT_ROOT}/qt-novnc-platform-plugin.spec"
RPM_DIST_DIR="${PROJECT_ROOT}/rpm-dist"

if [[ ! -f "${SPEC_FILE}" ]]; then
    echo "Spec file not found at ${SPEC_FILE}" >&2
    exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
    echo "docker is required to run this script" >&2
    exit 1
fi

VERSION="$(awk '/^Version:/ { print $2; exit }' "${SPEC_FILE}")"
if [[ -z "${VERSION}" ]]; then
    echo "Unable to determine Version from ${SPEC_FILE}" >&2
    exit 1
fi

SOURCE_ARCHIVE_BASENAME="v${VERSION}.tar.gz"
SOURCE_ARCHIVE="${PROJECT_ROOT}/${SOURCE_ARCHIVE_BASENAME}"

create_source_archive() {
    local tmp_dir exclude_file
    tmp_dir="$(mktemp -d)"
    exclude_file="$(mktemp)"

    cleanup() {
        rm -rf "$tmp_dir"
        rm -f "$exclude_file"
    }

    trap cleanup EXIT

    {
        echo ".git/"
        # Always exclude the archive we are about to create
        echo "${SOURCE_ARCHIVE_BASENAME}"

        if [[ -f "${PROJECT_ROOT}/.gitignore" ]]; then
            while IFS= read -r line; do
                [[ "$line" =~ ^[[:space:]]*$ ]] && continue
                [[ "$line" =~ ^[[:space:]]*# ]] && continue
                printf '%s\n' "$line"
            done < "${PROJECT_ROOT}/.gitignore"
        fi
    } > "${exclude_file}"

    local staging="${tmp_dir}/qt-novnc-platform-plugin-${VERSION}"
    mkdir -p "${staging}"

    rsync -a --delete \
        --exclude-from="${exclude_file}" \
        "${PROJECT_ROOT}/" "${staging}/"

    tar -C "${tmp_dir}" -czf "${SOURCE_ARCHIVE}" "qt-novnc-platform-plugin-${VERSION}"
    echo "Rebuilt ${SOURCE_ARCHIVE_BASENAME} including current workspace changes."
    cleanup
    trap - EXIT
}

if [[ -f "${SOURCE_ARCHIVE}" ]]; then
    rm -f "${SOURCE_ARCHIVE}"
fi
create_source_archive

mkdir -p "${RPM_DIST_DIR}"

HOST_UID="$(id -u)"
HOST_GID="$(id -g)"

declare -a TARGETS=(
    "rockylinux:9|el9"
    "almalinux:10|el10"
)

for target in "${TARGETS[@]}"; do
    IMAGE="${target%%|*}"
    DIST_TAG="${target##*|}"

    echo ""
    echo "=== Building RPMs for ${IMAGE} (${DIST_TAG}) ==="
    mkdir -p "${RPM_DIST_DIR}/${DIST_TAG}/RPMS" "${RPM_DIST_DIR}/${DIST_TAG}/SRPMS"

    docker run --rm -i \
        -v "${PROJECT_ROOT}:/src" \
        -w /src \
        -e SPEC_FILE_BASENAME="$(basename "${SPEC_FILE}")" \
        -e SOURCE_ARCHIVE_BASENAME="${SOURCE_ARCHIVE_BASENAME}" \
        -e DIST_TAG="${DIST_TAG}" \
        -e RPM_OUTPUT_DIR="/src/rpm-dist" \
        -e HOST_UID="${HOST_UID}" \
        -e HOST_GID="${HOST_GID}" \
        "${IMAGE}" /bin/bash - <<'EOF'
set -euo pipefail

dnf -y install dnf-plugins-core rpm-build rpmdevtools
dnf -y config-manager --set-enabled crb >/dev/null 2>&1 || true
dnf -y builddep "/src/${SPEC_FILE_BASENAME}"

rpmdev-setuptree
cp "/src/${SPEC_FILE_BASENAME}" "/root/rpmbuild/SPECS/"
cp "/src/${SOURCE_ARCHIVE_BASENAME}" "/root/rpmbuild/SOURCES/"

rpmbuild -ba "/root/rpmbuild/SPECS/${SPEC_FILE_BASENAME}"

mkdir -p "${RPM_OUTPUT_DIR}/${DIST_TAG}/SRPMS" "${RPM_OUTPUT_DIR}/${DIST_TAG}/RPMS"
cp /root/rpmbuild/SRPMS/*.rpm "${RPM_OUTPUT_DIR}/${DIST_TAG}/SRPMS/"
find /root/rpmbuild/RPMS -name '*.rpm' -exec cp {} "${RPM_OUTPUT_DIR}/${DIST_TAG}/RPMS/" \;

chown -R "${HOST_UID}:${HOST_GID}" "${RPM_OUTPUT_DIR}/${DIST_TAG}"
EOF
done

echo ""
echo "RPM artifacts copied to ${RPM_DIST_DIR}/{el9,el10}"
