param([string]$FlasherPython = "$PSScriptRoot/../.venv-flasher/Scripts/python.exe")
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath("$PSScriptRoot/..")
$workspace = [IO.Path]::GetFullPath("$repo/..")
$cmake = "$workspace/toolchains/python/cmake/data/bin/cmake.exe"
$objcopy = "$workspace/toolchains/bin/arm-none-eabi-objcopy.exe"
$env:PATH = "$workspace/toolchains/bin;" + $env:PATH
$build = "$repo/build/neo-jack-diagnostics"
$out = "$repo/build/NeoPod-v1.8.2-jack-diagnostics"
function Run-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE)" }
}
New-Item -ItemType Directory -Path "$out/sd-card/.echo" -Force | Out-Null
Run-Checked $cmake @('-S', $repo, '-B', $build, '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$workspace/toolchains/python/bin/ninja.exe",
    "-DCMAKE_TOOLCHAIN_FILE=$repo/cmake/arm-none-eabi.cmake",
    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_FLAGS_RELEASE=-O2', '-DECHO_BOARD=neo',
    '-DECHO_BUILD_SIM=OFF', '-DECHO_BUILD_TESTS=OFF', '-DECHO_BUILD_DEVICE=ON',
    '-DECHO_RELEASE=ON', '-DECHO_JACK_DIAGNOSTICS=ON')
Run-Checked $cmake @('--build', $build, '--target', 'echo_device', 'echo_rescan_overlay',
    'echo_core1', 'echo_core1_mp3', 'echo_core1_aac', 'echo_core1_opus', '-j', '6')
foreach ($decoder in @{'echo_core1_mp3'='core1_mp3'; 'echo_core1'='core1'; 'echo_core1_aac'='core1_aac'; 'echo_core1_opus'='core1_opus'}.GetEnumerator()) {
    Run-Checked $objcopy @('-O', 'binary', "$build/platform/device/$($decoder.Key).elf", "$build/platform/device/$($decoder.Value).bin")
}
Run-Checked $FlasherPython @("$repo/tools/build_boot_image.py", "$repo/assets/boot.png", "$out/sd-card/.echo/boot.rgb565")
Run-Checked $FlasherPython @("$repo/tools/build_neo_image.py", '--version', '182',
    '--elf', "$build/platform/device/echo_device.elf", '--output', "$out/DP3_V182.IMG",
    '--overlay', "$build/platform/device/echo_rescan_overlay.elf",
    '--core1-mp3', "$build/platform/device/core1_mp3.bin", '--core1-flac', "$build/platform/device/core1.bin",
    '--core1-aac', "$build/platform/device/core1_aac.bin", '--core1-opus', "$build/platform/device/core1_opus.bin",
    '--sd-output', "$out/sd-card/.echo")
$files = @(Get-Item -LiteralPath "$out/DP3_V182.IMG") + @(Get-ChildItem -LiteralPath "$out/sd-card/.echo" -File)
$sums = foreach ($file in $files) {
    $relative = $file.FullName.Substring($out.Length + 1).Replace('\', '/')
    (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $relative
}
[IO.File]::WriteAllLines("$out/SHA256SUMS.txt", $sums, [Text.Encoding]::ASCII)
Run-Checked $FlasherPython @("$repo/tools/build_flash_neo.py", '--output', $out)
Copy-Item -LiteralPath "$repo/docs/jack-diagnostics.md" -Destination "$out/README.md"
Write-Host "Diagnostic package ready: $out"
