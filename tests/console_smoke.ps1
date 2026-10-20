param([string]$Executable = '')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Executable) {
    $Executable = Join-Path $projectRoot 'bin\supermarket.exe'
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$workspaceRoot = $projectRoot
$runRoot = Join-Path $workspaceRoot ('build\console-smoke\run_' + [guid]::NewGuid().ToString('N'))
$chineseName = -join @([char]0x4e2d, [char]0x6587)
$dataDirectory = Join-Path $runRoot ($chineseName + ' data')
$productName = -join @([char]0x77ff, [char]0x6cc9, [char]0x6c34)
$category = -join @([char]0x996e, [char]0x6599)
$reason = -join @([char]0x5b9e, [char]0x7269, [char]0x6838, [char]0x5bf9)
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

function Invoke-Console {
    param([string[]]$Lines, [string]$Options)
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $Executable
    $startInfo.Arguments = $Options
    $startInfo.WorkingDirectory = $runRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $startInfo.StandardOutputEncoding = $utf8
    $startInfo.StandardErrorEncoding = $utf8
    if ($startInfo.PSObject.Properties.Name -contains 'StandardInputEncoding') {
        $startInfo.StandardInputEncoding = $utf8
    }
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw 'Could not start the console program.' }
        $outputTask = $process.StandardOutput.ReadToEndAsync()
        $errorTask = $process.StandardError.ReadToEndAsync()
        if ($Lines.Count -gt 0) {
            $inputBytes = $utf8.GetBytes(($Lines -join "`n") + "`n")
            $process.StandardInput.BaseStream.Write($inputBytes, 0, $inputBytes.Length)
        }
        $process.StandardInput.Close()
        if (-not $process.WaitForExit(30000)) {
            $process.Kill()
            throw 'Console run timed out.'
        }
        return [PSCustomObject]@{
            ExitCode = $process.ExitCode
            Output = $outputTask.GetAwaiter().GetResult()
            Error = $errorTask.GetAwaiter().GetResult()
        }
    } finally {
        $process.Dispose()
    }
}

function Assert-PlainConsole {
    param([string]$Output, [string]$Description)
    if ($Output.Contains([string][char]27) -or $Output.Contains([string][char]12)) {
        throw ($Description + ' contains terminal control sequences in redirected output.')
    }
}

$steps = @(
    'admin', 'DemoOnly_2026!', 'DemoOnly_2026!', 'y',
    'admin', 'DemoOnly_2026!',
    '1', '3', '6900000000001', $productName, $category, '1.20', '2.00', '5', 'y', '0',
    '2', '1', 'P000001', '20', '1.20', 'y', '0',
    '6', '2', 'cashier', 'CashierOnly_2026!', 'CashierOnly_2026!', '2', 'y', '0',
    '9', 'cashier', 'CashierOnly_2026!',
    '3', '1', 'P000001', '3', '4', '10.00', 'y', '6', '', '', '0',
    '2', '9', 'admin', 'DemoOnly_2026!',
    '4', '1', 'P000001', '16', $reason, 'y', '3', '0',
    '5', '1', '', '', '3', '0',
    '7', 'y', '0'
)
$options = '--data-dir "' + $dataDirectory + '"'
$first = Invoke-Console -Lines $steps -Options $options
[IO.File]::WriteAllText((Join-Path $runRoot 'first-run.txt'), $first.Output + $first.Error, (New-Object System.Text.UTF8Encoding($false)))
if ($first.ExitCode -ne 0) { throw ('First console run failed: ' + $first.Error) }
Assert-PlainConsole -Output $first.Output -Description 'Full console workflow'
$storePath = Join-Path $dataDirectory 'store.db'
$store = [IO.File]::ReadAllText($storePath, [Text.Encoding]::UTF8)
if (-not $store.Contains($productName)) { throw 'Chinese product name did not round-trip.' }
if ($store -notmatch 'PRODUCT "P000001" [^\r\n]+ 120 200 16 5 1') { throw 'Final stock is not 16.' }
if ($store -notmatch 'SALE "S000002" [^\r\n]+ "cashier" 600 1000 400 1') { throw 'Sale totals or cashier are wrong.' }
if ($store -notmatch 'STOCKTAKE "T000003" [^\r\n]+ 17 16 ') { throw 'Stocktake history is wrong.' }
if (-not (Get-ChildItem -LiteralPath (Join-Path $dataDirectory 'backups') -Filter '*.db')) { throw 'Backup was not created.' }
Write-Host 'PASS: full console workflow, Chinese data/path, cashier, stocktake and backup'

$second = Invoke-Console -Lines @('admin', 'DemoOnly_2026!', '5', '1', '', '', '3', '0', '0') -Options $options
[IO.File]::WriteAllText((Join-Path $runRoot 'restart.txt'), $second.Output + $second.Error, (New-Object System.Text.UTF8Encoding($false)))
if ($second.ExitCode -ne 0 -or -not $second.Output.Contains('6.00') -or -not $second.Output.Contains('16')) {
    throw 'Restarted program did not load the saved reports.'
}
Write-Host 'PASS: process restart and report retrieval'

