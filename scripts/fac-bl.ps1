# Docker wrapper for the F.A.C. V2 bootloader: build, host tests, shell, clean.
# Windows PowerShell 5.1 compatible.
param(
	[Parameter(Position = 0)][string]$Command = "",
	[Parameter(Position = 1)][string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"
$Image = "fac-bootloader-build:local"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Show-Usage {
	Write-Host "Usage: fac-bl.ps1 <command>"
	Write-Host "  image                    build the Docker image ($Image)"
	Write-Host "  build [Debug|Release]    build the firmware (default Release)"
	Write-Host "  test                     build and run the host unit tests"
	Write-Host "  shell                    interactive bash in the container"
	Write-Host "  clean                    remove build/ and tests/build"
}

function Build-Image {
	docker build -t $Image -f (Join-Path $Root "docker\Dockerfile") (Join-Path $Root "docker")
	if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Confirm-Image {
	# docker writes to stderr when the image is missing: do not turn that into an error
	$prev = $ErrorActionPreference
	$ErrorActionPreference = "Continue"
	docker image inspect $Image 2>&1 | Out-Null
	$missing = ($LASTEXITCODE -ne 0)
	$ErrorActionPreference = $prev
	if ($missing) { Build-Image }
}

function Invoke-InContainer {
	param([string[]]$DockerFlags, [string[]]$Cmd)
	$mount = "$($Root):/work"
	$all = @("run", "--rm") + $DockerFlags + @("-v", $mount, "-w", "/work", $Image) + $Cmd
	# CMake writes messages to stderr: with "Stop" a redirected stderr would abort the script
	$ErrorActionPreference = "Continue"
	& docker @all
	if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

switch ($Command) {
	"image" { Build-Image }
	"build" {
		if (($BuildType -ne "Debug") -and ($BuildType -ne "Release")) {
			Write-Host "Unknown build type: $BuildType (use Debug or Release)"
			exit 1
		}
		Confirm-Image
		Invoke-InContainer @() @("sh", "-c", "cmake --preset $BuildType && cmake --build build/$BuildType")
	}
	"test" {
		Confirm-Image
		Invoke-InContainer @() @("sh", "-c", "cmake -S tests -B tests/build -G Ninja && cmake --build tests/build && ctest --test-dir tests/build --output-on-failure")
	}
	"shell" {
		Confirm-Image
		Invoke-InContainer @("-it") @("bash")
	}
	"clean" {
		Confirm-Image
		Invoke-InContainer @() @("sh", "-c", "rm -rf build tests/build")
	}
	default {
		Show-Usage
		if ($Command -ne "") { exit 1 }
	}
}
