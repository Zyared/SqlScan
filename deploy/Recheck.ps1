[CmdletBinding()]
param([switch]$Quality, [string]$Cache, [string]$ResultDirectory, [switch]$Offline)
$ErrorActionPreference = 'Stop'
if ($Quality -and $Offline -and !$Cache) { throw 'Offline quality checks require -Cache with previously verified data.' }
$kit = $PSScriptRoot
$python = Join-Path $kit 'validation\python\python.exe'
if (!$ResultDirectory) {
    $ResultDirectory = Join-Path $env:LOCALAPPDATA ('SqlScan2\Checks\' + (Get-Date -Format yyyyMMdd-HHmmss) + '-' + [Guid]::NewGuid().ToString('N').Substring(0,6))
}
$result = [IO.Directory]::CreateDirectory($ResultDirectory).FullName
$data = Join-Path ([IO.Path]::GetTempPath()) ('sqlscan-qa-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($data) | Out-Null
$log = [IO.StreamWriter]::new((Join-Path $result 'checks.txt'), $false, [Text.UTF8Encoding]::new($true))
$log.AutoFlush = $true
function Run([string]$path, [string[]]$arguments) {
    $null = Get-Item -LiteralPath $path -ErrorAction Stop
    $log.WriteLine("RUN " + [IO.Path]::GetFileName($path))
    # Windows PowerShell 5 may present a native stderr line as a non-terminating error.
    # Keep that diagnostic and use the process exit code to decide the result.
    $ErrorActionPreference = 'Continue'
    & $path @arguments 2>&1 | ForEach-Object {
        $line = [string]$_
        $log.WriteLine($line)
        Write-Output $line
    }
    if ($LASTEXITCODE) { throw "Check failed: $([IO.Path]::GetFileName($path)) ($LASTEXITCODE)" }
}
$bin = Join-Path $kit 'runtime\pgsql\bin'
$dsnKey = $null
$prior = @{}
foreach ($keyName in 'PSModulePath','PATH','PGCLIENTENCODING','PGPASSWORD','PYTHONUTF8','PYTHONIOENCODING','SQLSCAN_CONNECTION','SQLSCAN_TEST_CONNECTION') {
    $prior[$keyName] = [Environment]::GetEnvironmentVariable($keyName, 'Process')
}
try {
    $env:PSModulePath = (Join-Path $PSHOME 'Modules') + ';' + $env:PSModulePath
    $env:PGCLIENTENCODING = 'UTF8'
    $env:PYTHONUTF8 = '1'
    $env:PYTHONIOENCODING = 'utf-8'
    Run $python @('-B', (Join-Path $kit 'source\tests\check_manifest.py'), $kit)
    & (Join-Path $kit 'Start-Local.ps1') -DataRoot $data -PrepareOnly
    $config = Import-Clixml -LiteralPath (Join-Path $data 'instance.xml')
    $port = $config.Port
    $dsn = "SqlScan2-Local-$port"
    $selector = 'Driver={PostgreSQL Unicode(x64)}'
    $key = "HKCU:\Software\ODBC\ODBC.INI\$dsn"
    if (Test-Path -LiteralPath $key) {
        $dll = Get-ItemPropertyValue -LiteralPath $key -Name Driver
        if ($dll.StartsWith($data+'\',[StringComparison]::OrdinalIgnoreCase)) {
            $dsnKey = $key
            $env:PATH = (Split-Path $dll -Parent) + ';' + $env:PATH
            $selector = "DSN=$dsn"
        }
    }
    $env:PGPASSWORD = $config.App.GetNetworkCredential().Password
    $env:SQLSCAN_TEST_CONNECTION = "$selector;Server=127.0.0.1;Port=$port;Database=sqlscan;Uid=sqlscan_app;Pwd=$env:PGPASSWORD;"
    $env:SQLSCAN_CONNECTION = $env:SQLSCAN_TEST_CONNECTION
    $pg = @('-X','-w','-v','ON_ERROR_STOP=1','-h','127.0.0.1','-p',[string]$port,'-U','sqlscan_app','-d','sqlscan')
    $psql = Join-Path $bin 'psql.exe'
    Run $psql ($pg + @('-f',(Join-Path $kit 'source\db\test_schema.sql')))
    Run $psql ($pg + @('-f',(Join-Path $kit 'source\tests\seed_journal.sql')))
    Run (Join-Path $kit 'validation\Acceptance.exe') @()
    if ($env:SQLSCAN_VSTEST) {
        Run $env:SQLSCAN_VSTEST @((Join-Path $kit 'validation\SqlScanTests.dll'),'/Platform:x64',('/ResultsDirectory:' + $result))
    }
    Run $python @('-B',(Join-Path $kit 'source\tests\batch_smoke.py'),(Join-Path $kit 'SqlScan.exe'))
    Run $python @('-B',(Join-Path $kit 'source\tests\score_audit.py'),$kit,(Join-Path $result 'scores.json'))
    Run $python @('-B',(Join-Path $kit 'source\tests\gui_smoke.py'),(Join-Path $kit 'SqlScan.exe'))
    Run $python @('-B',(Join-Path $kit 'source\tests\release_checks.py'),$kit,$data,[string]$port)
    $ordinary = Join-Path $result 'ordinary-sql.json'
    Run $python @('-B',(Join-Path $kit 'source\tests\benign_subqueries.py'),$kit,$ordinary)
    if ((Get-FileHash $ordinary).Hash -ne (Get-FileHash (Join-Path $kit 'source\benchmark\results\ordinary-sql-2.0.3.json')).Hash) { throw 'Ordinary SQL results differ' }
    if ($Quality) {
        $output = Join-Path $result 'quality.json'
        $opts = @('-B',(Join-Path $kit 'source\benchmark\verify_quality.py'),'--kit',$kit,'--out',$output)
        if (!$Cache) { $Cache = Join-Path $data 'quality-cache' }
        $opts += @('--cache',$Cache)
        if ($Offline) { $opts += '--offline' }
        Run $python $opts
        $paired = Join-Path $result 'paired.json'
        $compare = @('-B',(Join-Path $kit 'source\benchmark\compare_releases.py'),'--kit',$kit,'--cache',$Cache,'--out',$paired)
        if ($Offline) { $compare += '--offline' }
        Run $python $compare
        $compareResult = Join-Path $kit 'source\tests\check_benchmark.py'
        Run $python @('-B',$compareResult,$kit,(Join-Path $kit 'source\benchmark\results\paired-2.0.3.json'),$paired)
        Run $python @('-B',$compareResult,$kit,(Join-Path $kit 'source\benchmark\results\release-quality.json'),$output)
        Run $python @('-B',(Join-Path $kit 'source\benchmark\verify_refactor.py'),
            '--previous',(Join-Path $kit 'validation\SqlScan-2.0.3.exe'),'--kit',$kit,
            '--cache',$Cache,'--out',(Join-Path $result 'refactor.json'))
    }
} catch {
    $log.WriteLine("FAIL " + $_.Exception.Message)
    throw
} finally {
    try {
        try { & (Join-Path $kit 'Start-Local.ps1') -DataRoot $data -Stop }
        finally {
            if ($dsnKey -and (Test-Path -LiteralPath $dsnKey)) { Remove-Item -LiteralPath $dsnKey }
            if ((Test-Path -LiteralPath $data) -and !(Test-Path -LiteralPath (Join-Path $data 'cluster\postmaster.pid'))) {
                $verified = Get-Item -LiteralPath $data
                if ($verified.Parent.FullName.TrimEnd('\') -eq [IO.Path]::GetTempPath().TrimEnd('\') -and $verified.Name -match '^sqlscan-qa-[0-9a-f]{32}$' -and !($verified.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                    Remove-Item -LiteralPath $verified.FullName -Recurse -Force
                }
            }
            Write-Output "Report: $result"
        }
    } finally {
        foreach ($keyName in $prior.Keys) {
            [Environment]::SetEnvironmentVariable($keyName, $prior[$keyName], 'Process')
        }
        $log.Dispose()
    }
}
[IO.File]::AppendAllText((Join-Path $result 'checks.txt'), "PASS" + [Environment]::NewLine)
Write-Output 'PASS'
