param(
    [Parameter(Mandatory)] [string] $BuildDir,
    [Parameter(Mandatory)] [string] $SdlDir,
    [Parameter(Mandatory)] [string] $AndroidJar,
    [Parameter(Mandatory)] [string] $BuildToolsDir,
    [Parameter(Mandatory)] [string] $OutputApk,
    [string] $ApplicationId = 'org.tpwport.android',
    [ValidateRange(1, 2147483647)] [int] $VersionCode,
    [string] $VersionName,
    [string] $ReleaseKeyStore,
    [string] $ReleaseKeyAlias,
    [ValidateSet('arm64-v8a', 'x86_64')] [string] $Abi = 'arm64-v8a'
)

$ErrorActionPreference = 'Stop'

function Invoke-Tool {
    param([string] $Program, [string[]] $Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program exited with code $LASTEXITCODE" }
}

$build = (Resolve-Path -LiteralPath $BuildDir).Path
$source = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$sdl = (Resolve-Path -LiteralPath $SdlDir).Path
$tools = (Resolve-Path -LiteralPath $BuildToolsDir).Path
$platform = (Resolve-Path -LiteralPath $AndroidJar).Path
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputApk)
$classes = Join-Path $build 'apk-classes'
$libraries = Join-Path $build 'apk-libs'
New-Item -ItemType Directory -Force -Path $classes | Out-Null
New-Item -ItemType Directory -Force -Path $libraries | Out-Null
$strip = (Select-String -LiteralPath (Join-Path $build 'CMakeCache.txt') -Pattern '^CMAKE_STRIP:FILEPATH=(.+)$').Matches.Groups[1].Value
$toolchain = (Select-String -LiteralPath (Join-Path $build 'CMakeCache.txt') -Pattern '^CMAKE_TOOLCHAIN_FILE:[^=]+=(.+)$').Matches.Groups[1].Value
$ndk = (Resolve-Path -LiteralPath (Join-Path (Split-Path -Parent $toolchain) '../..')).Path
foreach ($library in @('SDL3/libSDL3.so', 'libtpw_launcher.so')) {
    Invoke-Tool $strip @('--strip-debug', '-o', (Join-Path $libraries (Split-Path -Leaf $library)), (Join-Path $build $library))
}
$base = Join-Path $build 'apk-base.apk'
$resources = Join-Path $build 'apk-resources.zip'
$generated = Join-Path $build 'apk-generated'
$aapt = Join-Path $tools 'aapt2.exe'
Invoke-Tool $aapt @('compile', '--dir', (Join-Path $PSScriptRoot 'res'), '-o', $resources)
$link = @('link', '-I', $platform, '--manifest', (Join-Path $PSScriptRoot 'AndroidManifest.xml'),
    '--java', $generated, '--custom-package', 'org.tpwport.android', '-o', $base, $resources)
if ($ApplicationId -ne 'org.tpwport.android') { $link += @('--rename-manifest-package', $ApplicationId) }
if ($VersionCode -or $VersionName) { $link += '--replace-version' }
if ($VersionCode) { $link += @('--version-code', $VersionCode.ToString()) }
if ($VersionName) { $link += @('--version-name', $VersionName) }
if (!$ReleaseKeyStore) { $link += '--debug-mode' }
Invoke-Tool $aapt $link

$sources = @(Get-ChildItem -LiteralPath (Join-Path $sdl 'android-project/app/src/main/java') -Filter '*.java' -Recurse |
    ForEach-Object FullName)
$sources += @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.java' | ForEach-Object FullName)
$sources += Join-Path $generated 'org/tpwport/android/R.java'

Invoke-Tool 'javac' (@('--release', '8', '-encoding', 'UTF-8', '-classpath', $platform, '-d', $classes) + $sources)
$dex = Join-Path $build 'apk-classes.zip'
$classJar = Join-Path $build 'apk-classes.jar'
Invoke-Tool 'jar' @('cf', $classJar, '-C', $classes, '.')
Invoke-Tool (Join-Path $tools 'd8.bat') @('--lib', $platform, '--min-api', '33', '--output', $dex, $classJar)

