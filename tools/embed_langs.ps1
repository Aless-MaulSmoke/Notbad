# Gera res\lang_embedded.rc com uma linha NBLANG_<TAG> RCDATA por JSON
# encontrado em lang\ — a lista e derivada dos arquivos presentes no momento
# da build; nada e enumerado na mao. Nome do recurso: "pt-BR" -> NBLANG_PT_BR.
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

$langDir   = Join-Path $Root 'lang'
$outFile   = Join-Path $Root 'res\lang_embedded.rc'
$jsonFiles = @(Get-ChildItem -LiteralPath $langDir -Filter '*.json' -File | Sort-Object Name)

$seen    = @{}
$lines   = New-Object System.Collections.Generic.List[string]
$lines.Add('// Gerado automaticamente por tools\embed_langs.ps1. Nao edite este arquivo.')
$lines.Add('// Nome do recurso = nome do arquivo sem .json, maiusculas, "-" -> "_".')

foreach ($f in $jsonFiles) {
    $tag  = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
    $res  = 'NBLANG_' + ($tag.ToUpperInvariant() -replace '-', '_')

    if ($seen.ContainsKey($res)) {
        throw "lang: arquivos que mapeiam para o mesmo recurso ($res): '$($seen[$res])' e '$($f.Name)'"
    }
    $seen[$res] = $f.Name

    $lines.Add(('{0} RCDATA "../lang/{1}"' -f $res, $f.Name)) | Out-Null
}

if ($lines.Count -le 2) {
    $lines.Add('// (nenhum JSON em lang/)') | Out-Null
}

$content = ($lines -join "`r`n") + "`r`n"
$old = $null
if ([System.IO.File]::Exists($outFile)) {
    $old = [System.IO.File]::ReadAllText($outFile)
}

if ($old -ne $content) {
    [System.IO.File]::WriteAllText($outFile, $content, [System.Text.Encoding]::ASCII)
    Write-Host ("embed_langs: {0} idioma(s) embutido(s)" -f $jsonFiles.Count)
}
