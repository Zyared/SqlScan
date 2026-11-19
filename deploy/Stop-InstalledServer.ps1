# Only the private server running from this installation needs to release its files.
$ErrorActionPreference = 'Stop'
$cluster = Join-Path $env:LOCALAPPDATA 'SqlScan2\Local\cluster'
$pidFile = Join-Path $cluster 'postmaster.pid'
if (!(Test-Path -LiteralPath $pidFile)) { return }
$serverId = 0
if (![int]::TryParse((Get-Content -LiteralPath $pidFile -TotalCount 1),[ref]$serverId)) { return }
$server = Get-Process -Id $serverId -ErrorAction SilentlyContinue
$bin = Join-Path $PSScriptRoot 'runtime\pgsql\bin'
if ($server -and $server.Path -eq (Join-Path $bin 'postgres.exe')) {
    & (Join-Path $bin 'pg_ctl.exe') stop -w -m fast -D $cluster
    if ($LASTEXITCODE) { throw 'Could not stop the SqlScan database for maintenance.' }
}
