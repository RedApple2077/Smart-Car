[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [ValidatePattern('^COM[0-9]+$')]
  [string]$Port,
  [ValidateSet("Probe", "SetPid", "Identify", "Step", "Capture")]
  [string]$Action = "Probe",
  [ValidateSet("L", "R", "B")]
  [string]$Wheel = "B",
  [int]$KpMilli = 100,
  [int]$KiMilli = 10,
  [int]$KdMilli = 0,
  [ValidateRange(-500, 500)]
  [int]$TargetMmS = 200,
  [ValidateRange(100, 2000)]
  [int]$DurationMs = 2000,
  [ValidateRange(3, 9)]
  [int]$Rounds = 3,
  [switch]$SelfTestAdapter
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$logRoot = Join-Path $repoRoot "build/tune-logs"
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$rawLogPath = Join-Path $logRoot ("hc05-tune-{0}-{1}.log" -f $Action, $timestamp)
$utf8 = [Text.UTF8Encoding]::new($false)
$logWriter = [IO.StreamWriter]::new($rawLogPath, $false, $utf8)
$serial = $null
$captureLines = [Collections.Generic.List[string]]::new()

function Write-Log {
  param([string]$Message)

  $line = "{0:yyyy-MM-dd HH:mm:ss.fff} {1}" -f (Get-Date), $Message
  Write-Host $line
  $logWriter.WriteLine($line)
  $logWriter.Flush()
}

function Write-CsvNoBom {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][object[]]$Rows
  )

  $csv = $Rows | ConvertTo-Csv -NoTypeInformation
  [IO.File]::WriteAllLines($Path, $csv, $utf8)
}

function Test-UnsupportedSerialDeviceName {
  param([string]$Name)

  return $Name -match (
    "(?i)CH340|" +
    "USB(?:\s|[-_])*(?:TO(?:\s|[-_])*)?SERIAL|" +
    "USB(?:\s|[-_])*(?:TTL|串行|串口|转串口)|" +
    "CP210\d*|FTDI")
}

function ConvertTo-NormalizedContainerId {
  param([object]$Value)

  if ($null -eq $Value) {
    return $null
  }
  $text = ([string]$Value).Trim()
  if ([string]::IsNullOrWhiteSpace($text)) {
    return $null
  }
  try {
    return ([guid]$text).ToString("B").ToUpperInvariant()
  }
  catch {
    return $text.ToUpperInvariant()
  }
}

function Resolve-AdapterSupport {
  param(
    [string]$PortName,
    [object]$ComDevice,
    [bool]$ContainerLookupCompleted = $false,
    [object]$ComContainerId = $null,
    [bool]$SameContainerHc05Found = $false,
    [string]$ContainerDetail = ""
  )

  $acceptedMessage = "Only an HC-05 Bluetooth SPP COM port is accepted."
  if ($null -eq $ComDevice) {
    return [pscustomobject]@{
      supported = $false
      evidence = "$PortName has no queryable PnP device"
      message = "$acceptedMessage $PortName has no queryable device."
    }
  }

  $name = [string]$ComDevice.Name
  if ([string]::IsNullOrWhiteSpace($name)) {
    return [pscustomobject]@{
      supported = $false
      evidence = "$PortName has no device name"
      message = "$acceptedMessage $PortName has no queryable name."
    }
  }
  if (Test-UnsupportedSerialDeviceName -Name $name) {
    return [pscustomobject]@{
      supported = $false
      evidence = "$PortName is unsupported USB serial '$name'"
      message = "$acceptedMessage $PortName is reported as '$name'."
    }
  }

  $normalizedContainerId = ConvertTo-NormalizedContainerId $ComContainerId
  if ((-not $ContainerLookupCompleted) -or
      [string]::IsNullOrWhiteSpace($normalizedContainerId)) {
    return [pscustomobject]@{
      supported = $false
      evidence = (
        "$PortName '$name'; PnP container identity is unavailable" +
        $(if ([string]::IsNullOrWhiteSpace($ContainerDetail)) { "" } else { "; $ContainerDetail" })
      )
      message = (
        "$acceptedMessage Windows container identity for $PortName is unavailable; " +
        "a verified same-container HC-05 is required."
      )
    }
  }

  if ($SameContainerHc05Found) {
    return [pscustomobject]@{
      supported = $true
      evidence = "$PortName '$name'; HC-05 shares container $normalizedContainerId"
      message = ""
    }
  }

  return [pscustomobject]@{
    supported = $false
    evidence = (
      "$PortName '$name'; container $normalizedContainerId has no HC-05" +
      $(if ([string]::IsNullOrWhiteSpace($ContainerDetail)) { "" } else { "; $ContainerDetail" })
    )
    message = (
      "$acceptedMessage $PortName is not in the same PnP container as an HC-05 " +
      "device (container $normalizedContainerId)."
    )
  }
}

