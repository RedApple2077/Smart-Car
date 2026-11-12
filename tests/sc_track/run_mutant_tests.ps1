[CmdletBinding()]
param(
  [string]$Compiler = "C:/mingw64/bin/gcc.exe",
  [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$testSource = Join-Path $repoRoot "tests/sc_track"
$productionSource = Join-Path $repoRoot "Core/Src/sc_track.c"
$runId = [guid]::NewGuid().ToString("N")
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
  ("smart-car-sc-track-mutants-" + $runId)
$temporaryRootResolved =
  (New-Item -ItemType Directory -Path $temporaryRoot).FullName
$buildRoot = Join-Path $repoRoot ("build/sc_track-mutants-" + $runId)
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
    "-DCMAKE_C_COMPILER=$Compiler" "-DSC_TRACK_SOURCE=$SourcePath"
  Invoke-Native "cmake" --build $buildDirectory
  return $buildDirectory
}

function Invoke-ExpectedFailure {
  param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$TestName
  )

  Write-Host "EXPECT FAIL: $TestName"
  $executable = Join-Path $BuildDirectory "sc_track_tests.exe"
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

  Invoke-Mutant -Name "single_sample_confirms" `
    -Original @'
  if ((controller->recovery_sample_count >= SC_TRACK_RECOVERY_SAMPLES) &&
      ((uint32_t)(line->sample.sample_time_ms -
                  controller->recovery_first_sample_time_ms) >=
       SC_TRACK_RECOVERY_MIN_TIME_MS))
'@ `
    -Replacement @'
  if (fresh != 0u)
'@ `
    -TestName "single_black_sample_does_not_reset_recovery"

  Invoke-Mutant -Name "all_white_blind_forward" `
    -Original @'
  else
  {
    sc_track_clear_target(target_out);
    return SC_TRACK_ERR_LOST;
  }
  return SC_TRACK_RECOVERING;
'@ `
    -Replacement @'
  else
  {
    sc_track_set_target(controller, SC_TRACK_CENTER_RATIO,
                        SC_TRACK_CENTER_RATIO, target_out);
  }
  return SC_TRACK_RECOVERING;
'@ `
    -TestName "unconditional_all_white_stop"

  Invoke-Mutant -Name "wrong_fine_ratio" `
    -Original "#define SC_TRACK_FINE_INNER_RATIO     558" `
    -Replacement "#define SC_TRACK_FINE_INNER_RATIO     145" `
    -TestName "ratio_table"

  Invoke-Mutant -Name "center_keeps_direction" `
    -Original @'
      sc_track_set_target(controller, SC_TRACK_CENTER_RATIO,
                          SC_TRACK_CENTER_RATIO, target_out);
      controller->last_turn_direction = 0;
      return SC_TRACK_OK;
'@ `
    -Replacement @'
      sc_track_set_target(controller, SC_TRACK_CENTER_RATIO,
                          SC_TRACK_CENTER_RATIO, target_out);
      return SC_TRACK_OK;
'@ `
    -TestName "center_and_junction_clear_direction"

  Invoke-Mutant -Name "recovery_ignores_minimum_time" `
    -Original "#define SC_TRACK_RECOVERY_MIN_TIME_MS 20u" `
    -Replacement "#define SC_TRACK_RECOVERY_MIN_TIME_MS 1u" `
    -TestName "three_fresh_samples_and_minimum_time_confirm"

  Invoke-Mutant -Name "duplicate_sequence_counts" `
    -Original @'
  uint8_t fresh =
    (uint8_t)((controller->recovery_has_sequence == 0u) ||
              (line->sample.sequence !=
               controller->recovery_last_sequence));
'@ `
    -Replacement "  uint8_t fresh = 1u;" `
    -TestName "duplicate_sequence_does_not_confirm"

  Invoke-Mutant -Name "white_keeps_recovery_candidate" `
    -Original @'
    if (line->black_mask == 0x00u)
    {
      sc_track_reset_recovery_candidate(controller);
      return sc_track_output_search(controller, target_out);
    }
'@ `
    -Replacement @'
    if (line->black_mask == 0x00u)
    {
      return sc_track_output_search(controller, target_out);
    }
'@ `
    -TestName "alternating_white_black_uses_total_deadline"

  Write-Host ("MUTANT SUMMARY: baseline passed; {0}/{0} mutants failed target tests" -f $mutantCount)
}
finally {
  $resolvedTempBase = (Resolve-Path ([IO.Path]::GetTempPath())).Path.TrimEnd("\")
  $resolvedMutationRoot =
    [IO.Path]::GetFullPath($temporaryRootResolved).TrimEnd("\")
  if (($resolvedMutationRoot.StartsWith(
        $resolvedTempBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationRoot).StartsWith(
        "smart-car-sc-track-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationRoot -Recurse -Force
  }

  $resolvedBuildBase = [IO.Path]::GetFullPath(
    (Join-Path $repoRoot "build")).TrimEnd("\")
  $resolvedMutationBuildRoot =
    [IO.Path]::GetFullPath($buildRootResolved).TrimEnd("\")
  if (($resolvedMutationBuildRoot.StartsWith(
        $resolvedBuildBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationBuildRoot).StartsWith(
        "sc_track-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationBuildRoot -Recurse -Force
  }
}
