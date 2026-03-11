<#
	---------------------------------------------------------------------------
	RainJIT Post-Build Script
	---------------------------------------------------------------------------

	This script is intended to be executed as a post-build step.
	It performs the following operations:

	1. Loads environment variables from a .env file
	2. Validates required environment variables and paths
	3. Stops the Rainmeter process safely
	4. Copies the built RainJIT.dll into the Rainmeter plugin directory
	5. Restarts Rainmeter
	6. Sends a command to open the Rainmeter About / Log window

	The script is designed to be robust, defensive, and CI/CD-friendly.
#>

param (
	# Full path to the compiled RainJIT.dll produced by the build system
	[Parameter(Mandatory = $true)]
	[string]$BuildFile
)



# Resolve the absolute path to the scripts directory
# $workdir = Resolve-Path "$PSScriptRoot/../../assets/scripts/"

# Import helpers script
. "$PSScriptRoot/dotenv.ps1"

# Load environment variables from .env file
[dotenv]::read("$PSScriptRoot/.env")



# Validate required environment variables and paths
if (
	-not $env:PLUGIN_PATH -or
	-not $env:RAINMETER_PATH -or
	-not (Test-Path $env:PLUGIN_PATH) -or
	-not (Test-Path (Join-Path $env:RAINMETER_PATH "Rainmeter.exe"))
) {
	Write-Host "`n[RainJIT Post-Build] -> Invalid or missing environment variables."
	Write-Host "[RainJIT Post-Build] -> Ensure PLUGIN_PATH and RAINMETER_PATH are correctly defined.`n"
	exit
}



Write-Host ""
Write-Host "`n[RainJIT Post-Build]"

$hasError = $false


Write-Host "[RainJIT Post-Build] -> Stopping Rainmeter process..."

# Attempt to stop Rainmeter if it is currently running
Get-Process -Name "Rainmeter" -ErrorAction SilentlyContinue | Stop-Process -Force

# Wait until the process fully exits, with a timeout to avoid infinite loops
$timeout = 10
while (
	(Get-Process -Name "Rainmeter" -ErrorAction SilentlyContinue) -and
	$timeout-- -gt 0
) {
	Start-Sleep -Seconds 1
}

if ($timeout -le 0) {
	Write-Host "[RainJIT Post-Build] -> Timeout while waiting for Rainmeter to close.`n"
	exit
}


# Verify that the build artifact exists
if (Test-Path $BuildFile) {

	Write-Host "[RainJIT Post-Build] -> Copying RainJIT.dll to Rainmeter plugin directory..."

	Copy-Item `
		-Path $BuildFile `
		-Destination (Join-Path $env:PLUGIN_PATH "RainJIT.dll") `
		-Force

	Write-Host "[RainJIT Post-Build] -> Plugin successfully copied."
}

else {
	$hasError = $true
	Write-Host "[RainJIT Post-Build] -> Build file not found:"
	Write-Host "[RainJIT Post-Build] -> $BuildFile"
}



Write-Host "[RainJIT Post-Build] -> Starting Rainmeter..."

# Start Rainmeter normally
Start-Process (Join-Path $env:RAINMETER_PATH "Rainmeter.exe")


# Wait until Rainmeter is responsive
do {
	Start-Sleep -Milliseconds 500
	$process = Get-Process -Name "Rainmeter" -ErrorAction SilentlyContinue
} while (-not $process -or -not $process.Responding)

# Small additional delay to ensure internal readiness
Start-Sleep -Seconds 2


# Send command to the running Rainmeter instance (does NOT start a new instance)
Start-Process `
	(Join-Path $env:RAINMETER_PATH "Rainmeter.exe") `
	-ArgumentList "!About"

Write-Host "[RainJIT Post-Build] -> Rainmeter started and log window opened."



# Emit an audible warning if any non-fatal error occurred
if ($hasError) {
	[System.Media.SystemSounds]::Exclamation.Play()
}



Write-Host "[RainJIT Post-Build]`n"