function Get-DeviceContainerId {
  param([string]$InstanceId)

  $property = Get-PnpDeviceProperty -InstanceId $InstanceId `
    -KeyName "DEVPKEY_Device_ContainerId" -ErrorAction Stop
  if ($null -eq $property -or $null -eq $property.Data) {
    return $null
  }
  return ConvertTo-NormalizedContainerId $property.Data
}

function Invoke-AdapterLogicSelfTest {
  $cases = @(
    [pscustomobject]@{
      name = "chinese_name_same_container_hc05"
      device = [pscustomobject]@{ Name = "蓝牙链接上的标准串行 (COM13)" }
      containerLookupCompleted = $true
      containerId = "{00018EE4-989C-5927-977A-39B62B0B53D8}"
      sameContainerHc05 = $true
      expected = $true
    },
    [pscustomobject]@{
      name = "bluetooth_name_without_container_rejected"
      device = [pscustomobject]@{ Name = "Bluetooth Standard Serial (COM13)" }
      containerLookupCompleted = $false
      containerId = $null
      sameContainerHc05 = $false
      expected = $false
    },
    [pscustomobject]@{
      name = "container_without_hc05_rejected"
      device = [pscustomobject]@{ Name = "蓝牙链接上的标准串行 (COM13)" }
      containerLookupCompleted = $true
      containerId = "{11111111-2222-3333-4444-555555555555}"
      sameContainerHc05 = $false
      expected = $false
    },
    [pscustomobject]@{
      name = "missing_device"
      device = $null
      containerLookupCompleted = $false
      containerId = $null
      sameContainerHc05 = $false
      expected = $false
    },
    [pscustomobject]@{
      name = "ch340_rejected"
      device = [pscustomobject]@{ Name = "USB-SERIAL CH340 (COM13)" }
      containerLookupCompleted = $true
      containerId = "{00018EE4-989C-5927-977A-39B62B0B53D8}"
      sameContainerHc05 = $true
      expected = $false
    }
  )

  foreach ($case in $cases) {
    $decision = Resolve-AdapterSupport `
      -PortName "COM13" `
      -ComDevice $case.device `
      -ContainerLookupCompleted $case.containerLookupCompleted `
      -ComContainerId $case.containerId `
      -SameContainerHc05Found $case.sameContainerHc05
    if ($decision.supported -ne $case.expected) {
      throw ("adapter self-test failed: {0}; {1}" -f $case.name, $decision.evidence)
    }
    Write-Host ("ADAPTER SELFTEST PASS: {0} -> supported={1}" -f
      $case.name, $decision.supported)
  }
  Write-Host ("ADAPTER SELFTEST OK: {0}/{0}" -f $cases.Count)
}

function Assert-SupportedAdapter {
  try {
    $devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop)
  }
  catch {
    $decision = Resolve-AdapterSupport -PortName $Port -ComDevice $null
    throw $decision.message
  }

  $portPattern = [regex]::Escape("($Port)")
  $device = $devices |
    Where-Object { $_.Name -match $portPattern } |
    Select-Object -First 1

  $containerLookupCompleted = $false
  $containerId = $null
  $sameContainerHc05Found = $false
  $containerDetail = ""
  if (($null -ne $device) -and -not [string]::IsNullOrWhiteSpace($device.DeviceID)) {
    try {
      $containerId = Get-DeviceContainerId -InstanceId $device.DeviceID
    }
    catch {
      $containerDetail = "COM ContainerId query failed: $($_.Exception.Message)"
    }
  }

  if ($null -ne $containerId) {
    try {
      $hc05Candidates = @(
        $devices | Where-Object { $_.Name -match "(?i)HC[- ]?05" })
      $containerLookupCompleted = $true
      foreach ($candidate in $hc05Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate.DeviceID)) {
          $containerLookupCompleted = $false
          $containerDetail = "HC-05 candidate '$($candidate.Name)' has no DeviceID"
          continue
        }
        try {
          $candidateContainerId = Get-DeviceContainerId -InstanceId $candidate.DeviceID
        }
        catch {
          $containerLookupCompleted = $false
          $containerDetail = "HC-05 ContainerId query failed: $($_.Exception.Message)"
          continue
        }
        if ($candidateContainerId -eq $containerId) {
          $sameContainerHc05Found = $true
          $containerDetail = "matched HC-05 device '$($candidate.Name)'"
          break
        }
      }
    }
    catch {
      $containerLookupCompleted = $false
      $containerDetail = "PnP container enumeration failed: $($_.Exception.Message)"
    }
  }

  $decision = Resolve-AdapterSupport `
    -PortName $Port `
    -ComDevice $device `
    -ContainerLookupCompleted $containerLookupCompleted `
    -ComContainerId $containerId `
    -SameContainerHc05Found $sameContainerHc05Found `
    -ContainerDetail $containerDetail
  Write-Log ("ADAPTER: " + $decision.evidence)
  if (-not $decision.supported) {
    throw $decision.message
  }
}

