param([string]$FlasherPython = "$PSScriptRoot/.venv-flasher/Scripts/python.exe")
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
    '-DECHO_BUILD_SIM=OFF', '-DECHO_BUILD_TESTS=OFF', '-DECHO_BUILD_DEVICE=ON', '-DECHO_RELEASE=ON',
    '-DECHO_JACK_DIAGNOSTICS=OFF')

Write-Host "Building NeoPod targets..."
Run-Checked $cmake @('--build', $build, '--target', 'echo_device', 'echo_rescan_overlay',
    'echo_core1', 'echo_core1_mp3', 'echo_core1_aac', 'echo_core1_opus', '-j', '6')

Write-Host "Extracting companion binary decoders..."
Run-Checked $objcopy @('-O', 'binary', "$build/platform/device/echo_core1_mp3.elf", "$build/platform/device/core1_mp3.bin")
Run-Checked $objcopy @('-O', 'binary', "$build/platform/device/echo_core1.elf", "$build/platform/device/core1.bin")
Run-Checked $objcopy @('-O', 'binary', "$build/platform/device/echo_core1_aac.elf", "$build/platform/device/core1_aac.bin")
Run-Checked $objcopy @('-O', 'binary', "$build/platform/device/echo_core1_opus.elf", "$build/platform/device/core1_opus.bin")

Write-Host "Packaging NeoPod firmware image DP3_V182.IMG..."
Run-Checked $FlasherPython @("$repoRoot/tools/build_boot_image.py", "$repoRoot/assets/boot.png", "$out/sd-card/.echo/boot.rgb565")
Run-Checked $FlasherPython @("$repoRoot/tools/build_neo_image.py", '--version', '182',
    '--elf', "$build/platform/device/echo_device.elf", '--output', "$out/DP3_V182.IMG",
    '--overlay', "$build/platform/device/echo_rescan_overlay.elf",
    '--core1-mp3', "$build/platform/device/core1_mp3.bin",
    '--core1-flac', "$build/platform/device/core1.bin",
    '--core1-aac', "$build/platform/device/core1_aac.bin",
    '--core1-opus', "$build/platform/device/core1_opus.bin",
    '--sd-output', "$out/sd-card/.echo")

$files = @(Get-Item -LiteralPath "$out/DP3_V182.IMG") + @(Get-ChildItem -LiteralPath "$out/sd-card/.echo" -File)
$sums = foreach ($file in $files) {
    $relative = $file.FullName.Substring($out.Length + 1).Replace('\', '/')
    (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $relative
}
[IO.File]::WriteAllLines("$out/SHA256SUMS.txt", $sums, [Text.Encoding]::ASCII)

Write-Host "Building and checking both Windows flash helpers..."
Run-Checked $FlasherPython @("$repoRoot/tools/build_flash_neo.py", '--output', $out)
Run-Checked $FlasherPython @("$repoRoot/tools/package_production.py")

Write-Host "Build Complete!"
Write-Host "Firmware Image: $out/DP3_V182.IMG"
Write-Host "Companion Decoders: $out/sd-card/.echo/"