$unsigned = Join-Path $build 'apk-unsigned.apk'
$zipScript = @'
import sys
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED, ZIP_STORED

base, dex, libraries, abi, output, source, sdl, ndk = sys.argv[1:]
source, sdl, ndk = Path(source), Path(sdl), Path(ndk)
third_party = source / 'third_party'
decomp_vendor = source / 'tpw-decomp' / 'third_party'
notices = {
    'SDL.txt': sdl / 'LICENSE.txt',
    'SDL-HIDAPI.txt': sdl / 'src/hidapi/LICENSE-bsd.txt',
    'SDL-YUV2RGB.txt': sdl / 'src/video/yuv2rgb/LICENSE',
    'zlib.txt': decomp_vendor / 'zlib/LICENSE',
    'libarchive.txt': decomp_vendor / 'libarchive/COPYING',
    'liblzma.txt': decomp_vendor / 'xz/COPYING.0BSD',
    'libjpeg.txt': third_party / 'libjpeg-6b/README',
    'stb.txt': third_party / 'stb/LICENSE',
    'Fredoka.txt': third_party / 'fonts/fredoka/OFL.txt',
    'Nunito.txt': third_party / 'fonts/nunito/OFL.txt',
    'Vulkan-Headers.md': third_party / 'vulkan/LICENSE.md',
    'Android-NDK.txt': ndk / 'NOTICE',
    'Android-NDK-toolchain.txt': ndk / 'NOTICE.toolchain',
}
with ZipFile(output, 'w') as apk, ZipFile(base) as manifest, ZipFile(dex) as bytecode:
    for entry in manifest.infolist():
        apk.writestr(entry, manifest.read(entry))
    for entry in bytecode.infolist():
        if entry.filename.endswith('.dex'):
            apk.writestr(entry, bytecode.read(entry))
    for library in [Path(libraries) / name for name in ('libSDL3.so', 'libtpw_launcher.so')]:
        apk.write(library, f'lib/{abi}/{library.name}', compress_type=ZIP_STORED)
    for name, path in notices.items():
        apk.write(path, f'assets/licenses/{name}', compress_type=ZIP_DEFLATED)
    apk.writestr('assets/licenses/NOTICE.txt',
                 'This software is based in part on the work of the Independent JPEG Group.\n')
'@
$zipScript | python - $base $dex $libraries $Abi $unsigned $source $sdl $ndk
if ($LASTEXITCODE -ne 0) { throw 'Could not assemble APK' }

$aligned = Join-Path $build 'apk-aligned.apk'
Invoke-Tool (Join-Path $tools 'zipalign.exe') @('-P', '16', '-f', '4', $unsigned, $aligned)

if ($ReleaseKeyStore) {
    if (!$ReleaseKeyAlias -or !$env:TPW_SIGNING_PASSWORD) { throw 'Set ReleaseKeyAlias and TPW_SIGNING_PASSWORD for a release build' }
    $key = (Resolve-Path -LiteralPath $ReleaseKeyStore).Path
    $signature = @('--ks', $key, '--ks-key-alias', $ReleaseKeyAlias, '--ks-pass', 'env:TPW_SIGNING_PASSWORD')
} else {
    $key = Join-Path $build 'android-debug.jks'
    if (!(Test-Path -LiteralPath $key)) {
        Invoke-Tool 'keytool' @('-genkeypair', '-keystore', $key, '-storepass', 'android', '-keypass', 'android',
            '-alias', 'androiddebugkey', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000',
            '-dname', 'CN=Android Debug,O=Android,C=US')
    }
    $signature = @('--ks', $key, '--ks-pass', 'pass:android')
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null
Invoke-Tool (Join-Path $tools 'apksigner.bat') (@('sign') + $signature + @('--out', $output, $aligned))
Invoke-Tool (Join-Path $tools 'apksigner.bat') @('verify', '--verbose', $output)
Invoke-Tool (Join-Path $tools 'zipalign.exe') @('-c', '-P', '16', '4', $output)
Get-Item -LiteralPath $output
