[CmdletBinding()]
param(
  [string]$Compiler = "C:/mingw64/bin/gcc.exe",
  [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$testSource = Join-Path $repoRoot "tests/sc_i2c2"
$i2cSource = Join-Path $repoRoot "Core/Src/sc_i2c2.c"
$mpuSource = Join-Path $repoRoot "Core/Src/sc_mpu6050.c"
$oledSource = Join-Path $repoRoot "Core/Src/sc_ssd1306.c"
$runId = [guid]::NewGuid().ToString("N")
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
  ("smart-car-sc-i2c2-mutants-" + $runId)
$temporaryRootResolved =
  (New-Item -ItemType Directory -Path $temporaryRoot).FullName
$buildRoot = Join-Path $repoRoot ("build/sc_i2c2-mutants-" + $runId)
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
    [Parameter(Mandatory = $true)][string]$I2cPath,
    [Parameter(Mandatory = $true)][string]$MpuPath,
    [Parameter(Mandatory = $true)][string]$OledPath
  )

  $buildDirectory = Join-Path $buildRootResolved $Name
  Invoke-Native "cmake" -S $testSource -B $buildDirectory `
    -G $Generator -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_C_COMPILER=$Compiler" `
    "-DSC_I2C2_SOURCE=$I2cPath" `
    "-DSC_MPU6050_SOURCE=$MpuPath" `
    "-DSC_SSD1306_SOURCE=$OledPath"
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
  $executable = Join-Path $BuildDirectory "sc_i2c2_tests.exe"
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
    [Parameter(Mandatory = $true)][string]$Replacement,
    [string]$SecondOriginal = "",
    [string]$SecondReplacement = ""
  )

  $utf8 = [Text.UTF8Encoding]::new($false)
  $sourceText = [IO.File]::ReadAllText($SourcePath, $utf8)
  if (-not $sourceText.Contains($Original)) {
    throw "mutation anchor not found for $Name"
  }

  $mutatedText = $sourceText.Replace($Original, $Replacement)
  if ($SecondOriginal.Length -ne 0) {
    if (-not $mutatedText.Contains($SecondOriginal)) {
      throw "second mutation anchor not found for $Name"
    }
    $mutatedText =
      $mutatedText.Replace($SecondOriginal, $SecondReplacement)
  }
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
    [string]$SecondOriginal = "",
    [string]$SecondReplacement = "",
    [Parameter(Mandatory = $true)][string]$TestName
  )

  $mutant = New-MutantSource -Name $Name -SourcePath $SourcePath `
    -Original $Original -Replacement $Replacement `
    -SecondOriginal $SecondOriginal `
    -SecondReplacement $SecondReplacement
  if ($SourcePath -eq $i2cSource) {
    $build = Build-TestExecutable -Name $Name -I2cPath $mutant `
      -MpuPath $mpuSource -OledPath $oledSource
  }
  elseif ($SourcePath -eq $mpuSource) {
    $build = Build-TestExecutable -Name $Name -I2cPath $i2cSource `
      -MpuPath $mutant -OledPath $oledSource
  }
  else {
    $build = Build-TestExecutable -Name $Name -I2cPath $i2cSource `
      -MpuPath $mpuSource -OledPath $mutant
  }
  Invoke-ExpectedFailure -BuildDirectory $build -TestName $TestName
  $script:mutantCount++
}

