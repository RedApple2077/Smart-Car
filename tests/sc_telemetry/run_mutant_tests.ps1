[CmdletBinding()]
param(
  [string]$Compiler = "C:/mingw64/bin/gcc.exe",
  [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$testSource = Join-Path $repoRoot "tests/sc_telemetry"
$productionSource = Join-Path $repoRoot "Core/Src/sc_telemetry.c"
$runId = [guid]::NewGuid().ToString("N")
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
  ("smart-car-sc-telemetry-mutants-" + $runId)
$temporaryRootResolved =
  (New-Item -ItemType Directory -Path $temporaryRoot).FullName
$buildRoot = Join-Path $repoRoot ("build/sc_telemetry-mutants-" + $runId)
$buildRootResolved =
  (New-Item -ItemType Directory -Path $buildRoot).FullName
$mutantCount = 0

function Invoke-Native {
  param(
    [Parameter(Mandatory = $true)][string]$FilePath,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments
  )

  & $FilePath @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
  if ($LASTEXITCODE -ne 0) {
    throw ("command failed with exit code {0}: {1} {2}" -f
      $LASTEXITCODE, $FilePath, ($Arguments -join " "))
  }
}

function Build-TestExecutable {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$SourcePath
  )

  $buildDirectory = Join-Path $buildRootResolved $Name
  Invoke-Native "cmake" -S $testSource -B $buildDirectory `
    -G $Generator -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_C_COMPILER=$Compiler" `
    "-DSC_TELEMETRY_SOURCE=$SourcePath"
  Invoke-Native "cmake" --build $buildDirectory
  return $buildDirectory
}

function Invoke-ExpectedFailure {
  param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$TestName
  )

  Write-Host "EXPECT FAIL: $TestName"
  $executable = Join-Path $BuildDirectory "sc_telemetry_tests.exe"
  $startInfo = [Diagnostics.ProcessStartInfo]::new()
  $startInfo.FileName = $executable
  $startInfo.ArgumentList.Add($TestName)
  $startInfo.UseShellExecute = $false
  $startInfo.RedirectStandardOutput = $true
  $startInfo.RedirectStandardError = $true
  $startInfo.CreateNoWindow = $true

  $process = [Diagnostics.Process]::Start($startInfo)
  $standardOutput = $process.StandardOutput.ReadToEnd()
  $standardError = $process.StandardError.ReadToEnd()
  $process.WaitForExit()
  Write-Host $standardOutput
  Write-Host $standardError
  if ($process.ExitCode -eq 0) {
    throw "mutant escaped detection: $TestName"
  }
  if (($standardOutput + "`n" + $standardError) -notmatch
      "CHECK failed at ") {
    throw "mutant target did not execute a failing assertion: $TestName"
  }
}

function Invoke-Mutant {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Original,
    [Parameter(Mandatory = $true)][string]$Replacement,
    [Parameter(Mandatory = $true)][string]$TestName
  )

  $utf8 = [Text.UTF8Encoding]::new($false)
  $sourceText = [IO.File]::ReadAllText($productionSource, $utf8)
  if (-not $sourceText.Contains($Original)) {
    throw "mutation anchor not found for $Name"
  }
  $mutantPath = Join-Path $temporaryRootResolved "$Name.c"
  [IO.File]::WriteAllText(
    $mutantPath, $sourceText.Replace($Original, $Replacement), $utf8)
  $build = Build-TestExecutable -Name $Name -SourcePath $mutantPath
  Invoke-ExpectedFailure -BuildDirectory $build -TestName $TestName
  $script:mutantCount++
}

try {
  $baselineBuild = Build-TestExecutable -Name "baseline" `
    -SourcePath $productionSource
  Invoke-Native "ctest" --test-dir $baselineBuild --output-on-failure

  Invoke-Mutant -Name "uart_rate_too_fast" `
    -Original "    s_next_uart_ms = now_ms + SC_TELEMETRY_UART_PERIOD_MS;" `
    -Replacement "    s_next_uart_ms = now_ms + 1u;" `
    -TestName "uart_rate_and_csv_frame"

  Invoke-Mutant -Name "uart_failure_not_degraded" `
    -Original @'
    if ((sc_telemetry_build_frame(snapshot, now_ms) == 0u) ||
        (sc_uart_write(s_frame) != SC_UART_OK))
    {
      degraded |= SC_APP_DEGRADED_TELEMETRY;
      result = SC_TELEMETRY_ERR_UART;
    }
'@ `
    -Replacement @'
    (void)sc_telemetry_build_frame(snapshot, now_ms);
    (void)sc_uart_write(s_frame);
'@ `
    -TestName "uart_failure_only_degrades"

  Invoke-Mutant -Name "display_failure_not_degraded" `
    -Original @'
    if (sc_telemetry_update_oled(snapshot) == 0u)
    {
      degraded |= SC_APP_DEGRADED_DISPLAY;
      result = (result == SC_TELEMETRY_OK) ?
        SC_TELEMETRY_ERR_DISPLAY : result;
    }
'@ `
    -Replacement @'
    (void)sc_telemetry_update_oled(snapshot);
'@ `
    -TestName "display_failure_degrades"

  Invoke-Mutant -Name "async_display_status_ignored" `
    -Original @'
  display_status_result = sc_ssd1306_get_status(&display_status);
  if ((display_status_result != SC_SSD1306_OK) ||
      ((display_status_result == SC_SSD1306_OK) &&
       ((display_status.last_status != SC_SSD1306_OK) ||
        (display_status.state == SC_SSD1306_STATE_RETRY_WAIT) ||
        (display_status.retry_count != s_last_display_retry_count))))
  {
    degraded |= SC_APP_DEGRADED_DISPLAY;
    result = (result == SC_TELEMETRY_OK) ?
      SC_TELEMETRY_ERR_DISPLAY : result;
  }
'@ `
    -Replacement @'
  display_status_result = sc_ssd1306_get_status(&display_status);
  (void)display_status;
  (void)display_status_result;
'@ `
    -TestName "async_display_failure_degrades"

  Invoke-Mutant -Name "idle_led_on" `
    -Original @'
    case SC_APP_PHASE_IDLE:
      on = 0u;
      break;
'@ `
    -Replacement @'
    case SC_APP_PHASE_IDLE:
      on = 1u;
      break;
'@ `
    -TestName "oled_rate_and_led"

  Write-Host ("MUTANT SUMMARY: baseline passed; {0}/{0} mutants failed target tests" -f $mutantCount)
}
finally {
  $resolvedTempBase = (Resolve-Path ([IO.Path]::GetTempPath())).Path.TrimEnd("\")
  $resolvedMutationRoot =
    [IO.Path]::GetFullPath($temporaryRootResolved).TrimEnd("\")
  if (($resolvedMutationRoot.StartsWith(
        $resolvedTempBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationRoot).StartsWith(
        "smart-car-sc-telemetry-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationRoot -Recurse -Force
  }

  $resolvedBuildBase = [IO.Path]::GetFullPath(
    (Join-Path $repoRoot "build")).TrimEnd("\")
  $resolvedMutationBuildRoot =
    [IO.Path]::GetFullPath($buildRootResolved).TrimEnd("\")
  if (($resolvedMutationBuildRoot.StartsWith(
        $resolvedBuildBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationBuildRoot).StartsWith(
        "sc_telemetry-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationBuildRoot -Recurse -Force
  }
}
