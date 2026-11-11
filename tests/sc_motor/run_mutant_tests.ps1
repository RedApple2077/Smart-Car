[CmdletBinding()]
param(
  [string]$Compiler = "C:/mingw64/bin/gcc.exe",
  [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$testSource = Join-Path $repoRoot "tests/sc_motor"
$tbSource = Join-Path $repoRoot "Core/Src/sc_tb6612.c"
$encoderSource = Join-Path $repoRoot "Core/Src/sc_encoder.c"
$runId = [guid]::NewGuid().ToString("N")
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
  ("smart-car-sc-motor-mutants-" + $runId)
$temporaryRootResolved =
  (New-Item -ItemType Directory -Path $temporaryRoot).FullName
$buildRoot = Join-Path $repoRoot ("build/sc_motor-mutants-" + $runId)
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
    [Parameter(Mandatory = $true)][string]$TbPath,
    [Parameter(Mandatory = $true)][string]$EncoderPath
  )

  $buildDirectory = Join-Path $buildRootResolved $Name
  Invoke-Native "cmake" -S $testSource -B $buildDirectory `
    -G $Generator -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_C_COMPILER=$Compiler" `
    "-DSC_TB6612_SOURCE=$TbPath" `
    "-DSC_ENCODER_SOURCE=$EncoderPath"
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
  $executable = Join-Path $BuildDirectory "sc_motor_tests.exe"
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

  $process = [Diagnostics.Process]::Start($startInfo)
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
    [Parameter(Mandatory = $true)][string]$SourcePath,
    [Parameter(Mandatory = $true)][string]$Original,
    [Parameter(Mandatory = $true)][string]$Replacement
  )

  $utf8 = [Text.UTF8Encoding]::new($false)
  $sourceText = [IO.File]::ReadAllText($SourcePath, $utf8)
  if (-not $sourceText.Contains($Original)) {
    throw "mutation anchor not found for $Name"
  }

  $mutatedText = $sourceText.Replace($Original, $Replacement)
  $mutantPath = Join-Path $temporaryRootResolved "$Name.c"
  [IO.File]::WriteAllText($mutantPath, $mutatedText, $utf8)
  return $mutantPath
}

function Invoke-Mutant {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$SourcePath,
    [Parameter(Mandatory = $true)][string]$Original,
    [Parameter(Mandatory = $true)][string]$Replacement,
    [Parameter(Mandatory = $true)][string]$TestName
  )

  $mutant = New-MutantSource -Name $Name -SourcePath $SourcePath `
    -Original $Original -Replacement $Replacement
  if ($SourcePath -eq $tbSource) {
    $build = Build-TestExecutable -Name $Name -TbPath $mutant `
      -EncoderPath $encoderSource
  }
  else {
    $build = Build-TestExecutable -Name $Name -TbPath $tbSource `
      -EncoderPath $mutant
  }
  Invoke-ExpectedFailure -BuildDirectory $build -TestName $TestName
  $script:mutantCount++
}

