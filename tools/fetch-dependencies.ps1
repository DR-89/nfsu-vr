$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$dependencies = Get-Content -LiteralPath (Join-Path $repo 'quest/dependencies.json') -Raw | ConvertFrom-Json
foreach ($property in $dependencies.PSObject.Properties) {
    $dep = $property.Value
    $destination = Join-Path $repo "build/deps/$($property.Name)"
    if (-not (Test-Path -LiteralPath $destination)) {
        & git clone --depth 1 --branch $dep.tag $dep.url $destination
        if ($LASTEXITCODE) { throw "Dependency clone failed: $($property.Name)" }
    }
    $actual = & git -C $destination rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $actual -ne $dep.revision) { throw "Dependency revision mismatch: $destination" }
    $changes = & git -C $destination status --porcelain
    if ($LASTEXITCODE -ne 0 -or $changes) { throw "Dependency checkout has changes: $destination" }
}
