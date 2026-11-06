param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Version
)

if (-not (Test-Path -LiteralPath $Source)) {
    exit 1
}

$dir = Split-Path -Parent $Source
$leaf = 'TMX' + [char]0x8C03 + [char]0x8BD5 + [char]0x52A9 + [char]0x624B + ' ' + $Version + '.exe'
Copy-Item -LiteralPath $Source -Destination (Join-Path $dir $leaf) -Force
