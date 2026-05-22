<?php
/**
 * BambuStudio Fork — Slicer Update API
 *
 * Endpoint: GET /v1/iot-service/api/slicer/resource
 *
 * Query params sent by BambuStudio:
 *   distro        — Linux distro ID from /etc/os-release (debian, ubuntu, arch, fedora, opensuse, ...)
 *   arch          — CPU architecture (x86_64, aarch64, ...)
 *   name          — "slicer" (from check_new_version path)
 *   version       — current installed slicer version
 *   slicer        — current installed slicer version (from PresetUpdater path)
 *   network_plugin — current network plugin version
 *
 * Returns Bambulab-compatible JSON.
 */

header('Content-Type: application/json');
header('Cache-Control: max-age=300');

const GITHUB_REPO    = 'BenJule/BambuStudio';
const GITHUB_API_URL = 'https://api.github.com/repos/' . GITHUB_REPO . '/releases/latest';

// Distro → asset filename pattern (matched against GitHub release asset names)
const DISTRO_PATTERNS = [
    'debian'   => '_debian-bookworm_deb_',
    'ubuntu'   => '_ubuntu',           // matches ubuntu2204 / ubuntu2404
    'arch'     => '_arch_pkg_',
    'fedora'   => '_fedora41_rpm_',
    'opensuse' => '_opensuse-tumbleweed_',
    'sles'     => '_opensuse-tumbleweed_',
    'default'  => '_appimage_',        // fallback: AppImage works on any distro
];

function fail(string $msg): void {
    echo json_encode(['message' => 'error', 'error' => $msg]);
    exit;
}

function fetch_latest_release(): array {
    $ctx = stream_context_create(['http' => [
        'method'  => 'GET',
        'header'  => "User-Agent: BambuStudio-ForkUpdateAPI/1.0\r\n" .
                     "Accept: application/vnd.github+json\r\n",
        'timeout' => 10,
    ]]);
    $body = @file_get_contents(GITHUB_API_URL, false, $ctx);
    if ($body === false) fail('GitHub API unreachable');
    $data = json_decode($body, true);
    if (!$data || empty($data['tag_name'])) fail('No release found');
    return $data;
}

function pick_asset(array $assets, string $distro, string $arch): ?array {
    $pattern = DISTRO_PATTERNS[$distro] ?? DISTRO_PATTERNS['default'];

    // Prefer architecture-specific match, then fall back to any match
    $best = null;
    foreach ($assets as $asset) {
        $name = $asset['name'];
        if (stripos($name, $pattern) === false) continue;
        if ($arch === 'aarch64' || $arch === 'arm64') {
            if (stripos($name, 'arm') !== false || stripos($name, 'aarch64') !== false) {
                return $asset; // exact arch match
            }
        } else {
            // x86_64 — prefer assets without arm in name
            if (stripos($name, 'arm') === false && stripos($name, 'aarch') === false) {
                $best = $asset;
            }
        }
    }
    // Fall back to AppImage if no distro-specific asset found
    if ($best === null) {
        foreach ($assets as $asset) {
            if (stripos($asset['name'], DISTRO_PATTERNS['default']) !== false) {
                $best = $asset;
                break;
            }
        }
    }
    return $best;
}

function version_from_tag(string $tag): string {
    // Tags like "v01.10.02.00" → "01.10.02.00"
    return ltrim($tag, 'vV');
}

// --- Main ---

$distro  = strtolower(trim($_GET['distro'] ?? 'default'));
$arch    = strtolower(trim($_GET['arch']   ?? 'x86_64'));
$current = trim($_GET['version'] ?? $_GET['slicer'] ?? '');

$release = fetch_latest_release();
$latest  = version_from_tag($release['tag_name']);
$asset   = pick_asset($release['assets'] ?? [], $distro, $arch);

// If no update needed
if ($current !== '' && version_compare($latest, $current, '<=')) {
    echo json_encode(['message' => 'success', 'software' => []]);
    exit;
}

if ($asset === null) {
    // No matching asset — still report version, link to releases page
    $download_url = "https://github.com/" . GITHUB_REPO . "/releases/tag/" . $release['tag_name'];
} else {
    $download_url = $asset['browser_download_url'];
}

$description = $release['body'] ?? '';
// Trim release notes to first 500 chars to avoid UI overflow
if (strlen($description) > 500) {
    $description = substr($description, 0, 497) . '...';
}

// Respond in Bambulab format — supports both check_new_version() and sync_config() callers
echo json_encode([
    'message'  => 'success',
    'software' => [
        'version'      => $latest,
        'url'          => $download_url,
        'description'  => $description,
        'force_update' => false,
    ],
    'resources' => [[
        'type'         => 'slicer',
        'version'      => $latest,
        'url'          => $download_url,
        'description'  => $description,
        'force_update' => false,
    ]],
], JSON_UNESCAPED_SLASHES | JSON_PRETTY_PRINT);
