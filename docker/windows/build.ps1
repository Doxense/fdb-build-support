param (
    [string]$ImageName = "fdb-windows",
    # By default we want to leave one CPU core for the OS so the user has some minimal control over the system
    [string]$Cpus = (Get-CimInstance -ClassName Win32_Processor -Filter "DeviceID='CPU0'").NumberOfLogicalProcessors - 2,
    # We want to leave at least 1GB of memory for the OS
    [string]$Memory = (Get-CimInstance -ClassName Win32_ComputerSystem).TotalPhysicalMemory - 2*[Math]::Pow(2, 30),
    [Parameter(Mandatory=$true)][string]$SourceDir,
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [switch]$DryRun = $false,
    [switch]$ForceConfigure = $false,
    [switch]$SkipDockerBuild = $false,
    [Parameter(Position=0)][string]$Target = "installer"
)

$SourceDir = Resolve-Path $SourceDir
# we don't want a trailing \ in the build dir
if ($SourceDir.EndsWith("\")) {
    $SourceDir = $SourceDir.Substring(0, $SourceDir.Length - 1)
}
$BuildDir = Resolve-Path $BuildDir
# we don't want a trailing \ in the build dir
if ($BuildDir.EndsWith("\")) {
    $BuildDir = $BuildDir.Substring(0, $BuildDir.Length - 1)
}

$exponent = 0
$Memory = $Memory.ToUpper()
if ($Memory.EndsWith("K")) {
    $exponent = 10
} elseif ($Memory.EndsWith("M")) {
    $exponent = 20
} elseif ($Memory.EndsWith("G")) {
    $exponent = 30
} elseif ($Memory.EndsWith("T")) {
    $exponent = 40
}
if ($exponent -gt 0) {
    $Memory = $Memory.Substring(0, $Memory.Length - 1) * [Math]::Pow(2, $exponent)
}

$buildCommand = [string]::Format("Get-Content .\devel\Dockerfile | docker build -t {1} -m {2} -", 
                                 $SourceDir, $ImageName, [Math]::Min(16 * [Math]::Pow(2, 30), $Memory))
if ($DryRun -and !$SkipDockerBuild) {
    Write-Output $buildCommand
} elseif (!$SkipDockerBuild) {
    Invoke-Expression -Command $buildCommand
}

# Write build instructions into file
$cmdFile = "docker_command.ps1"
$batFile = "$BuildDir\$cmdFile"
$batFileDocker = "C:\fdbbuild\$cmdFile"
# "C:\BuildTools\Common7\Tools\VsDevCmd.bat" | Out-File $batFile
"cd \fdbbuild" | Out-File -Append $batFile
if ($ForceConfigure -or ![System.IO.File]::Exists("$BuildDir\CMakeCache.txt") -or ($Target -eq "configure")) {
    "cmake -G ""Visual Studio 16 2019"" -A x64 -T""ClangCL"" -S C:\foundationdb -B C:\fdbbuild --debug-trycompile" | Out-File -Append $batFile
}
if ($Target -ne "configure") {
    "msbuild /p:CL_MPCount=$Cpus /m /p:UseMultiToolTask=true /p:Configuration=Release foundationdb.sln" | Out-File -Append $batFile
}

$dockerCommand = "powershell.exe -NoLogo -ExecutionPolicy Bypass -File $batFileDocker"
$runCommand = [string]::Format("docker run -v {0}:C:\foundationdb -v {1}:C:\fdbbuild --name fdb-build -m {2} --cpus={3} --rm {4} ""{5}""",
                                $SourceDir, $BuildDir, $Memory, $Cpus, $ImageName, $dockerCommand);
if ($DryRun) {
    Write-Output $runCommand
} else {
    Invoke-Expression $runCommand
}