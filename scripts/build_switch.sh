
set -e

BUILD_DIR=${BUILD_DIR:-cmake-build-switch}


cd "$(dirname $0)/.."
git config --global --add safe.directory `pwd`

# Aggiorna i pacchetti nel container DevkitPro prima della build.
# GitHub Actions occasionally receives 403s from the devkitPro package index;
# SKIP_DKP_SYNC lets CI use the container's bundled package database instead.
if [ "${SKIP_DKP_SYNC}" != "true" ]; then
    dkp-pacman -Syu --noconfirm
fi

BASE_URL="https://github.com/xfangfang/wiliwili/releases/download/v0.1.0/"

PKGS=(
    "switch-libass-0.17.1-1-any.pkg.tar.zst"
    "switch-ffmpeg-6.1-5-any.pkg.tar.zst"
    "switch-libmpv-0.36.0-2-any.pkg.tar.zst"
    "switch-nspmini-48d4fc2-1-any.pkg.tar.xz"
    "hacBrewPack-3.05-1-any.pkg.tar.zst"
)
for PKG in "${PKGS[@]}"; do
    [ -f "${PKG}" ] || curl -LO ${BASE_URL}${PKG}
    dkp-pacman -U --noconfirm ${PKG}
done


if [ -z "${GA_ID}" ] || [ -z "${GA_KEY}" ]; then
    echo "GA_ID or GA_KEY not found in environment"
    exit 1
fi

if [ -z "${SERVER_URL}" ]; then
    echo "SERVER_URL not found in environment"
    exit 1
fi

if [ -z "${SERVER_TOKEN}" ]; then
    echo "SERVER_TOKEN not found in environment"
    exit 1
fi

# GITHUB_TOKEN is optional but pass it if available
GITHUB_TOKEN_FLAG=""
if [ -n "${GITHUB_TOKEN}" ]; then
    GITHUB_TOKEN_FLAG="-DGITHUB_TOKEN=\"${GITHUB_TOKEN}\""
fi

# M3U8_URL is optional. Leaving it empty makes the public build start without
# a bundled playlist, so users can enter their own M3U and XMLTV/EPG URLs.
M3U8_URL_FLAG=""
if [ -n "${M3U8_URL}" ]; then
    M3U8_URL_FLAG="-DM3U8_URL=\"${M3U8_URL}\""
fi

# Disable unity build by default for stability on Switch
# Can be re-enabled with ENABLE_UNITY_BUILD=true environment variable
UNITY_BUILD_FLAG="-DBRLS_UNITY_BUILD=OFF"
if [ "${ENABLE_UNITY_BUILD}" = "true" ]; then
    UNITY_BUILD_FLAG="-DBRLS_UNITY_BUILD=ON"
fi

cmake -B ${BUILD_DIR} \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILTIN_NSP=ON \
  -DPLATFORM_SWITCH=ON \
  ${UNITY_BUILD_FLAG} \
  -DCMAKE_UNITY_BUILD_BATCH_SIZE=16 \
  -DANALYTICS=ON \
  -DANALYTICS_ID="${GA_ID}" \
  -DANALYTICS_KEY="${GA_KEY}" \
  -DSERVER_URL="${SERVER_URL}" \
  -DSERVER_TOKEN="${SERVER_TOKEN}" \
  ${M3U8_URL_FLAG} \
  ${GITHUB_TOKEN_FLAG} 

APP_TARGET_NAME=${APP_TARGET_NAME:-PocketTV}
make -C ${BUILD_DIR} ${APP_TARGET_NAME}.nro -j$(nproc)