try {
  Write-Host "MUTANT SOURCE DIR: $temporaryRootResolved"
  Write-Host "MUTANT BUILD DIR: $buildRootResolved"

  $baselineBuild = Build-TestExecutable -Name "baseline" `
    -TbPath $tbSource -EncoderPath $encoderSource
  Invoke-Native "ctest" --test-dir $baselineBuild --output-on-failure

  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "tb_scaling_same_direction"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "tb_init_standby_idempotent"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "tb_config_reject"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "tb_out_of_range_safe_fault"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "tb_zero_high_impedance"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "encoder_signed_wrap"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "encoder_ambiguous_side"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "encoder_long_interval"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "encoder_reset_recovery"

  Invoke-Mutant -Name "tb_channel_swap" -SourcePath $tbSource `
    -Original @'
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, left_ccr);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, right_ccr);
'@ `
    -Replacement @'
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, right_ccr);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, left_ccr);
'@ `
    -TestName "tb_scaling_same_direction"

  Invoke-Mutant -Name "tb_pwm_sign" -SourcePath $tbSource `
    -Original @'
  HAL_GPIO_WritePin(AIN1_GPIO_Port, AIN1_Pin,
                    (left_direction > 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
'@ `
    -Replacement @'
  HAL_GPIO_WritePin(AIN1_GPIO_Port, AIN1_Pin,
                    (left_direction < 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
'@ `
    -TestName "tb_scaling_same_direction"

  Invoke-Mutant -Name "tb_full_scale_3599" -SourcePath $tbSource `
    -Original @'
  return ((magnitude * 3600u) + 500u) / 1000u;
'@ `
    -Replacement @'
  return ((magnitude * 3599u) + 500u) / 1000u;
'@ `
    -TestName "tb_scaling_same_direction"

  Invoke-Mutant -Name "tb_zero_command" -SourcePath $tbSource `
    -Original @'
  if (command == 0)
  {
    return 3600u;
  }
'@ `
    -Replacement @'
  if (command == 0)
  {
    return 0u;
  }
'@ `
    -TestName "tb_zero_high_impedance"

  Invoke-Mutant -Name "tb_out_of_range_no_stop" -SourcePath $tbSource `
    -Original @'
    s_state.rejected_requests++;
    sc_tb6612_enter_fault();
    s_state.last_status = SC_TB6612_ERR_ARGUMENT;
'@ `
    -Replacement @'
    s_state.rejected_requests++;
    s_state.enabled = 0u;
    s_state.mode = SC_TB6612_MODE_FAULT;
    s_state.last_status = SC_TB6612_ERR_ARGUMENT;
'@ `
    -TestName "tb_out_of_range_safe_fault"

  Invoke-Mutant -Name "tb_rollback_missing" -SourcePath $tbSource `
    -Original @'
    stop_status = HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_1);
'@ `
    -Replacement @'
    stop_status = HAL_OK;
'@ `
    -TestName "tb_second_channel_rollback"

  Invoke-Mutant -Name "tb_direction_order" -SourcePath $tbSource `
    -Original @'
  HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin,
                    GPIO_PIN_RESET);
  s_state.standby = 0u;
  sc_tb6612_set_direction(left_permille, right_permille);
  sc_tb6612_set_compare(left_ccr, right_ccr);
'@ `
    -Replacement @'
  s_state.standby = 0u;
  sc_tb6612_set_direction(left_permille, right_permille);
  sc_tb6612_set_compare(left_ccr, right_ccr);
  HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin,
                    GPIO_PIN_RESET);
'@ `
    -TestName "tb_direction_transition_order"

  Invoke-Mutant -Name "tb_init_missing_update" -SourcePath $tbSource `
    -Original @'
  return HAL_TIM_GenerateEvent(&htim4, TIM_EVENTSOURCE_UPDATE);
'@ `
    -Replacement @'
  return HAL_OK;
'@ `
    -TestName "tb_init_standby_idempotent"

  Invoke-Mutant -Name "tb_fault_missing_update" -SourcePath $tbSource `
    -Original @'
static void sc_tb6612_enter_fault(void)
{
  HAL_StatusTypeDef hal_status = sc_tb6612_load_safe_outputs();
'@ `
    -Replacement @'
static void sc_tb6612_enter_fault(void)
{
  HAL_StatusTypeDef hal_status;

  sc_tb6612_set_gpio_safe();
  sc_tb6612_set_compare(0u, 0u);
  hal_status = HAL_OK;
'@ `
    -TestName "tb_out_of_range_safe_fault"

  Invoke-Mutant -Name "tb_config_access_tim" -SourcePath $tbSource `
    -Original @'
  sc_tb6612_set_gpio_safe();
  status = sc_tb6612_validate_config();
'@ `
    -Replacement @'
  sc_tb6612_set_gpio_safe();
  sc_tb6612_set_compare(0u, 0u);
  status = sc_tb6612_validate_config();
'@ `
    -TestName "tb_config_reject"

  Invoke-Mutant -Name "tb_config_missing_gpio_safe" -SourcePath $tbSource `
    -Original @'
  sc_tb6612_set_gpio_safe();
  status = sc_tb6612_validate_config();
'@ `
    -Replacement @'
  status = sc_tb6612_validate_config();
'@ `
    -TestName "tb_config_reject"

  Invoke-Mutant -Name "tb_standby_clear_missing" -SourcePath $tbSource `
    -Original @'
    hal_status = sc_tb6612_load_safe_outputs();
    if (hal_status != HAL_OK)
'@ `
    -Replacement @'
    hal_status = HAL_OK;
    if (hal_status != HAL_OK)
'@ `
    -TestName "tb_set_standby_clear_sequence"

  Invoke-Mutant -Name "tb_standby_clear_skip_ain2" -SourcePath $tbSource `
    -Original @'
  HAL_GPIO_WritePin(AIN2_GPIO_Port, AIN2_Pin,
                    (left_direction < 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
'@ `
    -Replacement @'
  /* AIN2 write intentionally omitted for mutant isolation. */
'@ `
    -TestName "tb_set_standby_clear_sequence"

  Invoke-Mutant -Name "encoder_unsigned_wrap" -SourcePath $encoderSource `
    -Original @'
  return (int32_t)difference - 65536;
'@ `
    -Replacement @'
  return (int32_t)difference;
'@ `
    -TestName "encoder_signed_wrap"

  Invoke-Mutant -Name "encoder_ignore_ambiguous" `
    -SourcePath $encoderSource `
    -Original @'
  if (difference == 0x8000u)
'@ `
    -Replacement @'
  if (0)
'@ `
    -TestName "encoder_ambiguous_side"

  Invoke-Mutant -Name "encoder_ignore_long_interval" `
    -SourcePath $encoderSource `
    -Original @'
  force_lost =
    (uint8_t)((s_first_sample == 0u) &&
              (interval_ms > SC_ENCODER_MAX_INTERVAL_MS));
'@ `
    -Replacement @'
  force_lost = 0u;
'@ `
    -TestName "encoder_long_interval"

  Invoke-Mutant -Name "encoder_reset_not_hardware" `
    -SourcePath $encoderSource `
    -Original @'
  __HAL_TIM_SET_COUNTER(&htim2, 0u);
  __HAL_TIM_SET_COUNTER(&htim3, 0u);

  s_state.left.raw_count = 0u;
'@ `
    -Replacement @'
  s_state.left.raw_count = 0u;
'@ `
    -TestName "encoder_reset_recovery"

  Write-Host ("MUTANT SUMMARY: baseline passed; {0}/{0} mutants failed target tests" -f $mutantCount)
}
finally {
  $resolvedTempBase = (Resolve-Path ([IO.Path]::GetTempPath())).Path.TrimEnd("\")
  $resolvedMutationRoot =
    [IO.Path]::GetFullPath($temporaryRootResolved).TrimEnd("\")
  if (($resolvedMutationRoot.StartsWith(
        $resolvedTempBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationRoot).StartsWith(
        "smart-car-sc-motor-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationRoot -Recurse -Force
  }
  else {
    Write-Warning "temporary mutation directory was not removed: $resolvedMutationRoot"
  }

  $resolvedBuildBase = [IO.Path]::GetFullPath(
    (Join-Path $repoRoot "build")).TrimEnd("\")
  $resolvedMutationBuildRoot =
    [IO.Path]::GetFullPath($buildRootResolved).TrimEnd("\")
  if (($resolvedMutationBuildRoot.StartsWith(
        $resolvedBuildBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationBuildRoot).StartsWith(
        "sc_motor-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationBuildRoot -Recurse -Force
  }
  else {
    Write-Warning "mutation build directory was not removed: $resolvedMutationBuildRoot"
  }
}