function Read-LoggedLine {
  try {
    $line = $serial.ReadLine()
  }
  catch [TimeoutException] {
    return $null
  }
  if ([string]::IsNullOrWhiteSpace($line)) {
    return $null
  }
  Write-Log ("RX: " + $line)
  return $line.Trim()
}

function Send-Command {
  param(
    [Parameter(Mandatory = $true)][string]$Command,
    [Parameter(Mandatory = $true)][string]$Pattern,
    [int]$TimeoutMs = 3000
  )

  Write-Log ("TX: " + $Command)
  $serial.WriteLine($Command)
  $stopwatch = [Diagnostics.Stopwatch]::StartNew()
  while ($stopwatch.ElapsedMilliseconds -lt $TimeoutMs) {
    $line = Read-LoggedLine
    if (($null -ne $line) -and ($line -match $Pattern)) {
      return $line
    }
  }
  throw ("timeout waiting for /{0}/ after {1}" -f $Pattern, $Command)
}

function Wait-ForTerminal {
  param([int]$TimeoutMs = 3500)

  $stopwatch = [Diagnostics.Stopwatch]::StartNew()
  while ($stopwatch.ElapsedMilliseconds -lt $TimeoutMs) {
    $line = Read-LoggedLine
    if ($null -eq $line) {
      continue
    }
    if (($line -match "^OK STOP AUTO") -or ($line -match "^ERR ")) {
      return $line
    }
  }
  throw "timeout waiting for automatic stop"
}

function Send-BestEffortStop {
  if (($null -eq $serial) -or (-not $serial.IsOpen)) {
    return
  }
  try {
    Write-Log "TX: STOP (cleanup)"
    $serial.WriteLine("STOP")
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    while ($stopwatch.ElapsedMilliseconds -lt 1200) {
      $line = Read-LoggedLine
      if (($null -ne $line) -and
          (($line -match "^OK STOP") -or ($line -match "^ERR "))) {
        break
      }
    }
  }
  catch {
    Write-Log ("STOP cleanup failed: " + $_.Exception.Message)
  }
}

function Get-StatusNumber {
  param([string]$StatusLine, [string]$Field)

  $match = [regex]::Match(
    $StatusLine, "(?:^|\s)" + [regex]::Escape($Field) + "=(-?[0-9]+)")
  if (-not $match.Success) {
    throw ("STATUS missing field {0}: {1}" -f $Field, $StatusLine)
  }
  return [long]$match.Groups[1].Value
}

function Assert-ResetStatus {
  param([string]$StatusLine)

  if ((Get-StatusNumber $StatusLine "Ltotal") -ne 0 -or
      (Get-StatusNumber $StatusLine "Rtotal") -ne 0 -or
      (Get-StatusNumber $StatusLine "Lvalid") -ne 1 -or
      (Get-StatusNumber $StatusLine "Rvalid") -ne 1) {
    throw ("RESET verification failed: " + $StatusLine)
  }
}

function Test-CanIgnorePreCaptureHeartbeat {
  param([string]$Line, [int]$ExpectedCount)

  return (($ExpectedCount -lt 0) -and ($Line -match "^HB "))
}

