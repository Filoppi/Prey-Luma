#Requires -Version 7.1

try {
    $repo = Resolve-Path $PSScriptRoot/../
    Push-Location $repo

    # test-path
    $manifests = @(
        "$repo/Plugin/CMakeLists.txt",
        "$repo/Plugin/cmake/Plugin.h.in",
        "$repo/Plugin/vcpkg.json",
        "$repo/.github/TEMPLATE",
        "$repo/.github/workflows/bootstrap.yml",
        # duh
        "$($MyInvocation.MyCommand.Path)"
    )

    Write-Host "Checking files..."
    foreach ($file in $manifests) {
        if (!(Test-Path $file)) {
            throw "File not found : {$file}"
        }
    }
    Write-Host "...Ok, all files present"


    # parse repo
    $author = $($env:REPO).Split('/')[0]
    $project = $($env:REPO).Split('/')[1]
    Write-Host "AUTHOR: $author"
    Write-Host "PROJECT: $project"

    
    # update CMakeLists.txt
    Write-Host "Updating $($manifests[0])"
    $cmake = [IO.File]::ReadAllText($manifests[0])
    $cmake = $cmake -replace "(?<=project\(\s+\s+).*?(?=\n\s+VERSION)", "$project"
    [IO.File]::WriteAllText($manifests[0], $cmake)
    Write-Host "...Ok"


    # update Plugin.h
    Write-Host "Updating $($manifests[1])"
    $plugin = [IO.File]::ReadAllText($manifests[1])
    $plugin = $plugin -replace "@PluginAuthor@", $author
    [IO.File]::WriteAllText($manifests[1], $plugin)
    Write-Host "...Ok"


    # update vcpkg.json
    Write-Host "Updating $($manifests[2])"
    $vcpkg = [IO.File]::ReadAllText($manifests[2]) | ConvertFrom-Json
    $vcpkg.name = $project.ToLower() -replace "_", "-"
    $vcpkg.homepage = "https://github.com/$($env:REPO)"
    $vcpkg = $vcpkg | ConvertTo-Json -Depth 9 | ForEach-Object { $_ -replace "(?m)  (?<=^(?:  )*)", "    " }
    [IO.File]::WriteAllText($manifests[2], $vcpkg)
    Write-Host "...Ok"


    # remove bootstrappers
    Write-Host "Removing bootstrap files..."
    foreach ($file in $manifests[3..5]) {
        Remove-Item $file -Force -ErrorAction:SilentlyContinue -Confirm:$false | Out-Null
    }
    Write-Host "...Ok"

    Write-Output "SETUP_SUCCESS=true" >> $env:GITHUB_OUTPUT
}
catch {
    Write-Error "...Failed: $_"
    Write-Output "SETUP_SUCCESS=false" >> $env:GITHUB_OUTPUT
}
finally {
    Pop-Location
    exit
}
