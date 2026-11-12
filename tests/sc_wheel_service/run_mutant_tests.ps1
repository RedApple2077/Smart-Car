[CmdletBinding()]
param(
  [string]$Compiler = "C:/mingw64/bin/gcc.exe",
  [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$testSource = Join-Path $repoRoot "tests/sc_wheel_service"
$productionSource = Join-Path $repoRoot "Core/Src/sc_wheel_service.c"
$runId = [guid]::NewGuid().ToString("N")
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
  ("smart-car-sc-wheel-service-mutants-" + $runId)
$temporaryRootResolved =
  (New-Item -ItemType Directory -Path $temporaryRoot).FullName
$buildRoot = Join-Path $repoRoot ("build/sc_wheel_service-mutants-" + $runId)
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
    "-DSC_WHEEL_SERVICE_SOURCE=$SourcePath"
  Invoke-Native "cmake" --build $buildDirectory
  return $buildDirectory
}

function Invoke-ExpectedFailure {
  param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$TestName
  )

  Write-Host "EXPECT FAIL: $TestName"
  $executable = Join-Path $BuildDirectory "sc_wheel_service_tests.exe"
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

  Invoke-Mutant -Name "window_complete_early" `
    -Original "  if (window->sample_count < SC_WHEEL_HISTORY_SAMPLES)" `
    -Replacement "  if (window->sample_count < 2u)" `
    -TestName "precision_sign_and_five_frame_window"

  Invoke-Mutant -Name "duplicate_sequence_accepted" `
    -Original @'
  if ((service->has_snapshot != 0u) &&
      (snapshot->sequence == service->last_sequence))
  {
    return SC_WHEEL_SERVICE_ERR_DUPLICATE_SEQUENCE;
  }
'@ `
    -Replacement @'
  if (0)
  {
    return SC_WHEEL_SERVICE_ERR_DUPLICATE_SEQUENCE;
  }
'@ `
    -TestName "duplicate_sequence"

  Invoke-Mutant -Name "stale_snapshot_accepted" `
    -Original @'
  if ((uint32_t)(now_ms - snapshot->sample_time_ms) >
      SC_WHEEL_MAX_SNAPSHOT_AGE_MS)
'@ `
    -Replacement @'
  if ((uint32_t)(now_ms - snapshot->sample_time_ms) == UINT32_MAX)
'@ `
    -TestName "stale_snapshot"

  Invoke-Mutant -Name "invalid_right_marked_valid" `
    -Original @'
  else
  {
    sc_wheel_reset_window(&service->right);
  }

  feedback->valid =
'@ `
    -Replacement @'
  else
  {
    sc_wheel_reset_window(&service->right);
    feedback->right_valid = 1u;
  }

  feedback->valid =
'@ `
    -TestName "left_right_independent_validity"

  Invoke-Mutant -Name "wheel_pi_precision" `
    -Original "#define SC_WHEEL_PI_NUM  355" `
    -Replacement "#define SC_WHEEL_PI_NUM  314" `
    -TestName "precision_sign_and_five_frame_window"

  Invoke-Mutant -Name "window_uses_fixed_ten_ms" `
    -Original @'
  elapsed_ms =
    (uint32_t)(window->sample_time_ms[newest_index] -
               window->sample_time_ms[oldest_index]);
'@ `
    -Replacement @'
  elapsed_ms = 10u;
'@ `
    -TestName "sliding_window_actual_time"

  Write-Host ("MUTANT SUMMARY: baseline passed; {0}/{0} mutants failed target tests" -f $mutantCount)
}
finally {
  $resolvedTempBase = (Resolve-Path ([IO.Path]::GetTempPath())).Path.TrimEnd("\")
  $resolvedMutationRoot =
    [IO.Path]::GetFullPath($temporaryRootResolved).TrimEnd("\")
  if (($resolvedMutationRoot.StartsWith(
        $resolvedTempBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationRoot).StartsWith(
        "smart-car-sc-wheel-service-mutants-",
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
        "sc_wheel_service-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationBuildRoot -Recurse -Force
  }
}
