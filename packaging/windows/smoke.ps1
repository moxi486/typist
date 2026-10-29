param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [string]$LogDirectory = (Join-Path $env:TEMP 'typist-smoke')
)

$ErrorActionPreference = 'Stop'
$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
$stdout = Join-Path $LogDirectory 'stdout.log'
$stderr = Join-Path $LogDirectory 'stderr.log'
# PowerShell can return before a Windows GUI executable exits when invoked with &.
# Wait for the actual process and inspect its exit code instead of $LASTEXITCODE.
$application = Start-Process -FilePath $resolvedExecutable -ArgumentList '--bench-repaint' `
    -WorkingDirectory (Split-Path -Parent $resolvedExecutable) -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
if (-not $application.WaitForExit(30000)) {
    $application.Kill()
    throw 'The deployed application timed out'
}
$application.Refresh()
$output = Get-Content -LiteralPath $stdout -Raw
$errors = Get-Content -LiteralPath $stderr -Raw
Write-Output $output
if ($errors) { Write-Output $errors }
if ($application.ExitCode -ne 0) {
    throw "The deployed application exited with code $($application.ExitCode)"
}
if ($output -notmatch 'doc_blocks=\d+' -or $output -notmatch 'repaint_ms=\d+') {
    throw 'The deployed application did not finish the startup and repaint checks'
}
