param(
    [string]$SourceDir = "",
    [string]$BuildDir = "",
    [string]$InstallDir = "",
    [string]$ArmClangBin = "",
    [string]$Generator = "Ninja",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Join-Path $Root "rlottie-0.2"
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $Root "build"
}
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $Root "install"
}
if ([string]::IsNullOrWhiteSpace($ArmClangBin)) {
    $ArmClangBin = "D:\App\Keil\Keil_v5\ARM\ARMCLANG\bin"
}

$SourceDir = [System.IO.Path]::GetFullPath($SourceDir)
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$InstallDir = [System.IO.Path]::GetFullPath($InstallDir)
$Toolchain = Join-Path $Root "armclang-cortex-m7.cmake"

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE"
    }
}

function Replace-TextOnce {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Old,
        [Parameter(Mandatory = $true)]
        [string]$New
    )

    $text = Get-Content -LiteralPath $Path -Raw
    if ($text.Contains($Old)) {
        Set-Content -LiteralPath $Path -Value $text.Replace($Old, $New) -NoNewline
    }
}

function Apply-RlottieAc6Patch {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source
    )

    $rootCMake = Join-Path $Source "CMakeLists.txt"
    $rootText = Get-Content -LiteralPath $rootCMake -Raw
    if (!$rootText.Contains('SUFFIX ".lib"')) {
        $rootText = $rootText.Replace(
            "set_target_properties( rlottie PROPERTIES DEFINE_SYMBOL RLOTTIE_BUILD )",
            "set_target_properties( rlottie PROPERTIES DEFINE_SYMBOL RLOTTIE_BUILD )`nset_target_properties( rlottie PROPERTIES PREFIX `"`" SUFFIX `".lib`" )")
    }
    if (!$rootText.Contains("option(LOTTIE_EXAMPLE")) {
        $rootText = $rootText.Replace(
            "option(LOTTIE_TEST `"Build LOTTIE AUTOTESTS`" OFF)`noption(LOTTIE_CCACHE",
            "option(LOTTIE_TEST `"Build LOTTIE AUTOTESTS`" OFF)`noption(LOTTIE_EXAMPLE `"Build LOTTIE EXAMPLES`" ON)`noption(LOTTIE_CCACHE")
    }
    if (!$rootText.Contains("if (LOTTIE_EXAMPLE)")) {
        $rootText = [regex]::Replace(
            $rootText,
            "(?m)^\s*add_subdirectory\(example\)\s*$",
            "if (LOTTIE_EXAMPLE)`n    add_subdirectory(example)`nendif()")
    }
    Set-Content -LiteralPath $rootCMake -Value $rootText -NoNewline

    $vectorCMake = Join-Path $Source "src\vector\CMakeLists.txt"
    $vectorText = Get-Content -LiteralPath $vectorCMake -Raw
    if (!$vectorText.Contains("AC6_RLOTTIE_NO_THREAD_LIBCXX")) {
        $vectorText = [regex]::Replace(
            $vectorText,
            "(?m)^\s*`"`\$\{CMAKE_CURRENT_LIST_DIR\}/vdebug\.cpp`"\s*\r?\n",
            "")
        $vectorText = $vectorText.Replace(
            "target_include_directories(rlottie",
            "if (NOT AC6_RLOTTIE_NO_THREAD_LIBCXX)`n    target_sources(rlottie`n        PRIVATE`n            `"`${CMAKE_CURRENT_LIST_DIR}/vdebug.cpp`"`n        )`nendif()`n`ntarget_include_directories(rlottie")
    }
    Set-Content -LiteralPath $vectorCMake -Value $vectorText -NoNewline
}

if (!(Test-Path (Join-Path $SourceDir "CMakeLists.txt"))) {
    throw "rlottie source not found: $SourceDir. Put rlottie-0.2 under External\keil_ac6_rlottie\rlottie-0.2 or pass -SourceDir."
}

Apply-RlottieAc6Patch -Source $SourceDir

if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

$cmakeArgs = @(
    "-S", $SourceDir,
    "-B", $BuildDir,
    "-G", $Generator,
    "-DCMAKE_TOOLCHAIN_FILE=$Toolchain",
    "-DARMCLANG_BIN=$ArmClangBin",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_INSTALL_PREFIX=$InstallDir",
    "-DLIB_INSTALL_DIR=lib",
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DLOTTIE_MODULE=OFF",
    "-DLOTTIE_THREAD=OFF",
    "-DLOTTIE_CACHE=OFF",
    "-DLOTTIE_TEST=OFF",
    "-DLOTTIE_EXAMPLE=OFF"
)

Write-Host "[rlottie] source : $SourceDir"
Write-Host "[rlottie] build  : $BuildDir"
Write-Host "[rlottie] install: $InstallDir"

Invoke-Checked cmake @cmakeArgs
Invoke-Checked cmake --build $BuildDir --config Release
Invoke-Checked cmake --install $BuildDir --config Release

$lib = Join-Path $InstallDir "lib\rlottie.lib"
if (!(Test-Path $lib)) {
    throw "Build finished but rlottie.lib was not found at $lib"
}

Write-Host "[rlottie] done: $lib"
