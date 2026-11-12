[CmdletBinding()]
param(
  [string]$Compiler = "C:/mingw64/bin/gcc.exe",
  [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$testSource = Join-Path $repoRoot "tests/sc_app"
$productionSource = Join-Path $repoRoot "Core/Src/sc_app.c"
$runId = [guid]::NewGuid().ToString("N")
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
  ("smart-car-sc-app-mutants-" + $runId)
$temporaryRootResolved =
  (New-Item -ItemType Directory -Path $temporaryRoot).FullName
$buildRoot = Join-Path $repoRoot ("build/sc_app-mutants-" + $runId)
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
    "-DCMAKE_C_COMPILER=$Compiler" "-DSC_APP_SOURCE=$SourcePath"
  Invoke-Native "cmake" --build $buildDirectory
  return $buildDirectory
}

function Invoke-ExpectedFailure {
  param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$TestName
  )

  Write-Host "EXPECT FAIL: $TestName"
  $executable = Join-Path $BuildDirectory "sc_app_tests.exe"
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
  $sourceText = [IO.File]::ReadAllText(
    $productionSource, $utf8).Replace("`r`n", "`n")
  $normalizedOriginal = $Original.Replace("`r`n", "`n")
  $normalizedReplacement = $Replacement.Replace("`r`n", "`n")
  if (-not $sourceText.Contains($normalizedOriginal)) {
    throw "mutation anchor not found for $Name"
  }
  $mutantPath = Join-Path $temporaryRootResolved "$Name.c"
  [IO.File]::WriteAllText(
    $mutantPath,
    $sourceText.Replace($normalizedOriginal, $normalizedReplacement),
    $utf8)
  $build = Build-TestExecutable -Name $Name -SourcePath $mutantPath
  Invoke-ExpectedFailure -BuildDirectory $build -TestName $TestName
  $script:mutantCount++
}

try {
  $baselineBuild = Build-TestExecutable -Name "baseline" `
    -SourcePath $productionSource
  Invoke-Native "ctest" --test-dir $baselineBuild --output-on-failure

  Invoke-Mutant -Name "isolated_release_starts" `
    -Original @'
    uint8_t short_release =
      (uint8_t)((app->button_press_seen != 0u) &&
                (app->long_press_consumed == 0u));
'@ `
    -Replacement "    uint8_t short_release = 1u;" `
    -TestName "isolated_release_does_not_start"

  Invoke-Mutant -Name "config_gate_removed" `
    -Original @'
static uint8_t sc_app_start_config_valid(const sc_app_t *app)
{
  return (uint8_t)(
    (app->config.calibration_valid != 0u) &&
    (app->config.tuning_valid != 0u));
}
'@ `
    -Replacement @'
static uint8_t sc_app_start_config_valid(const sc_app_t *app)
{
  (void)app;
  return 1u;
}
'@ `
    -TestName "full_white_and_invalid_config_block_start"

  Invoke-Mutant -Name "long_press_ignored" `
    -Original "  if ((events & SC_BUTTON_EVENT_LONG_PRESS) != 0u)" `
    -Replacement "  if ((events & 0x02u) == 0x02u)" `
    -TestName "long_press_stops_then_new_click_restarts"

  Invoke-Mutant -Name "stall_window_reset_by_jitter" `
    -Original "    if (stall_condition != 0u)" `
    -Replacement @'
    if ((stall_condition != 0u) && (physical_delta <= 0))
'@ `
    -TestName "jittery_counts_fail_progress_window"

  Invoke-Mutant -Name "direction_not_fatal" `
    -Original "    if (app->reverse_samples[side] >= SC_APP_REVERSE_SAMPLES)" `
    -Replacement "    if (app->reverse_samples[side] >= 255u)" `
    -TestName "reverse_direction_is_fatal"

  Invoke-Mutant -Name "direction_sign_gate_removed" `
    -Original @'
    uint8_t command_matches_target =
      (uint8_t)((target_sign != 0) && (target_sign == command_sign));
'@ `
    -Replacement @'
    uint8_t command_matches_target = 1u;
    (void)target_sign;
'@ `
    -TestName "negative_command_forward_counts_no_direction_fault"

  Invoke-Mutant -Name "line_lost_recovery_ignores_line" `
    -Original @'
      (sc_app_feedback_fresh(app, now_ms) == 0u) ||
      (app->line.black_mask == 0x00u))
  {
    return 0u;
  }
  if (((app->fault_bits & SC_APP_FAULT_LINE_LOST) != 0u) &&
      ((app->line.black_mask != 0x06u) ||
       (sc_app_center_window_ready(app) == 0u)))
  {
    return 0u;
  }
'@ `
    -Replacement @'
      (sc_app_feedback_fresh(app, now_ms) == 0u))
  {
    return 0u;
  }
'@ `
    -TestName "line_lost_recovery_requires_center"

  Invoke-Mutant -Name "recovery_accepts_white_line" `
    -Original "      (app->line.black_mask == 0x00u))" `
    -Replacement "      0u)" `
    -TestName "feedback_recovery_requires_nonwhite_line"

  Invoke-Mutant -Name "tb_fault_recoverable" `
    -Original @'
    if (sc_tb6612_apply(left_pwm, right_pwm) != SC_TB6612_OK)
    {
      return SC_APP_FAULT_TB6612;
    }
'@ `
    -Replacement @'
    if (sc_tb6612_apply(left_pwm, right_pwm) != SC_TB6612_OK)
    {
      return SC_APP_FAULT_CONTROL;
    }
'@ `
    -TestName "tb_fault_is_fatal"

  Invoke-Mutant -Name "motor_sign_ignored" `
    -Original @'
  snapshot->left_pwm = sc_app_apply_motor_sign(
    app->speed_output.left_permille, app->config.left_motor_sign);
'@ `
    -Replacement @'
  snapshot->left_pwm = app->speed_output.left_permille;
'@ `
    -TestName "snapshot_pwm_uses_motor_sign"

  Write-Host ("MUTANT SUMMARY: baseline passed; {0}/{0} mutants failed target tests" -f $mutantCount)
}
finally {
  $resolvedTempBase = (Resolve-Path ([IO.Path]::GetTempPath())).Path.TrimEnd("\")
  $resolvedMutationRoot =
    [IO.Path]::GetFullPath($temporaryRootResolved).TrimEnd("\")
  if (($resolvedMutationRoot.StartsWith(
        $resolvedTempBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationRoot).StartsWith(
        "smart-car-sc-app-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationRoot -Recurse -Force
  }

  $resolvedBuildBase = [IO.Path]::GetFullPath(
    (Join-Path $repoRoot "build")).TrimEnd("\")
  $resolvedMutationBuildRoot =
    [IO.Path]::GetFullPath($buildRootResolved).TrimEnd("\")
  if (($resolvedMutationBuildRoot.StartsWith(
        $resolvedBuildBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationBuildRoot).StartsWith(
        "sc_app-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationBuildRoot -Recurse -Force
  }
}
