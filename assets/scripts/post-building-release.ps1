

param (
	# Full path to the compiled RainJIT.dll produced by the build system
	[Parameter(Mandatory = $true)]
	[string]$BuildFile,
	[Parameter(Mandatory = $true)]
	[string]$BuildArch,
	[Parameter(Mandatory = $true)]
	[string]$Projec
)


$distDir = Resolve-Path "$PSScriptRoot/../../dist/$BuildArch"


Write-Host ""
Write-Host "`n[$Projec Post-Build]"

# Verify that the build artifact exists
if ( Test-Path $BuildFile ) {
	Write-Host "[$Projec Post-Build] -> Copying $Projec.dll to Dist directory..."

	Copy-Item `
		-Path $BuildFile `
		-Destination (Join-Path $distDir "$Projec.dll") `
		-Force

	Write-Host "[$Projec Post-Build] -> Plugin successfully copied."
}

else {
	$hasError = $true
	Write-Host "[$Projec Post-Build] -> Build file not found:"
	Write-Host "[$Projec Post-Build] -> $BuildFile"
}


Write-Host "[$Projec Post-Build]`n"