$dashboard = Invoke-Console -Lines @('admin', 'DemoOnly_2026!', '0') -Options $options
[IO.File]::WriteAllText((Join-Path $runRoot 'dashboard.txt'), $dashboard.Output + $dashboard.Error, (New-Object System.Text.UTF8Encoding($false)))
if ($dashboard.ExitCode -ne 0 -or -not $dashboard.Output.Contains('SUPERMARKET')) {
    throw 'Dashboard did not display the application brand.'
}
Assert-PlainConsole -Output $dashboard.Output -Description 'Dashboard'
$products = Invoke-Console -Lines @('admin', 'DemoOnly_2026!', '1', '1', '', '0', '0') -Options $options
[IO.File]::WriteAllText((Join-Path $runRoot 'products.txt'), $products.Output + $products.Error, (New-Object System.Text.UTF8Encoding($false)))
if ($products.ExitCode -ne 0 -or -not $products.Output.Contains($productName) -or -not $products.Output.Contains('P000001')) {
    throw 'Product list did not display the saved Chinese product.'
}
Assert-PlainConsole -Output $products.Output -Description 'Product list'
$receipts = Invoke-Console -Lines @('admin', 'DemoOnly_2026!', '3', '6', '', '', '0', '0') -Options $options
[IO.File]::WriteAllText((Join-Path $runRoot 'receipts.txt'), $receipts.Output + $receipts.Error, (New-Object System.Text.UTF8Encoding($false)))
if ($receipts.ExitCode -ne 0 -or -not $receipts.Output.Contains('S000002') -or -not $receipts.Output.Contains('6.00')) {
    throw 'Receipt query did not display the saved sale.'
}
Assert-PlainConsole -Output $receipts.Output -Description 'Receipt query'
Write-Host 'PASS: dashboard, product table and receipt previews use plain redirected text'

$help = Invoke-Console -Lines @() -Options '--help'
if ($help.ExitCode -ne 0 -or -not $help.Output.Contains('--data-dir')) { throw 'Help failed.' }
$unknown = Invoke-Console -Lines @() -Options '--unknown'
if ($unknown.ExitCode -eq 0) { throw 'Unknown argument was accepted.' }
$missing = Invoke-Console -Lines @() -Options '--data-dir'
if ($missing.ExitCode -eq 0) { throw 'Missing data directory argument was accepted.' }
Write-Host 'PASS: help and invalid arguments'

$holderInfo = New-Object System.Diagnostics.ProcessStartInfo
$holderInfo.FileName = $Executable
$holderInfo.Arguments = $options
$holderInfo.WorkingDirectory = $runRoot
$holderInfo.UseShellExecute = $false
$holderInfo.CreateNoWindow = $true
$holderInfo.RedirectStandardInput = $true
$holderInfo.RedirectStandardOutput = $true
$holderInfo.RedirectStandardError = $true
$holderInfo.StandardOutputEncoding = New-Object System.Text.UTF8Encoding($false)
$holder = New-Object System.Diagnostics.Process
$holder.StartInfo = $holderInfo
try {
    if (-not $holder.Start()) { throw 'Could not start lock holder.' }
    $ready = $false
    for ($lineIndex = 0; $lineIndex -lt 20; ++$lineIndex) {
        $lineTask = $holder.StandardOutput.ReadLineAsync()
        if (-not $lineTask.Wait(10000)) { throw 'Lock holder startup timed out.' }
        $line = $lineTask.GetAwaiter().GetResult()
        if ($null -eq $line) { throw 'Lock holder exited during startup.' }
        if ($line.Contains('SUPERMARKET')) { $ready = $true; break }
    }
    if (-not $ready) { throw 'Lock holder did not enter the menu.' }
    $contender = Invoke-Console -Lines @() -Options $options
    if ($contender.ExitCode -eq 0 -or -not $contender.Error) { throw 'Second process was not rejected.' }
    $holder.StandardInput.Close()
    if (-not $holder.WaitForExit(10000)) { throw 'Lock holder EOF shutdown timed out.' }
    if ($holder.ExitCode -ne 0) { throw 'Lock holder failed to exit.' }
} finally {
    if (-not $holder.HasExited) { $holder.Kill() }
    $holder.Dispose()
}
Write-Host 'PASS: two separate processes cannot use the same data directory'

$isolatedBin = Join-Path $runRoot 'isolated-bin'
New-Item -ItemType Directory -Path $isolatedBin -Force | Out-Null
$originalExecutable = $Executable
try {
    $Executable = Join-Path $isolatedBin 'supermarket.exe'
    Copy-Item -LiteralPath $originalExecutable -Destination $Executable
    $defaultRun = Invoke-Console -Lines @('0') -Options ''
    if ($defaultRun.ExitCode -ne 0) { throw 'Isolated executable failed to start.' }
    if (-not (Test-Path -LiteralPath (Join-Path $isolatedBin 'data\.market.lock'))) {
        throw 'Default data directory is not next to the executable.'
    }
    if (Test-Path -LiteralPath (Join-Path $runRoot 'data')) {
        throw 'Default data directory incorrectly uses the working directory.'
    }
} finally {
    $Executable = $originalExecutable
}
Write-Host 'PASS: executable runs alone and default data directory ignores working directory'
Write-Host ('Console smoke tests passed. Artifacts: ' + $runRoot)