try {
  Write-Host "MUTANT SOURCE DIR: $temporaryRootResolved"
  Write-Host "MUTANT BUILD DIR: $buildRootResolved"

  $baselineBuild = Build-TestExecutable -Name "baseline" `
    -I2cPath $i2cSource -MpuPath $mpuSource -OledPath $oledSource
  Invoke-Native "ctest" --test-dir $baselineBuild --output-on-failure

  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "oled_init_chunks"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "blocking_busy_settle"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "mpu_bias_api"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "mpu_calibration_completion_race"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "mpu_decode_and_failure"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "mpu_calibration"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "oled_generation_protection"
  Invoke-ExpectedPass -BuildDirectory $baselineBuild `
    -TestName "i2c_single_transaction"

  Invoke-Mutant -Name "oled_address_shift" -SourcePath $i2cSource `
    -Original @'
  hal_status = HAL_I2C_Master_Transmit_IT(&hi2c2,
                                          (uint16_t)(address << 1u),
                                          data, len);
'@ `
    -Replacement @'
  hal_status = HAL_I2C_Master_Transmit_IT(&hi2c2,
                                          (uint16_t)(address),
                                          data, len);
'@ `
    -TestName "oled_init_chunks"

  Invoke-Mutant -Name "mpu_byte_order" -SourcePath $mpuSource `
    -Original @'
  return (int16_t)(((uint16_t)data[0] << 8u) | (uint16_t)data[1]);
'@ `
    -Replacement @'
  return (int16_t)(((uint16_t)data[1] << 8u) | (uint16_t)data[0]);
'@ `
    -TestName "mpu_decode_and_failure"

  Invoke-Mutant -Name "mpu_wrong_scale" -SourcePath $mpuSource `
    -Original @'
  return ((int32_t)raw * 2000) / SC_MPU6050_GYRO_SCALE_DEN;
'@ `
    -Replacement @'
  return ((int32_t)raw * 1000) / SC_MPU6050_GYRO_SCALE_DEN;
'@ `
    -TestName "mpu_decode_and_failure"

  Invoke-Mutant -Name "mpu_early_bias" -SourcePath $mpuSource `
    -Original @'
  if ((s_calibration.active == 0u) ||
      (s_calibration.completed < s_calibration.target))
'@ `
    -Replacement @'
  if ((s_calibration.active == 0u) || (0))
'@ `
    -TestName "mpu_calibration"

  Invoke-Mutant -Name "mpu_failure_stays_valid" -SourcePath $mpuSource `
    -Original @'
  s_latest.valid = 0u;
  s_last_status = status;
'@ `
    -Replacement @'
  s_latest.valid = 1u;
  s_last_status = status;
'@ `
    -TestName "mpu_decode_and_failure"

  Invoke-Mutant -Name "oled_early_clean" -SourcePath $oledSource `
    -Original @'
  if (s_active_generation == s_page_generation[s_active_page])
'@ `
    -Replacement @'
  if (1)
'@ `
    -TestName "oled_generation_protection"

  Invoke-Mutant -Name "i2c_parallel_start" -SourcePath $i2cSource `
    -Original @'
static sc_i2c2_status_t sc_i2c2_check_common_ready(void)
{
  sc_i2c2_status_t status;

  if (s_initialized == 0u)
  {
    return SC_I2C2_ERR_NOT_READY;
  }

  status = sc_i2c2_validate_config();
  if (status != SC_I2C2_OK)
  {
    return status;
  }
  if (hi2c2.State != HAL_I2C_STATE_READY)
  {
    if (((uint32_t)hi2c2.State & 0x0Fu) != 0u)
    {
      return SC_I2C2_ERR_BUSY;
    }
    return SC_I2C2_ERR_NOT_READY;
  }
  if (s_inflight != 0u)
  {
    return SC_I2C2_ERR_BUSY;
  }
  return SC_I2C2_OK;
}
'@ `
    -Replacement @'
static sc_i2c2_status_t sc_i2c2_check_common_ready(void)
{
  return SC_I2C2_OK;
}
'@ `
    -SecondOriginal @'
  if ((s_inflight != 0u) ||
      (__HAL_I2C_GET_FLAG(&hi2c2, I2C_FLAG_BUSY) != RESET))
  {
    sc_i2c2_exit_critical(primask);
    return SC_I2C2_ERR_BUSY;
  }
'@ `
    -SecondReplacement @'
  if (0)
  {
    sc_i2c2_exit_critical(primask);
    return SC_I2C2_ERR_BUSY;
  }
'@ `
    -TestName "i2c_single_transaction"

  Invoke-Mutant -Name "blocking_prechecks_busy" -SourcePath $i2cSource `
    -Original @'
  if (s_inflight != 0u)
  {
    return SC_I2C2_ERR_BUSY;
  }
  return SC_I2C2_OK;
}
'@ `
    -Replacement @'
  if (s_inflight != 0u)
  {
    return SC_I2C2_ERR_BUSY;
  }
  if (__HAL_I2C_GET_FLAG(&hi2c2, I2C_FLAG_BUSY) != RESET)
  {
    return SC_I2C2_ERR_BUSY;
  }
  return SC_I2C2_OK;
}
'@ `
    -TestName "blocking_busy_settle"

  Invoke-Mutant -Name "mpu_bias_ignores_not_ready" -SourcePath $mpuSource `
    -Original @'
  if (s_bias.valid == 0u)
  {
    return SC_MPU6050_ERR_NOT_READY;
  }

  *bias_mdps = s_bias.gyro_z_mdps;
'@ `
    -Replacement @'
  if (0)
  {
    return SC_MPU6050_ERR_NOT_READY;
  }

  *bias_mdps = s_bias.gyro_z_mdps;
