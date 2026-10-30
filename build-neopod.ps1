$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot.Replace('\', '/')

# Locate CMake
$cmake = "cmake"
if (Test-Path "$repoRoot/../toolchains/python/cmake/data/bin/cmake.exe") {
    $cmake = "$repoRoot/../toolchains/python/cmake/data/bin/cmake.exe"
}

# Locate Ninja
$ninjaArg = @()
if (Test-Path "$repoRoot/../toolchains/python/bin/ninja.exe") {
    $ninjaArg = @("-DCMAKE_MAKE_PROGRAM=$repoRoot/../toolchains/python/bin/ninja.exe")
}

# Locate arm-none-eabi-objcopy
$objcopy = "arm-none-eabi-objcopy"
if (Test-Path "$repoRoot/../toolchains/bin/arm-none-eabi-objcopy.exe") {
    $objcopy = "$repoRoot/../toolchains/bin/arm-none-eabi-objcopy.exe"
    $env:PATH = "$repoRoot/../toolchains/bin;" + $env:PATH
}

function Run-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE)" }
}

$build = "$repoRoot/build/neo"
$out = "$repoRoot/release"
New-Item -ItemType Directory -Path "$out/sd-card/.echo" -Force | Out-Null

Write-Host "Configuring NeoPod CMake..."
Run-Checked $cmake @('-S', "$repoRoot", '-B', $build, '-G', 'Ninja',
    $ninjaArg, "-DCMAKE_TOOLCHAIN_FILE=$repoRoot/cmake/arm-none-eabi.cmake",
    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_FLAGS_RELEASE=-O2', '-DECHO_BOARD=neo',
    '-DECHO_BUILD_SIM=OFF', '-DECHO_BUILD_TESTS=OFF', '-DECHO_BUILD_DEVICE=ON', '-DECHO_RELEASE=ON')

Write-Host "Building NeoPod targets..."
Run-Checked $cmake @('--build', $build, '--target', 'echo_device', 'echo_rescan_overlay',
    'echo_core1', 'echo_core1_mp3', '-j', '6')

Write-Host "Extracting companion binary decoders..."
Run-Checked $objcopy @('-O', 'binary', "$build/platform/device/echo_core1_mp3.elf", "$build/platform/device/core1_mp3.bin")
Run-Checked $objcopy @('-O', 'binary', "$build/platform/device/echo_core1.elf", "$build/platform/device/core1.bin")

Write-Host "Packaging NeoPod firmware image DP3_V147.IMG..."
Run-Checked 'python' @("$repoRoot/tools/build_neo_image.py", '--version', '147',
    '--elf', "$build/platform/device/echo_device.elf", '--output', "$out/DP3_V147.IMG",
    '--overlay', "$build/platform/device/echo_rescan_overlay.elf",
    '--core1-mp3', "$build/platform/device/core1_mp3.bin",
    '--core1-flac', "$build/platform/device/core1.bin", '--sd-output', "$out/sd-card/.echo")

Write-Host "[SUCCESS] NeoPod DP3_V147.IMG packaged in $out/"
