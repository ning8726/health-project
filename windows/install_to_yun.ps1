param(
    [string]$YunHost = "192.168.240.1"
)

$ErrorActionPreference = "Stop"
if ($YunHost -notmatch '^[A-Za-z0-9.:-]+$') {
    throw "Use only the Yun IP address or hostname."
}

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Remote = "root@$YunHost"
$InstallDirectory = "/tmp/workout-install"
$SshOptions = @(
    "-o", "ConnectTimeout=15",
    "-o", "KexAlgorithms=+diffie-hellman-group14-sha1",
    "-o", "MACs=+hmac-sha1,hmac-md5",
    "-o", "HostKeyAlgorithms=+ssh-rsa",
    "-o", "PubkeyAcceptedAlgorithms=+ssh-rsa"
)

& ssh @SshOptions $Remote "mkdir -p $InstallDirectory"
if ($LASTEXITCODE -ne 0) { throw "Could not connect to the Yun by SSH." }

& scp @SshOptions -O `
    (Join-Path $ProjectRoot "yun\collector.py") `
    (Join-Path $ProjectRoot "yun\index.html") `
    (Join-Path $ProjectRoot "yun\workout-monitor.init") `
    (Join-Path $ProjectRoot "yun\install_on_yun.sh") `
    "${Remote}:$InstallDirectory/"
if ($LASTEXITCODE -ne 0) { throw "Could not copy the Yun files." }

& ssh $Remote "cd $InstallDirectory && sh install_on_yun.sh"
if ($LASTEXITCODE -ne 0) { throw "The Yun installer failed." }

Write-Host "Phone page: http://$YunHost/workout/"
