<#
.SYNOPSIS
    Script de build para projeto Rainmeter com Visual Studio.
.DESCRIPTION
    Localiza a instalação mais recente do Visual Studio, carrega o ambiente de linha de comando e executa
    o MSBuild de acordo com o modo selecionado:
    - Build: compilação normal (incremental)
    - Rebuild: limpa e recompila tudo
    - Analysis: executa somente a análise de código (sem gerar DLL)
.PARAMETER SolutionPath
    Caminho completo do arquivo .sln do projeto C++.
.PARAMETER Configuration
    Configuração de build: Debug ou Release.
.PARAMETER Platform
    Plataforma de build: x64 ou x86.
.PARAMETER Target
    Modo de execução: Build, Rebuild ou Analysis.
#>

param(
    [Parameter(Mandatory=$true)]
    [string]$SolutionPath,

    [Parameter(Mandatory=$true)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration,

    [Parameter(Mandatory=$true)]
    [ValidateSet("x64", "x86")]
    [string]$Platform,

    [Parameter(Mandatory=$false)]
    [ValidateSet("Build", "Rebuild", "Analysis")]
    [string]$Target = "Build"
)

# Normaliza barras para o padrão Windows (evita problemas com "/")
$SolutionPath = $SolutionPath.Replace('/', '\')

# Vai para a pasta da solução (MSBuild espera estar no diretório correto)
$solutionDir = Split-Path -Parent $SolutionPath
Push-Location $solutionDir

# Localiza o Visual Studio via vswhere (presente em VS 2017+)
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vsWhere)) {
    Write-Error "vswhere.exe não encontrado. Verifique a instalação do Visual Studio."
    exit 1
}

$vsPath = & $vsWhere -latest -property installationPath
if (-not $vsPath) {
    Write-Error "Visual Studio não encontrado."
    exit 1
}

# Caminho do MSBuild (pesquisa nas versões Current e 15.0)
$msbuild = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
if (-not (Test-Path $msbuild)) {
    $msbuild = Join-Path $vsPath "MSBuild\15.0\Bin\MSBuild.exe"
}
if (-not (Test-Path $msbuild)) {
    Write-Error "MSBuild.exe não encontrado em $vsPath."
    exit 1
}

Write-Host "Usando MSBuild: $msbuild"

# Caminho do script de ambiente do VS (variáveis de ambiente, compiladores etc.)
$vsDevCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"

# Monta os argumentos base
$msbuildArgs = @(
    "`"$SolutionPath`"",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform"
)

# Ajusta target e propriedades conforme o modo escolhido
switch ($Target) {
    "Build" {
        Write-Host "Modo: Build (incremental)"
        # Em Build, o alvo padrão já é "Build" - não precisa adicionar
    }
    "Rebuild" {
        Write-Host "Modo: Rebuild (recompilação completa)"
        $msbuildArgs += "/t:Rebuild"
    }
    "Analysis" {
        Write-Host "Modo: Análise de código (somente diagnóstico)"
        $msbuildArgs += "/t:RunCodeAnalysis"
        $msbuildArgs += "/p:RunCodeAnalysis=true"
    }
}

# Constrói a linha de comando final
$argString = $msbuildArgs -join ' '

# Executa o build dentro do ambiente do Visual Studio
if (Test-Path $vsDevCmd) {
    Write-Host "Carregando ambiente do Visual Studio..."
    # A linha executa VsDevCmd.bat e em seguida o MSBuild com os argumentos
    $batchCmd = "`"$vsDevCmd`" && `"$msbuild`" $argString"
    cmd /c $batchCmd
    $exitCode = $LASTEXITCODE
}
else {
    Write-Host "VsDevCmd.bat não encontrado. Executando msbuild diretamente..."
    & $msbuild $SolutionPath $argString
    $exitCode = $LASTEXITCODE
}

Pop-Location

# Retorna o código de saída do MSBuild (para o VS Code exibir problemas)
exit $exitCode