function Read-CaptureFrames {
  param([int]$TimeoutMs = 30000)

  $captureLines.Clear()
  $expectedCount = -1
  $ended = $false
  $protocolErrors = [Collections.Generic.List[string]]::new()
  Write-Log "TX: CAPTURE"
  $serial.WriteLine("CAPTURE")
  $stopwatch = [Diagnostics.Stopwatch]::StartNew()
  while ($stopwatch.ElapsedMilliseconds -lt $TimeoutMs) {
    $line = Read-LoggedLine
    if ($null -eq $line) {
      continue
    }
    if (Test-CanIgnorePreCaptureHeartbeat `
        -Line $line -ExpectedCount $expectedCount) {
      continue
    }
    if ($line -match "^OK CAPTURE BEGIN N=([0-9]+)$") {
      if ($expectedCount -ge 0) {
        $protocolErrors.Add("duplicate CAPTURE BEGIN")
      }
      else {
        $expectedCount = [int]$Matches[1]
        Write-Log ("CAPTURE BEGIN expected_count=" + $expectedCount)
      }
      continue
    }
    if ($line.StartsWith("CAP,")) {
      if ($expectedCount -lt 0) {
        $protocolErrors.Add("CAP frame received before CAPTURE BEGIN")
      }
      $captureLines.Add($line)
      continue
    }
    if ($line -eq "OK CAPTURE END") {
      $ended = $true
      break
    }
    if ($line -match "^ERR ") {
      throw ("CAPTURE failed: " + $line)
    }
    $protocolErrors.Add("unexpected line during CAPTURE: " + $line)
  }
  if (-not $ended) {
    throw "timeout waiting for OK CAPTURE END"
  }
  if ($expectedCount -lt 0) {
    $protocolErrors.Add("CAPTURE END without CAPTURE BEGIN")
  }
  if ($protocolErrors.Count -gt 0) {
    throw ("invalid CAPTURE protocol: " + ($protocolErrors -join "; "))
  }
  return [pscustomobject]@{
    expected_count = $expectedCount
    lines = @($captureLines)
  }
}

function Convert-CaptureRows {
  param([string[]]$Lines)

  $rows = [Collections.Generic.List[object]]::new()
  foreach ($line in $Lines) {
    $fields = $line.Split(",")
    if (($fields.Count -ne 11) -or ($fields[0] -ne "CAP")) {
      throw ("malformed CAPTURE field: " + $line)
    }
    $rows.Add([pscustomobject]@{
      timestamp_ms = [uint32]$fields[1]
      mode = $fields[2]
      target_left_mm_s = [int]$fields[3]
      target_right_mm_s = [int]$fields[4]
      measured_left_mm_s = [int]$fields[5]
      measured_right_mm_s = [int]$fields[6]
      pwm_left = [int]$fields[7]
      pwm_right = [int]$fields[8]
      fault = [int]$fields[9]
      controller_mode = [int]$fields[10]
    })
  }
  return @($rows)
}

function Test-CaptureIntegrity {
  param(
    [object[]]$Rows,
    [int]$ExpectedCount,
    [string]$ExpectedMode = "",
    [int]$ExpectedDurationMs = 0,
    [switch]$RequireZeroFault
  )

  $rowList = @($Rows)
  $errors = [Collections.Generic.List[string]]::new()
  $maxGapMs = $null
  if ($rowList.Count -ne $ExpectedCount) {
    $errors.Add((
      "sample count mismatch expected={0} actual={1}" -f
      $ExpectedCount, $rowList.Count))
  }
  if ($rowList.Count -eq 0) {
    $errors.Add("capture has no samples")
  }
  else {
    for ($index = 1; $index -lt $rowList.Count; $index++) {
      $gapMs = [int64]$rowList[$index].timestamp_ms -
        [int64]$rowList[$index - 1].timestamp_ms
      if (($null -eq $maxGapMs) -or ($gapMs -gt $maxGapMs)) {
        $maxGapMs = $gapMs
      }
      if ($gapMs -le 0) {
        $errors.Add((
          "timestamp is not strictly increasing at row {0}" -f $index))
      }
    }

    $modes = @($rowList |
      ForEach-Object { [string]$_.mode } |
      Select-Object -Unique)
    if ($modes.Count -ne 1) {
      $errors.Add("capture mixes experiment modes: " + ($modes -join ","))
    }
    if ((-not [string]::IsNullOrWhiteSpace($ExpectedMode)) -and
        (@($rowList | Where-Object { $_.mode -ne $ExpectedMode }).Count -gt 0)) {
      $errors.Add("capture mode does not match $ExpectedMode")
    }
    if ($RequireZeroFault -and
        (@($rowList | Where-Object { $_.fault -ne 0 }).Count -gt 0)) {
      $errors.Add("capture contains nonzero fault bits")
    }
  }

  $coverageRatio = $null
  $theoreticalSampleCount = $null
  $minimumSampleCount = $null
  $densityRatio = $null
  if ($ExpectedDurationMs -gt 0) {
    $theoreticalSampleCount =
      [int][Math]::Floor($ExpectedDurationMs / 10) + 1
    $minimumSampleCount =
      [int][Math]::Ceiling([double]$theoreticalSampleCount * 0.90)
    $densityRatio =
      [double]$rowList.Count / [double]$theoreticalSampleCount
    if ($ExpectedCount -lt $minimumSampleCount) {
      $errors.Add((
        "expected sample count {0} is below 90% of 100 Hz theory {1}" -f
        $ExpectedCount, $theoreticalSampleCount))
    }
    if ($rowList.Count -lt $minimumSampleCount) {
      $errors.Add((
        "actual sample count {0} is below 90% of 100 Hz theory {1}" -f
        $rowList.Count, $theoreticalSampleCount))
    }
    if (($null -ne $maxGapMs) -and ($maxGapMs -gt 30)) {
      $errors.Add((
        "capture timestamp gap {0} ms exceeds 30 ms" -f $maxGapMs))
    }
    if (($rowList.Count -lt 2) -or
        ([uint32]$rowList[-1].timestamp_ms -le
         [uint32]$rowList[0].timestamp_ms)) {
      $errors.Add("capture duration coverage cannot be established")
    }
    else {
      $spanMs = [int64]$rowList[-1].timestamp_ms -
        [int64]$rowList[0].timestamp_ms
      $coverageRatio = [double]$spanMs / [double]$ExpectedDurationMs
      if ($coverageRatio -lt 0.90) {
        $errors.Add((
          "capture duration coverage {0:P1} is below 90%" -f
          $coverageRatio))
      }
    }
  }

  return [pscustomobject]@{
    valid = ($errors.Count -eq 0)
    errors = @($errors)
    coverage_ratio = $coverageRatio
    theoretical_sample_count = $theoreticalSampleCount
    minimum_sample_count = $minimumSampleCount
    density_ratio = $densityRatio
    max_gap_ms = $maxGapMs
  }
}

function New-CaptureSelfTestRows {
  param(
    [uint32[]]$Timestamps,
    [int]$Fault = 0,
    [string]$Mode = "SPEED"
  )

  $rows = [Collections.Generic.List[object]]::new()
  foreach ($timestamp in $Timestamps) {
    $rows.Add([pscustomobject]@{
      timestamp_ms = $timestamp
      mode = $Mode
      fault = $Fault
    })
  }
  return @($rows)
}

function New-CaptureUniformSelfTestRows {
  param(
    [int]$DurationMs,
    [int]$IntervalMs = 10,
    [int]$Fault = 0,
    [string]$Mode = "SPEED"
  )

  $timestamps = [Collections.Generic.List[uint32]]::new()
  for ($timestamp = 0; $timestamp -le $DurationMs;
       $timestamp += $IntervalMs) {
    $timestamps.Add([uint32]$timestamp)
  }
  return New-CaptureSelfTestRows `
    -Timestamps @($timestamps) -Fault $Fault -Mode $Mode
}

function Invoke-CaptureIntegritySelfTest {
  $validRows = @(New-CaptureUniformSelfTestRows -DurationMs 2000)
  $missingRows = @($validRows | Select-Object -First 200)
  $duplicateRows = @(New-CaptureUniformSelfTestRows -DurationMs 2000)
  $duplicateRows[-1].timestamp_ms = $duplicateRows[-2].timestamp_ms
  $shortCoverageRows = @(
    New-CaptureUniformSelfTestRows -DurationMs 1000 -IntervalMs 5)
  $faultRows = @(New-CaptureUniformSelfTestRows -DurationMs 2000)
  $faultRows[100].fault = 1
  $slowRows = New-CaptureSelfTestRows `
    -Timestamps @(0, 200, 400, 600, 800, 1000, 1200, 1400, 1600, 1800, 2000)
  $gapTimes = [Collections.Generic.List[uint32]]::new()
  for ($timestamp = 0; $timestamp -le 900; $timestamp += 10) {
    $gapTimes.Add([uint32]$timestamp)
  }
  for ($timestamp = 1000; $timestamp -le 2000; $timestamp += 10) {
    $gapTimes.Add([uint32]$timestamp)
  }
  $gapRows = New-CaptureSelfTestRows -Timestamps @($gapTimes)

  $cases = @(
    [pscustomobject]@{
      name = "valid_complete_capture"
      rows = $validRows
      expectedCount = 201
      durationMs = 2000
      expectedValid = $true
    },
    [pscustomobject]@{
      name = "missing_sample_rejected"
      rows = $missingRows
      expectedCount = 201
      durationMs = 2000
      expectedValid = $false
    },
    [pscustomobject]@{
      name = "duplicate_timestamp_rejected"
      rows = $duplicateRows
      expectedCount = 201
      durationMs = 2000
      expectedValid = $false
    },
    [pscustomobject]@{
      name = "short_coverage_rejected"
      rows = $shortCoverageRows
      expectedCount = 201
      durationMs = 2000
      expectedValid = $false
    },
    [pscustomobject]@{
      name = "slow_sampling_rejected"
      rows = $slowRows
      expectedCount = 11
      durationMs = 2000
      expectedValid = $false
    },
    [pscustomobject]@{
      name = "max_gap_rejected"
      rows = $gapRows
      expectedCount = $gapRows.Count
      durationMs = 2000
      expectedValid = $false
    },
    [pscustomobject]@{
      name = "fault_rejected"
      rows = $faultRows
      expectedCount = 201
      durationMs = 2000
      expectedValid = $false
    }
  )

  foreach ($case in $cases) {
    $result = Test-CaptureIntegrity `
      -Rows $case.rows `
      -ExpectedCount $case.expectedCount `
      -ExpectedMode "SPEED" `
      -ExpectedDurationMs $case.durationMs `
      -RequireZeroFault
    if ($result.valid -ne $case.expectedValid) {
      throw ("capture self-test failed: {0}; {1}" -f
        $case.name, ($result.errors -join "; "))
    }
    Write-Host ("CAPTURE SELFTEST PASS: {0} -> valid={1}" -f
      $case.name, $result.valid)
  }
  if (-not (Test-CanIgnorePreCaptureHeartbeat `
        -Line "HB mode=STANDBY stby=0 L=0 R=0" -ExpectedCount -1)) {
    throw "pre-BEGIN heartbeat was not ignored"
  }
  if (Test-CanIgnorePreCaptureHeartbeat `
      -Line "HB mode=STANDBY stby=0 L=0 R=0" -ExpectedCount 201) {
    throw "post-BEGIN heartbeat was incorrectly ignored"
  }
  Write-Host "CAPTURE SELFTEST PASS: pre-BEGIN heartbeat ignored; post-BEGIN rejected"
  Write-Host ("CAPTURE SELFTEST OK: {0}/{0}" -f $cases.Count)
}

function Get-Median {
  param([double[]]$Values)

  if ($Values.Count -eq 0) {
    return $null
  }
  $sorted = @($Values | Sort-Object)
  $middle = [int][Math]::Floor($sorted.Count / 2)
  if (($sorted.Count % 2) -eq 1) {
    return [double]$sorted[$middle]
  }
  return ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2.0
}

function Get-TailMedian {
  param([object[]]$Rows, [string]$Property)

  $skip = [int][Math]::Floor($Rows.Count * 0.30)
  $tail = @($Rows | Select-Object -Skip $skip)
  return Get-Median -Values @($tail | ForEach-Object { [double]$_.$Property })
}

function Get-RelativeSpread {
  param([double[]]$Values)

  $median = Get-Median -Values $Values
  if (($null -eq $median) -or ($median -le 0)) {
    return $null
  }
  $minimum = ($Values | Measure-Object -Minimum).Minimum
  $maximum = ($Values | Measure-Object -Maximum).Maximum
  return ([double]$maximum - [double]$minimum) / [double]$median
}

function Invoke-OpenLoopRound {
  param([int]$Round)

  $null = Send-Command "RESET" "^OK RESET$"
  $status = Send-Command "STATUS" "^STATUS "
  Assert-ResetStatus $status
  $null = Send-Command "ARM" "^OK ARMED 5000$"
  $null = Send-Command "RAW B 396 1000" "^OK RAW PARAM=UNVERIFIED$"
  $terminal = Wait-ForTerminal -TimeoutMs 2500
  $capture = Read-CaptureFrames -TimeoutMs 30000
  $rows = @(Convert-CaptureRows -Lines @($capture.lines))
  $csvPath = Join-Path $logRoot (
    "identify-round{0}-{1}.csv" -f $Round, $timestamp)
  Write-CsvNoBom -Path $csvPath -Rows $rows
  $integrity = Test-CaptureIntegrity `
    -Rows $rows `
    -ExpectedCount $capture.expected_count `
    -ExpectedMode "RAW" `
    -ExpectedDurationMs 1000 `
    -RequireZeroFault
  if (-not $integrity.valid) {
    Write-Log ("CAPTURE integrity failed: " + ($integrity.errors -join "; "))
  }
  if ($terminal -match "^ERR ") {
    throw ("RAW failed: " + $terminal)
  }
  if (-not $integrity.valid) {
    throw ("CAPTURE integrity failed: " + ($integrity.errors -join "; "))
  }
  return [pscustomobject]@{
    round = $Round
    samples = $rows.Count
    expected_samples = $capture.expected_count
    coverage_ratio = $integrity.coverage_ratio
    density_ratio = $integrity.density_ratio
    max_gap_ms = $integrity.max_gap_ms
    vL396 = Get-TailMedian $rows "measured_left_mm_s"
    vR396 = Get-TailMedian $rows "measured_right_mm_s"
    csv = $csvPath
  }
}

try {
  if ($SelfTestAdapter) {
    Invoke-AdapterLogicSelfTest
    Invoke-CaptureIntegritySelfTest
    return
  }
  Assert-SupportedAdapter
  Write-Log ("Opening HC-05 SPP {0} at 9600 8N1" -f $Port)
  $serial = [IO.Ports.SerialPort]::new($Port, 9600, "None", 8, "One")
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
    $null = Send-Command "PING" "^OK PONG$"
    $status = Send-Command "STATUS" "^STATUS "
    Write-Host $status
    $null = Send-Command "STOP" "^OK STOP$"
  }
  elseif ($Action -eq "SetPid") {
    $command = "PID {0} {1} {2} {3}" -f `
      $Wheel, $KpMilli, $KiMilli, $KdMilli
    $response = Send-Command $command "^OK PID "
    $sides = if ($Wheel -eq "B") { @("L", "R") } else { @($Wheel) }
    foreach ($side in $sides) {
      if (((Get-StatusNumber $response ($side + "kp")) -ne $KpMilli) -or
          ((Get-StatusNumber $response ($side + "ki")) -ne $KiMilli) -or
          ((Get-StatusNumber $response ($side + "kd")) -ne $KdMilli)) {
        throw ("PID readback mismatch: " + $response)
      }
    }
    Write-Host $response
  }
  elseif ($Action -eq "Identify") {
    $results = [Collections.Generic.List[object]]::new()
    for ($round = 1; $round -le $Rounds; $round++) {
      $results.Add((Invoke-OpenLoopRound -Round $round))
    }
    $roundsPath = Join-Path $logRoot ("identify-rounds-$timestamp.csv")
    Write-CsvNoBom -Path $roundsPath -Rows @($results)
    $leftValues = @($results | ForEach-Object { [double]$_.vL396 })
    $rightValues = @($results | ForEach-Object { [double]$_.vR396 })
    $vL = Get-Median -Values $leftValues
    $vR = Get-Median -Values $rightValues
    $positiveRounds = -not (
      @($leftValues | Where-Object { $_ -le 0 }).Count -gt 0 -or
      @($rightValues | Where-Object { $_ -le 0 }).Count -gt 0)
    $leftSpread = Get-RelativeSpread -Values $leftValues
    $rightSpread = Get-RelativeSpread -Values $rightValues
    $repeatabilityOk = (
      $positiveRounds -and
      ($null -ne $leftSpread) -and
      ($null -ne $rightSpread) -and
      ($leftSpread -le 0.10) -and
      ($rightSpread -le 0.10))
    $warnings = [Collections.Generic.List[string]]::new()
    if (-not $positiveRounds) {
      $warnings.Add("one or more rounds had non-positive vL396/vR396")
    }
    if (($null -eq $leftSpread) -or ($null -eq $rightSpread)) {
      $warnings.Add("relative dispersion unavailable")
    }
    else {
      if ($leftSpread -gt 0.10) {
        $warnings.Add("left relative dispersion exceeds 10%")
      }
      if ($rightSpread -gt 0.10) {
        $warnings.Add("right relative dispersion exceeds 10%")
      }
    }
    $warning = if ($warnings.Count -eq 0) {
      ""
    }
    else {
      $warnings -join "; "
    }
    $minimumSpeed = if ($repeatabilityOk) {
      [Math]::Min($vL, $vR)
    }
    else {
      $null
    }
    $suggestedCruise = if ($null -ne $minimumSpeed) {
      [Math]::Round(0.97 * $minimumSpeed)
    }
    else {
      $null
    }
    $leftFf = if ($repeatabilityOk) {
      [Math]::Round(396.0 * 1000.0 / $vL)
    }
    else {
      $null
    }
    $rightFf = if ($repeatabilityOk) {
      [Math]::Round(396.0 * 1000.0 / $vR)
    }
    else {
      $null
    }
    $summaryPath = Join-Path $logRoot ("identify-summary-$timestamp.csv")
    $summary = @([pscustomobject]@{
      port = $Port
      rounds = $Rounds
      vL396 = $vL
      vR396 = $vR
      dispersion_left_ratio = $leftSpread
      dispersion_right_ratio = $rightSpread
      repeatability_ok = $repeatabilityOk
      warning = $warning
      suggested_cruise_mm_s = $suggestedCruise
      suggested_ff_left_milli = $leftFf
      suggested_ff_right_milli = $rightFf
      note = if ($repeatabilityOk) {
        "suggestions_only; source and VALID flags unchanged"
      }
      else {
        "raw results retained; suggestions withheld; source and VALID flags unchanged"
      }
    })
    Write-CsvNoBom -Path $summaryPath -Rows $summary
    Write-Host ("vL396={0} vR396={1}" -f $vL, $vR)
    Write-Host ("repeatability_ok={0} spreadL={1:P1} spreadR={2:P1}" -f
      $repeatabilityOk, $leftSpread, $rightSpread)
    if ($repeatabilityOk) {
      Write-Host ("suggested cruise={0} ffL={1} ffR={2}" -f
        $suggestedCruise, $leftFf, $rightFf)
    }
    else {
      Write-Host ("No cruise/ff suggestion: " + $warning)
    }
    Write-Host ("Round metrics: " + $roundsPath)
    Write-Host ("Summary: " + $summaryPath)
  }
  elseif ($Action -eq "Step") {
    $status = Send-Command "STATUS" "^STATUS "
    $applied = [pscustomobject]@{
      Lkp = Get-StatusNumber $status "Lkp"
      Lki = Get-StatusNumber $status "Lki"
      Lkd = Get-StatusNumber $status "Lkd"
      Lff = Get-StatusNumber $status "Lff"
      Rkp = Get-StatusNumber $status "Rkp"
      Rki = Get-StatusNumber $status "Rki"
      Rkd = Get-StatusNumber $status "Rkd"
      Rff = Get-StatusNumber $status "Rff"
    }
    $null = Send-Command "RESET" "^OK RESET$"
    Start-Sleep -Milliseconds 120
    $null = Send-Command "ARM" "^OK ARMED 5000$"
    $command = "SPEED {0} {1} {2}" -f $Wheel, $TargetMmS, $DurationMs
    $null = Send-Command $command "^OK SPEED PARAM=UNVERIFIED$"
    $terminal = Wait-ForTerminal -TimeoutMs ($DurationMs + 2500)
    $capture = Read-CaptureFrames -TimeoutMs 30000
    $rows = @(Convert-CaptureRows -Lines @($capture.lines))
    $rawCapturePath = Join-Path $logRoot ("step-capture-$timestamp.csv")
    Write-CsvNoBom -Path $rawCapturePath -Rows $rows
    $integrity = Test-CaptureIntegrity `
      -Rows $rows `
      -ExpectedCount $capture.expected_count `
      -ExpectedMode "SPEED" `
      -ExpectedDurationMs $DurationMs `
      -RequireZeroFault
    if (-not $integrity.valid) {
      Write-Log ("CAPTURE integrity failed: " + ($integrity.errors -join "; "))
    }
    if ($terminal -match "^ERR ") {
      throw ("SPEED failed: " + $terminal)
    }
    if (-not $integrity.valid) {
      throw ("CAPTURE integrity failed: " + ($integrity.errors -join "; "))
    }
    $leftRows = @($rows | ForEach-Object {
      [pscustomobject]@{
        timestamp_ms = $_.timestamp_ms
        setpoint = $_.target_left_mm_s
        input = $_.measured_left_mm_s
        pwm = $_.pwm_left
      }
    })
    $rightRows = @($rows | ForEach-Object {
      [pscustomobject]@{
        timestamp_ms = $_.timestamp_ms
        setpoint = $_.target_right_mm_s
        input = $_.measured_right_mm_s
        pwm = $_.pwm_right
      }
    })
    $leftPath = Join-Path $logRoot ("step-left-$timestamp.csv")
    $rightPath = Join-Path $logRoot ("step-right-$timestamp.csv")
    Write-CsvNoBom -Path $leftPath -Rows $leftRows
    Write-CsvNoBom -Path $rightPath -Rows $rightRows
    $metadataPath = Join-Path $logRoot ("step-meta-$timestamp.csv")
    $metadata = @([pscustomobject]@{
      port = $Port
      wheel = $Wheel
      setpoint_mm_s = $TargetMmS
      duration_ms = $DurationMs
      left_kp_milli = $applied.Lkp
      left_ki_milli = $applied.Lki
      left_kd_milli = $applied.Lkd
      left_ff_milli = $applied.Lff
      right_kp_milli = $applied.Rkp
      right_ki_milli = $applied.Rki
      right_kd_milli = $applied.Rkd
      right_ff_milli = $applied.Rff
      capture_expected_count = $capture.expected_count
      capture_coverage_ratio = $integrity.coverage_ratio
      capture_theoretical_sample_count = $integrity.theoretical_sample_count
      capture_minimum_sample_count = $integrity.minimum_sample_count
      capture_density_ratio = $integrity.density_ratio
      capture_max_gap_ms = $integrity.max_gap_ms
      source = "measured_hardware"
      note = "readback_confirmed; capture_integrity_passed"
    })
    Write-CsvNoBom -Path $metadataPath -Rows $metadata
    Write-Host ("Raw capture: " + $rawCapturePath)
    Write-Host ("Left: " + $leftPath)
    Write-Host ("Right: " + $rightPath)
    Write-Host ("Metadata: " + $metadataPath)
  }
  elseif ($Action -eq "Capture") {
    $status = Send-Command "STATUS" "^STATUS "
    if ($status -notmatch "mode=STANDBY") {
      throw "CAPTURE requires TB6612 standby"
    }
    $capture = Read-CaptureFrames -TimeoutMs 30000
    $rows = @(Convert-CaptureRows -Lines @($capture.lines))
    $capturePath = Join-Path $logRoot ("capture-$timestamp.csv")
    Write-CsvNoBom -Path $capturePath -Rows $rows
    $integrity = Test-CaptureIntegrity `
      -Rows $rows -ExpectedCount $capture.expected_count
    if (-not $integrity.valid) {
      throw ("CAPTURE integrity failed: " + ($integrity.errors -join "; "))
    }
    Write-Host ("Capture: " + $capturePath)
  }
}
catch {
  Write-Log ("FAILED: " + $_.Exception.Message)
  throw
}
finally {
  Send-BestEffortStop
  if ($null -ne $serial) {
    if ($serial.IsOpen) {
      $serial.Close()
    }
    $serial.Dispose()
  }
  $logWriter.Dispose()
  Write-Host ("Raw log: " + $rawLogPath)
}