'@ `
    -TestName "mpu_bias_api"

  Invoke-Mutant -Name "mpu_calibration_ignores_completed" `
    -SourcePath $mpuSource `
    -Original @'
  completed = s_read_done;
'@ `
    -Replacement @'
  completed = 0u;
'@ `
    -TestName "mpu_calibration_completion_race"

  Invoke-Mutant -Name "oled_data_offset_zero" -SourcePath $oledSource `
    -Original @'
  uint16_t offset =
    (uint16_t)(s_active_chunk * SC_SSD1306_CHUNK);
'@ `
    -Replacement @'
  uint16_t offset = (uint16_t)0u;
'@ `
    -TestName "oled_init_chunks"

  Invoke-Mutant -Name "mpu_calibration_first_sample" `
    -SourcePath $mpuSource `
    -Original @'
    s_calibration.gyro_z_sum_mdps += s_latest.gyro_mdps[2];
'@ `
    -Replacement @'
    if (s_calibration.completed == 0u)
    {
      s_calibration.gyro_z_sum_mdps =
        (int64_t)s_latest.gyro_mdps[2] * s_calibration.target;
    }
'@ `
    -TestName "mpu_calibration"

  Invoke-Mutant -Name "mpu_calibration_last_sample" `
    -SourcePath $mpuSource `
    -Original @'
    s_calibration.gyro_z_sum_mdps += s_latest.gyro_mdps[2];
'@ `
    -Replacement @'
    if (s_calibration.completed ==
        (uint16_t)(s_calibration.target - 1u))
    {
      s_calibration.gyro_z_sum_mdps =
        (int64_t)s_latest.gyro_mdps[2] * s_calibration.target;
    }
'@ `
    -TestName "mpu_calibration"

  Write-Host ("MUTANT SUMMARY: baseline passed; {0}/{0} mutants failed target tests" -f $mutantCount)
}
finally {
  $resolvedTempBase = (Resolve-Path ([IO.Path]::GetTempPath())).Path.TrimEnd("\")
  $resolvedMutationRoot =
    [IO.Path]::GetFullPath($temporaryRootResolved).TrimEnd("\")
  if (($resolvedMutationRoot.StartsWith(
        $resolvedTempBase + "\", [StringComparison]::OrdinalIgnoreCase)) -and
      ([IO.Path]::GetFileName($resolvedMutationRoot).StartsWith(
        "smart-car-sc-i2c2-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
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
        "sc_i2c2-mutants-", [StringComparison]::OrdinalIgnoreCase))) {
    Remove-Item -LiteralPath $resolvedMutationBuildRoot -Recurse -Force
  }
  else {
    Write-Warning "mutation build directory was not removed: $resolvedMutationBuildRoot"
  }
}
