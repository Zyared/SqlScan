[CmdletBinding()]
param(
    [string]$PostgresBin,
    [string]$DataRoot = (Join-Path $env:LOCALAPPDATA 'SqlScan2\Local'),
    [switch]$Stop,
    [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'
# A launch from a PowerShell 7 host can inherit that host's module directories.
# Prefer Windows PowerShell's own security and storage modules in this process.
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + ';' + $env:PSModulePath
$root = [IO.Path]::GetFullPath($DataRoot)
$cluster = Join-Path $root 'cluster'
$appDir = $PSScriptRoot
$driverMsi = Join-Path $appDir 'dependencies\psqlodbc_x64.msi'
$null = [IO.Directory]::CreateDirectory($root)
$settingsPath = Join-Path $root 'instance.xml'

if (!$PostgresBin) {
    $command = Get-Command pg_ctl.exe -ErrorAction SilentlyContinue
    $candidates = @(Join-Path $appDir 'runtime\pgsql\bin')
    if ($command) { $candidates += Split-Path $command.Source }
    $candidates += @(Get-ChildItem "$env:ProgramFiles\PostgreSQL\*\bin" -Directory -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | ForEach-Object FullName)
    $versionFile = Join-Path $cluster 'PG_VERSION'
    $major = if (Test-Path $versionFile) { (Get-Content $versionFile -Raw).Trim() }
    foreach ($candidate in $candidates) {
        $probe = Join-Path $candidate 'pg_ctl.exe'
        if (!(Test-Path $probe)) { continue }
        if (!$major -or ((& $probe --version) -match ("\b" + [regex]::Escape($major) + "\."))) {
            $PostgresBin = $candidate
            break
        }
    }
}
if (!$PostgresBin) { throw 'Install PostgreSQL with command-line tools or pass -PostgresBin.' }
$ctl = Join-Path $PostgresBin 'pg_ctl.exe'
$psql = Join-Path $PostgresBin 'psql.exe'
function RandomSecret {
    $bytes = New-Object byte[] 32
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
    [BitConverter]::ToString($bytes).Replace('-','')
}
function RunSql([string]$database, [string]$statement) {
    $result = $statement | & $psql -X -w -h 127.0.0.1 -p $settings.Port -U sqlscan_owner -d $database -At -v ON_ERROR_STOP=1
    if ($LASTEXITCODE) { throw "PostgreSQL setup failed for $database." }
    return $result
}
$mutex = [Threading.Mutex]::new($false,'Local\SqlScan.Local.Setup')
$locked = $false
try {
    try { $locked = $mutex.WaitOne(90000) } catch [Threading.AbandonedMutexException] { $locked = $true }
    if (!$locked) { throw 'Another SqlScan instance is still being configured.' }
    if ($Stop) {
        if (Test-Path (Join-Path $cluster 'postmaster.pid')) {
            & $ctl stop -w -m fast -D $cluster
            if ($LASTEXITCODE) { throw 'Could not stop the local database.' }
        }
        return
    }
    if (!(Test-Path $settingsPath)) {
        if (Test-Path (Join-Path $cluster 'PG_VERSION')) { throw 'Existing cluster has no matching SqlScan configuration.' }
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
        $listener.Start()
        try { $port = $listener.LocalEndpoint.Port } finally { $listener.Stop() }
        $settings = [pscustomobject]@{
            Port = $port
            Owner = [PSCredential]::new('sqlscan_owner',(ConvertTo-SecureString (RandomSecret) -AsPlainText -Force))
            App = [PSCredential]::new('sqlscan_app',(ConvertTo-SecureString (RandomSecret) -AsPlainText -Force))
        }
        $settings | Export-Clixml -LiteralPath $settingsPath
    } else { $settings = Import-Clixml -LiteralPath $settingsPath }
    if (!(Test-Path (Join-Path $cluster 'PG_VERSION'))) {
        $passfile = Join-Path $root ([Guid]::NewGuid().ToString('N') + '.tmp')
        try {
            [IO.File]::WriteAllText($passfile,$settings.Owner.GetNetworkCredential().Password)
            & (Join-Path $PostgresBin 'initdb.exe') -D $cluster --username=sqlscan_owner --auth=scram-sha-256 --encoding=UTF8 --locale=C "--pwfile=$passfile"
            if ($LASTEXITCODE) { throw 'Could not initialize the local PostgreSQL cluster.' }
        } finally { if(Test-Path $passfile){ Remove-Item -LiteralPath $passfile } }
    }
    & $ctl status -D $cluster *> $null
    if ($LASTEXITCODE -ne 0) {
        & $ctl start -w -D $cluster -l (Join-Path $root 'postgres.log') -o "-h 127.0.0.1 -p $($settings.Port) -c timezone=Europe/Moscow"
        if ($LASTEXITCODE) { throw 'Could not start PostgreSQL. Check postgres.log in the local data folder.' }
    }
    $oldPassword = $env:PGPASSWORD
    try {
        $env:PGPASSWORD = $settings.Owner.GetNetworkCredential().Password
        if (!(RunSql 'postgres' "SELECT 1 FROM pg_roles WHERE rolname='sqlscan_app'")) {
            $password = $settings.App.GetNetworkCredential().Password
            $null = RunSql 'postgres' "CREATE ROLE sqlscan_app LOGIN PASSWORD '$password'"
        }
        if (!(RunSql 'postgres' "SELECT 1 FROM pg_database WHERE datname='sqlscan'")) {
            $null = RunSql 'postgres' 'CREATE DATABASE sqlscan'
        }
        $null = RunSql 'sqlscan' 'REVOKE CREATE ON SCHEMA public FROM PUBLIC; CREATE SCHEMA IF NOT EXISTS sqlscan AUTHORIZATION sqlscan_app;'
    } finally { $env:PGPASSWORD = $oldPassword }
    $driver = 'PostgreSQL Unicode(x64)'
    $selector = "Driver={$driver}"
    if (!(Get-OdbcDriver -Platform '64-bit' -Name $driver -ErrorAction SilentlyContinue)) {
        $driverRoot = Join-Path $root 'driver'
        $localDriver = Get-ChildItem -LiteralPath $driverRoot -Filter 'podbc35w.dll' -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if (!$localDriver) {
            if (!(Test-Path $driverMsi)) { throw 'Install psqlODBC Unicode x64 or place psqlodbc_x64.msi in dependencies.' }
            $argsMsi = '/a "' + $driverMsi + '" /qn TARGETDIR="' + $driverRoot + '"'
            $install = Start-Process msiexec.exe -ArgumentList $argsMsi -WindowStyle Hidden -PassThru -Wait
            if ($install.ExitCode) { throw "ODBC extraction failed ($($install.ExitCode))." }
            $localDriver = Get-ChildItem -LiteralPath $driverRoot -Filter 'podbc35w.dll' -File -Recurse | Select-Object -First 1
        }
        if (!$localDriver) { throw 'ODBC driver DLL missing.' }
        $env:PATH = $localDriver.DirectoryName + ';' + $env:PATH
        # The Windows driver manager resolves an absolute DLL path through a user DSN.
        $dsnName = "SqlScan2-Local-$($settings.Port)"
        $dsnKey = "HKCU:\Software\ODBC\ODBC.INI\$dsnName"
        if (Test-Path -LiteralPath $dsnKey) {
            if ((Get-ItemPropertyValue -LiteralPath $dsnKey -Name Driver) -ne $localDriver.FullName) {
                throw 'The local DSN name is already in use.'
            }
        } else {
            $null = New-Item -Path $dsnKey -Force
            Set-ItemProperty -LiteralPath $dsnKey -Name Driver -Value $localDriver.FullName
        }
        $selector = "DSN=$dsnName"
    }
    if (!$PrepareOnly) {
        $env:SQLSCAN_CONNECTION = "$selector;Server=127.0.0.1;Port=$($settings.Port);Database=sqlscan;Uid=sqlscan_app;Pwd=$($settings.App.GetNetworkCredential().Password);"
        try { Start-Process -FilePath (Join-Path $appDir 'SqlScan.exe') -WorkingDirectory $appDir }
        finally { Remove-Item Env:SQLSCAN_CONNECTION -ErrorAction SilentlyContinue }
    }
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
