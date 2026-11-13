[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [ValidatePattern('^COM[0-9]+$')]
  [string]$Port,
  [int]$Baud = 9600,
  [ValidateSet("Probe", "Line", "DirectionMatrix")]
  [string]$Action = "Probe",
  [ValidateSet("All", "L", "R")]
  [string]$Wheel = "All",
  [ValidateSet("All", "Plus", "Minus")]
  [string]$Direction = "All",
  [switch]$ExecuteMotion
)

$ErrorActionPreference = "Stop"

if (($Action -eq "DirectionMatrix") -and (-not $ExecuteMotion)) {
  throw "DirectionMatrix executes real motor output and requires -ExecuteMotion."
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$logRoot = Join-Path $repoRoot "build/bench-logs"
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$logPath = Join-Path $logRoot ("hc05-" + $Action + "-" + $timestamp + ".log")
$utf8 = [Text.UTF8Encoding]::new($false)
$logWriter = [IO.StreamWriter]::new($logPath, $false, $utf8)
$serial = $null
$motionAttempted = $false
$safeStopSent = $false

function Write-Log {
  param([string]$Message)

  $line = "{0:yyyy-MM-dd HH:mm:ss.fff} {1}" -f (Get-Date), $Message
  Write-Host $line
  $logWriter.WriteLine($line)
  $logWriter.Flush()
}

function Send-Command {
  param(
    [Parameter(Mandatory = $true)][string]$Command,
    [Parameter(Mandatory = $true)][string]$Pattern,
    [int]$TimeoutMs = 2500
  )

  Write-Log ("TX: " + $Command)
  $serial.WriteLine($Command)
  $stopwatch = [Diagnostics.Stopwatch]::StartNew()
  while ($stopwatch.ElapsedMilliseconds -lt $TimeoutMs) {
    try {
      $line = $serial.ReadLine()
    }
    catch [TimeoutException] {
      continue
    }
    if ([string]::IsNullOrWhiteSpace($line)) {
      continue
    }
    Write-Log ("RX: " + $line)
    if ($line -match $Pattern) {
      return $line
    }
  }
  throw ("timeout waiting for /{0}/ after {1}" -f $Pattern, $Command)
}

function Wait-ForTerminal {
  param([int]$TimeoutMs = 1800)

  $stopwatch = [Diagnostics.Stopwatch]::StartNew()
  while ($stopwatch.ElapsedMilliseconds -lt $TimeoutMs) {
    try {
      $line = $serial.ReadLine()
    }
    catch [TimeoutException] {
      continue
    }
    if ([string]::IsNullOrWhiteSpace($line)) {
      continue
    }
    Write-Log ("RX: " + $line)
    if (($line -match "^OK STOP AUTO") -or
        ($line -match "^ERR ") -or
        ($line -match "^OK STOP")) {
      return $line
    }
  }
  throw "timeout waiting for DONE/FAULT/STOP"
}

function Send-SafeStopOnce {
  if (($script:safeStopSent) -or ($null -eq $serial) -or
      (-not $serial.IsOpen)) {
    return
  }
  $script:safeStopSent = $true
  try {
    Write-Log "TX: STOP (failure cleanup)"
    $serial.WriteLine("STOP")
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    while ($stopwatch.ElapsedMilliseconds -lt 1000) {
      try {
        $line = $serial.ReadLine()
      }
      catch [TimeoutException] {
        continue
      }
      if ([string]::IsNullOrWhiteSpace($line)) {
        continue
      }
      Write-Log ("RX: " + $line)
      if (($line -match "^OK STOP") -or ($line -match "^ERR ")) {
        break
      }
    }
  }
  catch {
    Write-Log ("STOP cleanup failed: " + $_.Exception.Message)
  }
}

function Get-StatusNumber {
  param(
    [Parameter(Mandatory = $true)][string]$StatusLine,
    [Parameter(Mandatory = $true)][string]$Field
  )

  $pattern = "(?:^|\s)" + [regex]::Escape($Field) + "=(-?[0-9]+)"
  $match = [regex]::Match($StatusLine, $pattern)
  if (-not $match.Success) {
    throw ("STATUS missing field {0}: {1}" -f $Field, $StatusLine)
  }
  return [long]$match.Groups[1].Value
}

function Assert-ResetStatus {
  param([Parameter(Mandatory = $true)][string]$StatusLine)

  $leftTotal = Get-StatusNumber -StatusLine $StatusLine -Field "Ltotal"
  $rightTotal = Get-StatusNumber -StatusLine $StatusLine -Field "Rtotal"
  $leftValid = Get-StatusNumber -StatusLine $StatusLine -Field "Lvalid"
  $rightValid = Get-StatusNumber -StatusLine $StatusLine -Field "Rvalid"
  if (($leftTotal -ne 0) -or ($rightTotal -ne 0) -or
      ($leftValid -ne 1) -or ($rightValid -ne 1)) {
    throw ("RESET verification failed: " + $StatusLine)
  }
}

try {
  Write-Log ("Opening {0} at {1} 8N1" -f $Port, $Baud)
  $serial = [IO.Ports.SerialPort]::new($Port, $Baud, "None", 8, "One")
  $serial.NewLine = "`r`n"
  $serial.Encoding = [Text.Encoding]::ASCII
  $serial.ReadTimeout = 250
  $serial.WriteTimeout = 1000
  $serial.DtrEnable = $false
  $serial.RtsEnable = $false
  $serial.Open()
  $serial.DiscardInBuffer()
  $serial.DiscardOutBuffer()

  if ($Action -eq "Probe") {
    $null = Send-Command -Command "PING" -Pattern "^OK PONG$"
    $status = Send-Command -Command "STATUS" -Pattern "^STATUS "
    if (($status -notmatch "mode=STANDBY") -or
        ($status -notmatch " stby=0 ") -or
        ($status -notmatch " L=0 ") -or
        ($status -notmatch " R=0 ")) {
      throw ("initial state is not safe: " + $status)
    }
    $null = Send-Command -Command "STOP" -Pattern "^OK STOP$"
    Write-Log "Probe completed"
  }
  elseif ($Action -eq "Line") {
    $line = Send-Command -Command "LINE" -Pattern "^LINE "
    $linePattern =
      "^LINE raw=([01]{4}) raw_hex=0x([0-9A-F]{2}) " +
      "black_mask=0x([0-9A-F]{2}) state=SC_LINE_[A-Z0-9_]+ " +
      "valid=1 t=([0-9]+) seq=([0-9]+) age=([0-9]+)$"
    $lineMatch = [regex]::Match($line, $linePattern)
    if (-not $lineMatch.Success) {
      throw ("LINE response malformed: " + $line)
    }
    $rawValue = [Convert]::ToInt32($lineMatch.Groups[1].Value, 2)
    $rawHex = [Convert]::ToInt32($lineMatch.Groups[2].Value, 16)
    $blackMask = [Convert]::ToInt32($lineMatch.Groups[3].Value, 16)
    if (($rawValue -ne $rawHex) -or
        ($blackMask -ne ((-bnot $rawValue) -band 0x0F))) {
      throw ("LINE bit mapping mismatch: " + $line)
    }
    Write-Log "Line query completed"
  }
  else {
    $motionAttempted = $true
    $signMismatches = [Collections.Generic.List[string]]::new()
    $cases = @(
      @{Wheel = "L"; Speed = "+150"},
      @{Wheel = "L"; Speed = "-150"},
      @{Wheel = "R"; Speed = "+150"},
      @{Wheel = "R"; Speed = "-150"}
    )
    if ($Wheel -ne "All") {
      $cases = @($cases | Where-Object { $_.Wheel -eq $Wheel })
    }
    if ($Direction -ne "All") {
      $directionPattern = if ($Direction -eq "Plus") { "^\+" } else { "^-" }
      $cases = @($cases | Where-Object {
        $_.Speed -match $directionPattern
      })
    }
    foreach ($case in $cases) {
      $serial.DiscardInBuffer()
      $null = Send-Command -Command "RESET" -Pattern "^OK RESET$"
      $resetStatus =
        Send-Command -Command "STATUS" -Pattern "^STATUS "
      Assert-ResetStatus -StatusLine $resetStatus

      $null = Send-Command -Command "ARM" -Pattern "^OK ARMED 5000$"
      Write-Log ("Running one pulse: {0} {1} 2000 ms" -f
        $case.Wheel, $case.Speed)
      $null = Send-Command `
        -Command ("MOTOR {0} {1} 2000" -f $case.Wheel, $case.Speed) `
        -Pattern "^OK MOTOR$"
      $terminal = Wait-ForTerminal -TimeoutMs 3200
      if ($terminal -match "^ERR ") {
        throw ("motor pulse failed: " + $terminal)
      }
      $status = Send-Command -Command "STATUS" -Pattern "^STATUS "
      if (($status -notmatch "mode=STANDBY") -or
          ($status -notmatch " stby=0 ") -or
          ($status -notmatch " L=0 ") -or
          ($status -notmatch " R=0 ")) {
        throw ("post-pulse state is not safe: " + $status)
      }

      $leftTotal = Get-StatusNumber -StatusLine $status -Field "Ltotal"
      $rightTotal = Get-StatusNumber -StatusLine $status -Field "Rtotal"
      $leftSpeed = Get-StatusNumber -StatusLine $status -Field "Lspeed"
      $rightSpeed = Get-StatusNumber -StatusLine $status -Field "Rspeed"
      $leftValid = Get-StatusNumber -StatusLine $status -Field "Lvalid"
      $rightValid = Get-StatusNumber -StatusLine $status -Field "Rvalid"
      if (($leftValid -ne 1) -or ($rightValid -ne 1)) {
        throw ("invalid encoder sample: " + $status)
      }
      if ($case.Wheel -eq "L") {
        $activeTotal = $leftTotal
        $activeSpeed = $leftSpeed
        $oppositeSpeed = $rightSpeed
      }
      else {
        $activeTotal = $rightTotal
        $activeSpeed = $rightSpeed
        $oppositeSpeed = $leftSpeed
      }
      if ($oppositeSpeed -ne 0) {
        throw ("opposite wheel mapped speed is nonzero: " + $status)
      }
      if ($activeTotal -eq 0) {
        throw ("active wheel produced no counts: " + $status)
      }
      if ($activeSpeed -eq 0) {
        throw ("active wheel mapped speed is zero: " + $status)
      }
      $expectedNegative = $case.Speed.StartsWith("-")
      $actualNegative = $activeSpeed -lt 0
      if ($expectedNegative -ne $actualNegative) {
        $message = ("{0} {1}: mapped speed {2}" -f
          $case.Wheel, $case.Speed, $activeSpeed)
        $signMismatches.Add($message)
        Write-Log ("ERROR: mapped speed sign differs: " + $message)
      }
    }
    if ($signMismatches.Count -ne 0) {
      throw ("encoder direction mismatch in {0} case(s): {1}" -f
        $signMismatches.Count, ($signMismatches -join " | "))
    }
    Write-Log "DirectionMatrix completed"
  }
}
catch {
  Write-Log ("FAILED: " + $_.Exception.Message)
  Send-SafeStopOnce
  throw
}
finally {
  if ($null -ne $serial) {
    if ($serial.IsOpen) {
      $serial.Close()
    }
    $serial.Dispose()
  }
  $logWriter.Dispose()
  Write-Host ("Log: " + $logPath)
}
