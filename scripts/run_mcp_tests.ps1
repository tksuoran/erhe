# Launches editor.exe in the background, waits for the MCP HTTP server,
# runs mcp_server_tests, then terminates the editor.
#
# Usage:
#   pwsh scripts\run_mcp_tests.ps1 [-BuildDir <dir>] [-Config <Debug|Release>] [-Port <int>] [-EditorTimeoutSec <int>]
#
# Defaults: BuildDir=build_vs2026_opengl, Config=Debug, Port=8080, EditorTimeoutSec=60.

[CmdletBinding()]
param(
    [string]$BuildDir         = "build_vs2026_opengl",
    [string]$Config           = "Debug",
    [int]   $Port             = 8080,
    [int]   $EditorTimeoutSec = 60
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$editorExe = Join-Path $repoRoot "$BuildDir\src\editor\$Config\editor.exe"
$testExe   = Join-Path $repoRoot "$BuildDir\src\editor\mcp\test\$Config\mcp_server_tests.exe"

if (-not (Test-Path $editorExe)) {
    Write-Error "editor.exe not found at $editorExe. Build the editor target first."
}
if (-not (Test-Path $testExe)) {
    Write-Error "mcp_server_tests.exe not found at $testExe. Build with -DERHE_BUILD_TESTS=ON and the mcp_server_tests target."
}

# Refuse to launch if port is already in use - either an editor instance is
# already running (use it directly with the test exe) or another process holds
# the port.
$portInUse = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
if ($portInUse) {
    $owners = $portInUse | ForEach-Object { (Get-Process -Id $_.OwningProcess -ErrorAction SilentlyContinue).ProcessName } | Where-Object { $_ } | Select-Object -Unique
    Write-Error "Port $Port is already listening (owners: $($owners -join ', ')). Kill the existing process or run the test exe directly: '$testExe'."
}

$logDir = Join-Path $repoRoot "build_vs2026_opengl\mcp_test_logs"
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir | Out-Null }
$stdoutLog = Join-Path $logDir "editor_stdout.log"
$stderrLog = Join-Path $logDir "editor_stderr.log"

Write-Host "Launching editor: $editorExe"
$editor = Start-Process -FilePath $editorExe `
    -WorkingDirectory $repoRoot `
    -RedirectStandardOutput $stdoutLog `
    -RedirectStandardError  $stderrLog `
    -PassThru -WindowStyle Hidden

if (-not $editor) {
    Write-Error "Failed to start editor process."
}

try {
    Write-Host "Waiting up to $EditorTimeoutSec s for MCP server on port $Port ..."
    $healthUrl = "http://127.0.0.1:$Port/health"
    $deadline = (Get-Date).AddSeconds($EditorTimeoutSec)
    $ready = $false
    while ((Get-Date) -lt $deadline) {
        if ($editor.HasExited) {
            Write-Error "Editor exited prematurely (code $($editor.ExitCode)). See $stderrLog"
        }
        try {
            $resp = Invoke-WebRequest -Uri $healthUrl -UseBasicParsing -TimeoutSec 2
            if ($resp.StatusCode -eq 200) { $ready = $true; break }
        } catch {
            # not ready yet
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $ready) {
        Write-Error "MCP server did not become ready within $EditorTimeoutSec s. See $stderrLog"
    }
    Write-Host "MCP server ready. Running tests..."

    $env:ERHE_MCP_TEST_PORT = "$Port"
    & $testExe @args
    $exitCode = $LASTEXITCODE
}
finally {
    if (-not $editor.HasExited) {
        Write-Host "Stopping editor (PID $($editor.Id))..."
        Stop-Process -Id $editor.Id -Force -ErrorAction SilentlyContinue
        # Give Windows a moment to clean up sockets.
        Start-Sleep -Milliseconds 500
    }
}

Write-Host "Tests exit code: $exitCode"
exit $exitCode
