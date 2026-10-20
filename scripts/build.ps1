param(
    [switch]$Test,
    [switch]$Debug
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$compiler = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $compiler) {
    throw 'g++ was not found. Install a C++17 MinGW-w64 compiler and add its bin directory to PATH.'
}

Push-Location -LiteralPath $projectRoot
try {
    $cacheBuildPath = 'build'
    New-Item -ItemType Directory -Path $cacheBuildPath,'bin' -Force | Out-Null
    $flags = @(
        '-std=c++17', '-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow',
        '-finput-charset=UTF-8', '-fexec-charset=UTF-8', '-Iinclude',
        '-static-libgcc', '-static-libstdc++'
    )
    if ($Debug) {
        $flags += @('-O0', '-g', '-D_GLIBCXX_ASSERTIONS')
    } else {
        $flags += '-O2'
    }
    $coreSources = @(
        'src/models.cpp', 'src/product_table.cpp', 'src/user.cpp',
        'src/product_manager.cpp', 'src/purchase_manager.cpp', 'src/sale_manager.cpp',
        'src/inventory_manager.cpp', 'src/report_manager.cpp',
        'src/file_manager.cpp', 'src/crypto.cpp', 'src/console_ui.cpp'
    )

    Write-Host 'Building supermarket...'
    & $compiler.Source @flags @coreSources 'src/menu.cpp' 'src/main.cpp' '-o' "$cacheBuildPath/supermarket.exe" '-lbcrypt' '-lshell32'
    if ($LASTEXITCODE -ne 0) { throw 'Application build failed.' }
    Copy-Item -LiteralPath "$cacheBuildPath/supermarket.exe" -Destination 'bin/supermarket.exe'
    Write-Host 'Built: bin/supermarket.exe'

    if ($Test) {
        Write-Host 'Building tests...'
        & $compiler.Source @flags @coreSources 'src/menu.cpp' 'tests/system_tests.cpp' '-o' "$cacheBuildPath/market_tests.exe" '-lbcrypt'
        if ($LASTEXITCODE -ne 0) { throw 'Test build failed.' }
        Write-Host 'Running tests...'
        & "$cacheBuildPath/market_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    }
} finally {
    Pop-Location
}
