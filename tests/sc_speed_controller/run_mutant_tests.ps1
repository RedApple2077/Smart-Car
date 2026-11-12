[CmdletBinding()]
param(
  [string]$Compiler = "C:/mingw64/bin/gcc.exe",
  [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$testSource = Join-Path $repoRoot "tests/sc_speed_controller"
$productionSource = Join-Path $repoRoot "Core/Src/sc_speed_controller.c"
$pidSource = Join-Path $repoRoot "Core/Src/sc_pid.c"
$runId = [guid]::NewGuid().ToString("N")
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
  ("smart-car-sc-speed-controller-mutants-" + $runId)
$temporaryRootResolved =
  (New-Item -ItemType Directory -Path $temporaryRoot).FullName
$buildRoot = Join-Path $repoRoot `
  ("build/sc_speed_controller-mutants-" + $runId)
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
    [Parameter(Mandatory = $true)][string]$SpeedSourcePath
  )

  $buildDirectory = Join-Path $buildRootResolved $Name
  Invoke-Native "cmake" -S $testSource -B $buildDirectory `
    -G $Generator -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_C_COMPILER=$Compiler" `
    "-DSC_SPEED_CONTROLLER_SOURCE=$SpeedSourcePath" `
    "-DSC_PID_SOURCE=$pidSource"
  Invoke-Native "cmake" --build $buildDirectory
  return $buildDirectory
}

function Invoke-ExpectedFailure {
  param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$TestName
  )

  Write-Host "EXPECT FAIL: $TestName"
  $executable = Join-Path $BuildDirectory "sc_speed_controller_tests.exe"
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
  $build = Build-TestExecutable -Name $Name -SpeedSourcePath $mutantPath
  Invoke-ExpectedFailure -BuildDirectory $build -TestName $TestName
  $script:mutantCount++
}

try {
  $baselineBuild = Build-TestExecutable -Name "baseline" `
    -SpeedSourcePath $productionSource
  Invoke-Native "ctest" --test-dir $baselineBuild --output-on-failure

  Invoke-Mutant -Name "output_limit_too_high" `
    -Original "SC_SPEED_OUTPUT_LIMIT_PERMILLE" `
    -Replacement "1000" `
    -TestName "target_and_output_ramp_limit"

  Invoke-Mutant -Name "output_ramp_disabled" `
    -Original @'
  output_step =
    sc_speed_rate_step(SC_SPEED_OUTPUT_RAMP_PERMILLE_S, dt_ms);
'@ `
    -Replacement @'
  output_step = SC_SPEED_OUTPUT_LIMIT_PERMILLE;
'@ `
    -TestName "target_and_output_ramp_limit"

  Invoke-Mutant -Name "target_ramp_disabled" `
    -Original @'
  side->target_ramped_mm_s = sc_speed_move_toward_i32(
    side->target_ramped_mm_s, requested_target_mm_s,
    sc_speed_rate_step(SC_SPEED_TARGET_RAMP_MM_S2, dt_ms));
'@ `
    -Replacement @'
  side->target_ramped_mm_s = sc_speed_move_toward_i32(
    side->target_ramped_mm_s, requested_target_mm_s, UINT32_MAX);
'@ `
    -TestName "target_and_output_ramp_limit"

  Invoke-Mutant -Name "invalid_feedback_accepted" `
    -Original @'
  if ((feedback->valid == 0u) || (feedback->left_valid == 0u) ||
      (feedback->right_valid == 0u) ||
      ((uint32_t)(now_ms - feedback->sample_time_ms) >
       SC_WHEEL_MAX_SNAPSHOT_AGE_MS))
'@ `
    -Replacement @'
  if ((uint32_t)(now_ms - feedback->sample_time_ms) == UINT32_MAX)
'@ `
    -TestName "invalid_or_stale_feedback"

  Invoke-Mutant -Name "right_uses_left_gains" `
    -Original @'
  if ((sc_speed_init_side_pid(&controller->left, &config->left) != SC_PID_OK) ||
      (sc_speed_init_side_pid(&controller->right, &config->right) != SC_PID_OK))
'@ `
    -Replacement @'
  if ((sc_speed_init_side_pid(&controller->left, &config->left) != SC_PID_OK) ||
      (sc_speed_init_side_pid(&controller->right, &config->left) != SC_PID_OK))
'@ `
    -TestName "different_left_right_gains"

  Invoke-Mutant -Name "zero_target_ramped" `
    -Original @'
  if (left_target_mm_s == 0)
  {
    sc_speed_force_zero_side(&controller->left);
  }
  if (right_target_mm_s == 0)
  {
    sc_speed_force_zero_side(&controller->right);
  }
'@ `
    -Replacement @'
  if ((left_target_mm_s == INT32_MIN) &&
      (right_target_mm_s == INT32_MIN))
  {
    sc_speed_force_zero_side(&controller->left);
    sc_speed_force_zero_side(&controller->right);
  }
'@ `
    -TestName "zero_target_and_stop"

  Write-Host ("MUTANT SUMMARY: baseline passed; {0}/{0} mutants failed target tests" -f $mutantCount)
}
finally {
  $resolvedTempBase = (Resolve-Path ([IO.Path]::GetTempPath())).Path.TrimEnd("\")
  $resolvedMutationRoot =
    [IO.Path]::GetFullPath($temporaryRootResolved).TrimEnd("\")
  if (($resolvedMutationRoot.StartsWith(
        $resolvedTempBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationRoot).StartsWith(
        "smart-car-sc-speed-controller-mutants-",
        [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationRoot -Recurse -Force
  }

  $resolvedBuildBase = [IO.Path]::GetFullPath(
    (Join-Path $repoRoot "build")).TrimEnd("\")
  $resolvedMutationBuildRoot =
    [IO.Path]::GetFullPath($buildRootResolved).TrimEnd("\")
  if (($resolvedMutationBuildRoot.StartsWith(
        $resolvedBuildBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationBuildRoot).StartsWith(
        "sc_speed_controller-mutants-",
        [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationBuildRoot -Recurse -Force
  }
}
