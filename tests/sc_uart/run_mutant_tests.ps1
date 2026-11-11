[CmdletBinding()]
param(
  [string]$Compiler = "C:/mingw64/bin/gcc.exe",
  [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$testSource = Join-Path $repoRoot "tests/sc_uart"
$productionSource = Join-Path $repoRoot "Core/Src/sc_uart.c"
$runId = [guid]::NewGuid().ToString("N")
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
  ("smart-car-sc-uart-mutants-" + $runId)
$temporaryRootResolved = (New-Item -ItemType Directory -Path $temporaryRoot).FullName
$buildRoot = Join-Path $repoRoot ("build/sc_uart-mutants-" + $runId)
$buildRootResolved = (New-Item -ItemType Directory -Path $buildRoot).FullName

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
    "-DCMAKE_C_COMPILER=$Compiler" "-DSC_UART_SOURCE=$SourcePath"
  Invoke-Native "cmake" --build $buildDirectory
  return $buildDirectory
}

function Invoke-ExpectedPass {
  param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$TestName
  )

  Write-Host "EXPECT PASS: $TestName"
  Invoke-Native "ctest" --test-dir $BuildDirectory `
    -R "^$TestName$" --output-on-failure
}

function Invoke-ExpectedFailure {
  param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$TestName
  )

  Write-Host "EXPECT FAIL: $TestName"
  $executable = Join-Path $BuildDirectory "sc_uart_tests.exe"
  if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "test executable not found: $executable"
  }

  $startInfo = [Diagnostics.ProcessStartInfo]::new()
  $startInfo.FileName = $executable
  $startInfo.ArgumentList.Add($TestName)
  $startInfo.UseShellExecute = $false
  $startInfo.RedirectStandardOutput = $true
  $startInfo.RedirectStandardError = $true
  $startInfo.CreateNoWindow = $true

  $process = $null
  for ($attempt = 1; $attempt -le 10; $attempt++) {
    try {
      $process = [Diagnostics.Process]::Start($startInfo)
      break
    }
    catch {
      $message = $_.Exception.Message
      $accessDenied = $message -match "access is denied" -or
                      $message -match "拒绝访问" -or
                      $message -match "being used by another process" -or
                      $message -match "另一个程序正在使用此文件"
      if ((-not $accessDenied) -or ($attempt -eq 10)) {
        throw
      }
      Start-Sleep -Seconds 1
    }
  }

  $standardOutput = $process.StandardOutput.ReadToEnd()
  $standardError = $process.StandardError.ReadToEnd()
  $process.WaitForExit()
  $testExitCode = $process.ExitCode
  $output = @($standardOutput, $standardError)
  Write-Host $standardOutput
  Write-Host $standardError
  if ($testExitCode -eq 0) {
    throw "mutant escaped detection: $TestName"
  }
  if (($output -join "`n") -notmatch "CHECK failed at ") {
    throw "mutant target did not execute a failing assertion: $TestName"
  }
}

function New-MutantSource {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Original,
    [Parameter(Mandatory = $true)][string]$Replacement
  )

  $utf8 = [Text.UTF8Encoding]::new($false)
  $sourceText = [IO.File]::ReadAllText($productionSource, $utf8)
  if (-not $sourceText.Contains($Original)) {
    throw "mutation anchor not found for $Name"
  }

  $mutatedText = $sourceText.Replace($Original, $Replacement)
  $mutantPath = Join-Path $temporaryRootResolved "$Name.c"
  [IO.File]::WriteAllText($mutantPath, $mutatedText, $utf8)
  return $mutantPath
}

try {
  Write-Host "MUTANT SOURCE DIR: $temporaryRootResolved"
  Write-Host "MUTANT BUILD DIR: $buildRootResolved"

  $baselineBuild = Build-TestExecutable -Name "baseline" `
    -SourcePath $productionSource
  Invoke-Native "ctest" --test-dir $baselineBuild --output-on-failure
  Invoke-ExpectedPass -BuildDirectory $baselineBuild -TestName "rx_overflow"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "rx_error_ordering"

  $rxOverflowMutant = New-MutantSource -Name "rx_overflow_overwrite" `
    -Original "  if (sc_uart_ring_free(ring) == 0u)" `
    -Replacement "  if (0)"
  $rxOverflowBuild = Build-TestExecutable -Name "rx_overflow_overwrite" `
    -SourcePath $rxOverflowMutant
  Invoke-ExpectedFailure -BuildDirectory $rxOverflowBuild `
    -TestName "rx_overflow"

  $errorByteMutant = New-MutantSource -Name "error_byte_enqueued" `
    -Original @'
  if (huart->ErrorCode != HAL_UART_ERROR_NONE)
  {
    ctx->stats.rx_error_bytes++;
    ctx->stats.last_hal_error = huart->ErrorCode;
    return;
  }
'@ `
    -Replacement @'
  if (huart->ErrorCode != HAL_UART_ERROR_NONE)
  {
    ctx->stats.rx_error_bytes++;
    ctx->stats.last_hal_error = huart->ErrorCode;
  }
'@
  $errorByteBuild = Build-TestExecutable -Name "error_byte_enqueued" `
    -SourcePath $errorByteMutant
  Invoke-ExpectedFailure -BuildDirectory $errorByteBuild `
    -TestName "rx_error_ordering"

  $errorRearmMutant = New-MutantSource -Name "error_no_rearm" `
    -Original @'
  if (huart->RxState == HAL_UART_STATE_READY)
  {
    primask = sc_uart_enter_critical();
    ctx->rx_armed = false;
    sc_uart_exit_critical(primask);
    (void)sc_uart_arm_rx(ctx);
  }
'@ `
    -Replacement @'
  if (huart->RxState == HAL_UART_STATE_READY)
  {
    primask = sc_uart_enter_critical();
    ctx->rx_armed = false;
    sc_uart_exit_critical(primask);
  }
'@
  $errorRearmBuild = Build-TestExecutable -Name "error_no_rearm" `
    -SourcePath $errorRearmMutant
  Invoke-ExpectedFailure -BuildDirectory $errorRearmBuild `
    -TestName "rx_error_ordering"

  Write-Host "MUTANT SUMMARY: baseline passed; 3/3 mutants failed their target tests"
}
finally {
  $resolvedTempBase = (Resolve-Path ([IO.Path]::GetTempPath())).Path.TrimEnd("\")
  $resolvedMutationRoot = [IO.Path]::GetFullPath($temporaryRootResolved).TrimEnd("\")
  if (($resolvedMutationRoot.StartsWith(
        $resolvedTempBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationRoot).StartsWith(
        "smart-car-sc-uart-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationRoot -Recurse -Force
  }
  else {
    Write-Warning "temporary mutation directory was not removed: $resolvedMutationRoot"
  }

  $resolvedBuildBase = [IO.Path]::GetFullPath(
    (Join-Path $repoRoot "build")).TrimEnd("\")
  $resolvedMutationBuildRoot = [IO.Path]::GetFullPath($buildRootResolved).TrimEnd("\")
  if (($resolvedMutationBuildRoot.StartsWith(
        $resolvedBuildBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationBuildRoot).StartsWith(
        "sc_uart-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationBuildRoot -Recurse -Force
  }
  else {
    Write-Warning "mutation build directory was not removed: $resolvedMutationBuildRoot"
  }
}
