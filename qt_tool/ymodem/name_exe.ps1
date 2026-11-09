param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Version
)

if (-not (Test-Path -LiteralPath $Source)) {
    exit 1
}

$dir = Split-Path -Parent $Source
$leaf = [char]0x5730 + [char]0x6728 + [char]0x5347 + [char]0x8C03 + [char]0x8BD5 + [char]0x52A9 + [char]0x624B + [char]0x751F + [char]0x4EA7 + [char]0x7248 + [char]0x672C + 'V' + $Version + '.exe'
Copy-Item -LiteralPath $Source -Destination (Join-Path $dir $leaf) -Force
