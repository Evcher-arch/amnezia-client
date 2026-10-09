$ErrorActionPreference = 'Stop'
# Load only the route-sync function. Never run the privileged sharing worker.
$source = Join-Path $PSScriptRoot '../service/server/sharing/Share.ps1'
$tokens = $null; $issues = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($source, [ref]$tokens, [ref]$issues)
if ($issues.Count) { throw ($issues | Out-String) }
$definition = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Sync-ShareVpnRoutes'
}, $true)
if (!$definition) { throw 'Route synchronizer missing' }
Invoke-Expression $definition.Extent.Text
$script:routes = @(
    [pscustomobject]@{ InterfaceIndex=10; DestinationPrefix='128.0.0.0/1'; Protocol='NetMgmt'; NextHop='0.0.0.0' },
    [pscustomobject]@{ InterfaceIndex=10; DestinationPrefix='10.33.0.2/32'; Protocol='Local'; NextHop='0.0.0.0' },
    [pscustomobject]@{ InterfaceIndex=10; DestinationPrefix='0.0.0.0/0'; Protocol='NetMgmt'; NextHop='0.0.0.0' }
)
$script:added = 0
function Get-NetRoute {
    [CmdletBinding()]param($InterfaceIndex, $AddressFamily, $DestinationPrefix)
    $script:routes | Where-Object {
        $_.InterfaceIndex -eq $InterfaceIndex -and
        (!$DestinationPrefix -or $_.DestinationPrefix -eq $DestinationPrefix)
    }
}
function New-NetRoute {
    [CmdletBinding()]param($InterfaceIndex, $DestinationPrefix, $NextHop, $RouteMetric, $PolicyStore)
    if ($InterfaceIndex -ne 20 -or $NextHop -ne '10.254.254.2' -or $PolicyStore -ne 'ActiveStore') {
        throw 'Unexpected route mutation'
    }
    $script:routes += [pscustomobject]@{InterfaceIndex=20;DestinationPrefix=$DestinationPrefix;Protocol='NetMgmt';NextHop=$NextHop}
    $script:added++
}
$owned = @(Sync-ShareVpnRoutes 10 20)
if ($script:added -ne 1 -or $owned -notcontains '128.0.0.0/1') { throw 'Initial mirroring failed' }
$script:routes += [pscustomobject]@{InterfaceIndex=10;DestinationPrefix='1.1.1.1/32';Protocol='NetMgmt';NextHop='0.0.0.0'}
$owned = @(Sync-ShareVpnRoutes 10 20)
if ($script:added -ne 2 -or $owned -notcontains '1.1.1.1/32') { throw 'Late DNS route was not mirrored/tracked for cleanup' }
$null = Sync-ShareVpnRoutes 10 20
if ($script:added -ne 2) { throw 'Repeated sync adds duplicate routes' }
$script:routes = @($script:routes | Where-Object { !($_.InterfaceIndex -eq 20 -and $_.DestinationPrefix -eq '1.1.1.1/32') })
$null = Sync-ShareVpnRoutes 10 20
if ($script:added -ne 3) { throw 'Deleted route was not repaired' }
($script:routes | Where-Object { $_.InterfaceIndex -eq 20 -and $_.DestinationPrefix -eq '1.1.1.1/32' }).NextHop = '10.254.254.99'
$conflict = $false
try { $null = Sync-ShareVpnRoutes 10 20 } catch { $conflict = $_.Exception.Message -like '*conflicting route*' }
if (!$conflict) { throw 'Foreign route was not protected' }
'PASS: initial/late/deleted routes, idempotency, cleanup ownership and conflict protection'
