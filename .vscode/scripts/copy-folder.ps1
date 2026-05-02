

param (
	[Parameter(Mandatory = $true)]
	[string]$type
)

# Import helpers script
. "$PSScriptRoot/dotenv.ps1"

# Load environment variables from .env file
[dotenv]::read("$PSScriptRoot/.env")


if ( $type -eq "Rain2Skins" ) {
	$DestinationFolder = "$env:SKINS_PATH\Rain"
	$SourceFolder = ( Resolve-Path "$PSScriptRoot/../../Skins" )
}

elseif ( $type -eq "Rain2Modules" ) {
	$DestinationFolder = "$env:SKINS_PATH\@Vault\Lua"
	$SourceFolder = ( Resolve-Path "$PSScriptRoot/../../Lua modules" )
}


# Cria o destino se não existir
if ( -not (Test-Path $DestinationFolder )) {
	New-Item -ItemType Directory -Path $DestinationFolder | Out-Null
}

# Copia apenas o CONTEÚDO da pasta Skins
robocopy "$SourceFolder" "$DestinationFolder" /E /R:2 /W:2

Write-Host "Conteúdo da pasta copiado com sucesso."
