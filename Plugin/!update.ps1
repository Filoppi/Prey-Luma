#Requires -Version 5

# args
param (
    [Parameter(Mandatory)][ValidateSet('SOURCEGEN', 'DISTRIBUTE')][string]$Mode = 'SOURCEGEN',
    [string]$Version,
    [string]$Path,
    [string]$Payload
)


$ErrorActionPreference = "Stop"

$Folder = $PSScriptRoot | Split-Path -Leaf
$SourceExt = @('.asm', '.c', '.cc', '.cpp', '.cxx', '.h', '.hpp', '.hxx', 'inc', '.inl', '.ixx')
$ConfigExt = @('.ini', '.json', '.toml', '.xml')
$DocsExt = @('.md')

function Normalize-Path {
    param (
        [string]$in
    )
    
    $out = $in -replace '\\', '/'
    while ($out.Contains('//')) {
        $out = $out -replace '//', '/'
    }
    return $out
}

function Resolve-Files {
    param (
        [Parameter(ValueFromPipeline)][string]$parent = $PSScriptRoot,
        [string[]]$range = @('include', 'src', 'test')
    )
    
    process {
        Push-Location $PSScriptRoot
        $_generated = [System.Collections.ArrayList]::new()

        try {
            foreach ($directory in $range) {
                if (!$env:RebuildInvoke) {
                    Write-Host "`t[$parent/$directory]"
                }

                Get-ChildItem "$parent/$directory" -Recurse -File -ErrorAction SilentlyContinue | Where-Object {
                    ($_.Extension -in ($SourceExt + $DocsExt)) -and 
                    ($_.Name -notmatch 'Plugin.h|Version.h')
                } | Resolve-Path -Relative | ForEach-Object {
                    if (!$env:RebuildInvoke) {
                        Write-Host "`t`t<$_>"
                    }
                    $_generated.Add("`n`t`"$(Normalize-Path $_.Substring(2))`"") | Out-Null
                }
            }               
            
            Get-ChildItem "$parent/dist" -Exclude "rules" | Get-ChildItem -File -Recurse -ErrorAction SilentlyContinue | Where-Object {
                ($_.Extension -in ($ConfigExt + $DocsExt)) -and 
                ($_.Name -notmatch 'cmake|vcpkg')
            } | Resolve-Path -Relative | ForEach-Object {
                if (!$env:RebuildInvoke) {
                    Write-Host "`t`t<$_>"
                }
                $_generated.Add("`n`t`"$(Normalize-Path $_.Substring(2))`"") | Out-Null
            }
        }
        finally {
            Pop-Location
        }

        return $_generated
    }
}


Write-Host "`n`t<$Folder> [$Mode]"


# @@SOURCEGEN
if ($Mode.ToUpper() -eq 'SOURCEGEN') {
    Write-Host "`tGenerating CMake sourcelist..."
    Remove-Item "$Path/sourcelist.cmake" -Force -Confirm:$false -ErrorAction Ignore

    $generated = 'set(SOURCES'
    $generated += $PSScriptRoot | Resolve-Files
    if ($Path) {
        $generated += $Path | Resolve-Files
    }
    $generated += "`n)"
    [IO.File]::WriteAllText("$Path/sourcelist.cmake", $generated)
}

$RuleVarTbl = @{ 
    config          = 'debug';
    cmake_output    = Normalize-Path ($Path + '/');
    dist            = Normalize-Path "$PSScriptRoot/dist/";
    project_name    = $Payload;
    project_version = $Version;
}
$RuleCmds = @()

function Resolve-RuleVar {
    param (
        [string]$Path
    )

    $Path = $Path.Trim(' ')
    $any = [regex]::Matches($Path, '\{.*?\}').Value
    $Resolved = $Path

    # env:
    foreach ($unset in $any) {
        $inner = $unset.Trim('{', '}')
        if ($inner.StartsWith('env:')) {
            $Resolved = $Resolved -replace $unset, [System.Environment]::GetEnvironmentVariable($inner.TrimStart('env:'))
        }
        else {
            $Resolved = if ($script:RuleVarTbl.Contains($inner)) {
                $Resolved -replace $unset, $script:RuleVarTbl[$inner]
            }
            else {
                $Resolved -replace $unset, $inner
            }
        }
    }

    return Normalize-Path $Resolved.Trim('{', '}')
}

function Resolve-Rules {
    param (
        [Object]$Deployee
    )

    switch ($Deployee.action) {
        "base" {
            $script:RuleVarTbl[($Deployee.params[0].Trim('{', '}'))] = Resolve-RuleVar $Deployee.params[1]
            #"setting $($Deployee.params[0]) to $(Resolve-RuleVar $Deployee.params[1])"
            break
        }
        "copy" {
            $source = $(Resolve-RuleVar $Deployee.params[0])
            $destination = $(Resolve-RuleVar $Deployee.params[1])
            $dest_path = $destination.Substring(0, $destination.LastIndexOf('/') + 1)
            $script:RuleCmds += "New-Item `'$dest_path`' -ItemType Directory -Force -ErrorAction:SilentlyContinue"
            $script:RuleCmds += "Copy-Item `'$source`' `'$destination`' -Force -Recurse -ErrorAction:SilentlyContinue"
            break
        }
        "copy_if" {
            $source = $(Resolve-RuleVar $Deployee.params[0])
            $destination = $(Resolve-RuleVar $Deployee.params[1])
            $dest_path = $destination.Substring(0, $destination.LastIndexOf('/') + 1)
            $script:RuleCmds += "if (Test-Path `'$source`') {"
            $script:RuleCmds += "New-Item `'$dest_path`' -ItemType Directory -Force -ErrorAction:SilentlyContinue"
            $script:RuleCmds += "Copy-Item `'$source`' `'$destination`' -Force -Recurse -ErrorAction:SilentlyContinue }"
            break
        }
        "package" {
            $source_tree = @()
            foreach ($source in $Deployee.params[0..($Deployee.params.Length - 2)]) {
                $source_tree += , "`'$(Resolve-RuleVar $source)`'"
            }
            $script:RuleCmds += "Compress-Archive -Path $($source_tree -join ',') -DestinationPath `'$(Resolve-RuleVar $Deployee.params[-1])`' -Force -ErrorAction:SilentlyContinue"
            break
        }
        "remove" {
            foreach ($to_remove in $Deployee.params) {
                $script:RuleCmds += "Remove-Item `'$(Resolve-RuleVar $to_remove)`' -Force -Recurse -ErrorAction:SilentlyContinue"
            }
            break
        }
        "script" {
            foreach ($raw_script in $Deployee.params) {
                $script:RuleCmds += $raw_script
            }
            break
        }
        default {
            break
        }
    }
}


# @@DISTRIBUTE
if ($Mode.ToUpper() -eq 'DISTRIBUTE') {
    if ($Version.ToUpper() -eq '-1') {
        # update script to every project
        Get-ChildItem "$PSScriptRoot/$Path" -Directory -Recurse | Where-Object {
            $_.Name -notin @('vcpkg', 'build', '.git', '.vs') -and
            (Test-Path "$_/CMakeLists.txt" -PathType Leaf) -and
            (Test-Path "$_/vcpkg.json" -PathType Leaf)
        } | ForEach-Object {
            Write-Host "`tUpdated <$_>"
            Robocopy.exe "$PSScriptRoot" "$_" '!Update.ps1' /MT /NJS /NFL /NDL /NJH | Out-Null
        }
        exit
    }

    $Path = Normalize-Path $Path
    $RuleVarTbl.config = $Path.Split('/')[-1].ToLower()

    $rules = Get-ChildItem "$($RuleVarTbl.dist)/rules" -File *.json
    $rule_timestamp = @( "!update : $((Get-Item $MyInvocation.MyCommand.Path).LastWriteTime.Ticks)" )

    # generate timestamp & distribution step
    foreach ($rule in $rules) {
        $rule_timestamp += , "$($rule.BaseName) : $($rule.LastWriteTime.Ticks)"
    }

    $regenerate = $true
    $timestamp = "$($RuleVarTbl.dist)/rules/$($RuleVarTbl.project_version).timestamp"
    Write-Host "`tChecking deploy rules..."

    if (Test-Path "$($RuleVarTbl.dist)/deploy.ps1") {
        $rule_timestamp += , "deployer : $((Get-Item "$($RuleVarTbl.dist)/deploy.ps1").LastWriteTime.Ticks)"
    }

    if (Test-Path $timestamp) {
        $old_timestamp = [IO.File]::ReadAllText($timestamp)

        if ($old_timestamp -eq ($rule_timestamp | Out-String)) {
            $regenerate = $false
            Write-Host "`t...No changes"
        }
        else {
            Write-Host "`t...Pending changes"
        }
    }

    # parse all rules
    if ($regenerate) {
        Write-Host "`tRegenerating deploy rules..."

        foreach ($rule in $rules) {
            $deployer = [IO.File]::ReadAllText($rule.FullName) | ConvertFrom-Json

            foreach ($deployee in $deployer) {
                if (("config" -in $deployee.PSObject.Properties.Name) -and ($deployee.config -ne $RuleVarTbl.config)) {
                    continue
                }
                else {
                    Resolve-Rules $deployee
                }
            }
        }

        $RuleCmds | Out-File "$($RuleVarTbl.dist)/deploy-$($RuleVarTbl.config.ToLower()).ps1" utf8

        $rule_timestamp[-1] = "deployer : $((Get-Item "$($RuleVarTbl.dist)/deploy-$($RuleVarTbl.config.ToLower()).ps1").LastWriteTime.Ticks)"
        Remove-Item "$($RuleVarTbl.dist)/rules/*.timestamp" -Force -ErrorAction:SilentlyContinue | Out-Null
        $rule_timestamp | Out-File $timestamp utf8

        Write-Host "`t...Ok"
    }
    
    # deploy
    Write-Host "`tExecuting deploy rules..."
    & "$($RuleVarTbl.dist)/deploy-$($RuleVarTbl.config.ToLower()).ps1"

    Write-Host "`t...Ok"
}
