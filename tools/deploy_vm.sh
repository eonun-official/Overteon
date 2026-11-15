#!/bin/sh
# deploy_vm.sh — provision a fresh Windows guest end to end over OpenSSH.
#
# Everything goes through ssh/scp/sftp from this script: file transfer, the
# installs, and the remote commands that verify them.  No WinRM, no scheduled
# tasks — those were needed only because WinRM's token is UAC-filtered, and
# over SSH as Administrator the token is already complete.
#
# Language / locale settings are deliberately NOT touched: the guest was
# reinstalled to get a clean English system.
#
# Usage:
#   tools/deploy_vm.sh <ip> <password> [staging-dir]
#
# What it reproduces from the previous environment:
#   * proxies (WinHTTP for services, WinINET for user apps)
#   * test-signing boot options
#   * audio services (MME / WASAPI depend on them)
#   * audio endpoint names forced to English (the guest's own MUI is Chinese,
#     and PKEY_Device_DeviceDesc is a cached string that only this exact
#     RegistryRights::SetValue path can overwrite)
#   * VC++ runtime, JACK2, Ardour, Falkon
#   * the Overteon plug-ins (LV2 / VST3 / VST2) + Qt runtime
#   * a running jackd on the PortAudio backend, verified with jack_lsp

set -eu

IP="${1:?usage: deploy_vm.sh <ip> <password> [staging-dir]}"
PASS="${2:?usage: deploy_vm.sh <ip> <password> [staging-dir]}"
STAGE="${3:-/tmp/ovstage}"
PROXY="${PROXY:-172.16.0.188:2080}"

SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
          -o LogLevel=ERROR -o ConnectTimeout=20"

say() { printf '\n=== %s ===\n' "$*"; }

# Run a PowerShell snippet on the guest.  The script is piped over stdin rather
# than quoted into the command line, so nothing in it needs escaping.
rsh() {
    timeout 900 sshpass -p "$PASS" ssh $SSH_OPTS "Administrator@$IP" \
        "powershell -NoProfile -ExecutionPolicy Bypass -Command -" 
}

# ---- 1. connection ---------------------------------------------------------
say "connect"
timeout 60 sshpass -p "$PASS" ssh $SSH_OPTS "Administrator@$IP" \
    "hostname & powershell -NoProfile -Command \"(Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').EditionID\"" \
    2>&1 | tr -d '\r'

# ---- 2. transfer -----------------------------------------------------------
say "transfer"
cat > /tmp/ov_sftp_batch <<SFTP
mkdir -p /C:/Setups
$(for f in jack2-win64.exe ovwin.tar.gz falkon-setup.exe ardour-setup.exe; do
     [ -f "$STAGE/$f" ] && echo "put $STAGE/$f /C:/Setups/$f"
   done)
SFTP
timeout 900 sshpass -p "$PASS" sftp $SSH_OPTS "Administrator@$IP" \
    < /tmp/ov_sftp_batch >/tmp/ov_sftp.log 2>&1 || true
grep -c "Uploading" /tmp/ov_sftp.log | sed 's/^/  files uploaded: /'

# ---- 3. system: proxy, signing, audio services -----------------------------
say "system"
rsh_proxy() { sed "s/@PROXY@/$PROXY/g" | rsh; }
rsh_proxy <<'PS1'
$ErrorActionPreference = 'Continue'
$o = @()

# Proxy.  WinHTTP is what services and Windows Update use; WinINET is the
# per-user setting applications read.
$P = '@PROXY@'
$null = netsh winhttp set proxy $P 2>&1
$k = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'
Set-ItemProperty $k ProxyEnable 1 -Type DWord -Force
Set-ItemProperty $k ProxyServer $P -Force
Set-ItemProperty $k ProxyOverride '<local>' -Force
$o += "proxy            $P"

# Test signing (lv2/vst drivers and any test-signed component need it).
$null = bcdedit /set testsigning on 2>&1
$null = bcdedit /set nointegritychecks on 2>&1
$o += "testsigning      " + (((bcdedit /enum '{current}') | Select-String testsigning) -join '').Trim()

# Audio services: MME and WASAPI both fail to enumerate without these.
foreach ($sv in @('AudioEndpointBuilder','Audiosrv','MMCSS')) {
    Set-Service $sv -StartupType Automatic
    if ((Get-Service $sv).Status -ne 'Running') { Start-Service $sv }
    $o += "{0,-17}{1}" -f $sv, (Get-Service $sv).Status
}

$o -join "`n"
PS1

# ---- 4. English audio endpoint names ---------------------------------------
# The device names an application sees are PKEY_Device_DeviceDesc on the
# endpoint, stored as a plain REG_SZ under MMDevices.  That key grants
# BUILTIN\Administrators "SetValue" and nothing else — not WriteKey, not Delete
# — so Set-ItemProperty and reg add both fail with "access denied", and
# DeleteSubKeyTree silently leaves the key in place.  Opening the key with
# exactly RegistryRights::SetValue is the only write that succeeds.
say "audio endpoint names"
rsh <<'PS1'
$ErrorActionPreference = 'Continue'
$o = @()
$K  = '{a45c254e-df1c-4efd-8020-67d146a850e0},2'
$FR = '{b3f8fa53-0004-438e-9003-51a46e139bfc},6'
$FL = '{b3f8fa53-0004-438e-9003-51a46e139bfc},2'

foreach ($sv in @('Audiosrv','AudioEndpointBuilder')) { Stop-Service $sv -Force -ErrorAction SilentlyContinue }
Start-Sleep -Seconds 4

foreach ($kind in @('Render','Capture')) {
    $base = "SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\$kind"
    $keys = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($base)
    if (-not $keys) { continue }
    foreach ($guid in $keys.GetSubKeyNames()) {
        # Windows default role only; the "communications" duplicate needs the
        # same treatment or one of the two names stays in the old language.
        $p = "$base\$guid\Properties"
        $rk = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey(
                $p,
                [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,
                [System.Security.AccessControl.RegistryRights]::SetValue)
        if (-not $rk) { $o += "  CANNOT OPEN $kind\$guid"; continue }

        $cur = $rk.GetValue($FR)
        if (-not $cur) { $cur = $rk.GetValue($K) }
        # "<friendly> (<adapter>)" is what the user sees; keep the adapter part.
        $name = 'Speakers'
        $lower = ($cur + '').ToLowerInvariant()
        if ($lower -match 'mic|捕获|輸入|输入|line|capture') { $name = 'Line In' }
        if ($kind -eq 'Render') { $name = 'Speakers' }

        $rk.SetValue($K,  $name, [Microsoft.Win32.RegistryValueKind]::String)
        $rk.SetValue($FR, "$name (High Definition Audio Device)", [Microsoft.Win32.RegistryValueKind]::String)
        $rk.Close()
        $o += "  $kind -> $name"
    }
    $keys.Close()
}

foreach ($sv in @('AudioEndpointBuilder','Audiosrv')) { Start-Service $sv }
Start-Sleep -Seconds 8
Get-PnpDevice -Class AudioEndpoint -ErrorAction SilentlyContinue |
    ForEach-Object { $o += "  pnp: " + $_.FriendlyName }
$o -join "`n"
PS1

# ---- 5. installs -----------------------------------------------------------
say "install"
rsh <<'PS1'
$ErrorActionPreference = 'Continue'
$o = @()
function Try-Run([string]$label, [string]$exe, [string]$switches) {
    if (-not (Test-Path $exe)) { return "  $label  MISSING $exe" }
    $p = Start-Process -FilePath $exe -ArgumentList $switches -Wait -PassThru
    "  $label  exit=$($p.ExitCode)"
}

# VC++ runtime — Qt6 links MSVCP140 / VCRUNTIME140.
$o += Try-Run 'vc_redist' 'C:\vc_redist.x64.exe' '/install /quiet /norestart'

# JACK2 (PortAudio backend, which is what exposes WASAPI).
# Inno Setup, not NSIS: "/S" leaves it waiting on a dialog forever.
$o += Try-Run 'jack2' 'C:\Setups\jack2.exe' '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART'

# Ardour.
# Ardour and Falkon are NSIS: "/S" is correct for them.
$o += Try-Run 'ardour' 'C:\Setups\ardour.exe' '/S'

# Falkon.
$o += Try-Run 'falkon' 'C:\Setups\falkon.exe' '/S'

Start-Sleep -Seconds 6
foreach ($p in @(
    'C:\Program Files\JACK2\jackd.exe',
    'C:\Program Files\Ardour9\bin\Ardour.exe',
    'C:\Program Files\Falkon\bin\falkon.exe')) {
    $o += "  " + (Test-Path $p) + "  $p"
}
$o -join "`n"
PS1

# ---- 6. plug-ins -----------------------------------------------------------
say "plugins"
rsh <<'PS1'
$ErrorActionPreference = 'Continue'
$o = @()
Set-Location C:\
if (Test-Path C:\ovwin) { Remove-Item C:\ovwin -Recurse -Force }
& tar -xzf C:\Setups\ovwin.tar.gz -C C:\
$o += "  extracted=" + (Test-Path C:\ovwin)

$vst3 = 'C:\Program Files\Common Files\VST3'
$vst2 = 'C:\Program Files\Common Files\VST2'
$lv2  = 'C:\Program Files\Common Files\LV2'
New-Item -ItemType Directory -Force -Path $vst3,$vst2,$lv2 | Out-Null
Copy-Item 'C:\ovwin\VST3\Overteon.vst3'      "$vst3\Overteon.vst3" -Recurse -Force
Copy-Item 'C:\ovwin\VST\Overteon.dll'        "$vst2\Overteon.dll"          -Force
Copy-Item 'C:\ovwin\LV2\Overteon.lv2'        "$lv2\Overteon.lv2"   -Recurse -Force

$o += "  VST3=" + (Test-Path "$vst3\Overteon.vst3")
$o += "  VST2=" + (Test-Path "$vst2\Overteon.dll")
$o += "  LV2 =" + (Test-Path "$lv2\Overteon.lv2\Overteon.dll")

# The plug-ins load the Qt runtime from next to themselves or from PATH.
$m = [Environment]::GetEnvironmentVariable('Path','Machine')
if ($m -notlike '*C:\ovwin*') { [Environment]::SetEnvironmentVariable('Path', "$m;C:\ovwin", 'Machine') }
[Environment]::SetEnvironmentVariable('QT_PLUGIN_PATH',    'C:\ovwin\plugins',           'Machine')
[Environment]::SetEnvironmentVariable('QML2_IMPORT_PATH',  'C:\ovwin\qml',               'Machine')
[Environment]::SetEnvironmentVariable('QT_QPA_PLATFORM_PLUGIN_PATH', 'C:\ovwin\plugins\platforms','Machine')
$o += "  env: PATH/QT_PLUGIN_PATH/QML2_IMPORT_PATH set"
$o -join "`n"
PS1

# ---- 7. JACK, verified -----------------------------------------------------
# Two traps here, both about process lifetime and identity:
#
#   * A jackd started from an SSH session dies when that session closes — the
#     session's job object takes the child with it.  So it is started and
#     verified inside ONE ssh call, which is exactly how an audio app uses it.
#   * jackd must run as the SAME USER as its clients.  JACK's named pipe and
#     events carry a per-user DACL: a jackd started as SYSTEM (scheduled task)
#     refuses Administrator's clients with "err = 5" even though both are in
#     session 0.  Run it as the user.
#
# For desktop use a logon task is registered instead, so jackd comes up in the
# interactive session alongside Ardour.  It does nothing until someone logs on.
say "jack"
rsh <<'PS1'
$ErrorActionPreference = 'Continue'
$J = 'C:\Program Files\JACK2'
$env:Path = "$J;$J\tools;$J\lib;$env:Path"

# Logon task for interactive use (harmless when nobody is logged on).
Get-Process jackd -ErrorAction SilentlyContinue | Stop-Process -Force
$launcher = @'
$J = "C:\Program Files\JACK2"
$env:Path = "$J;$J\tools;$J\lib;$env:Path"
Get-Process jackd -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2
& "$J\jackd.exe" -R -d portaudio -r 48000 -p 512 *> C:\jackd.log
'@
Set-Content -Path 'C:\jackd_start.ps1' -Value $launcher -Encoding ASCII
$act = New-ScheduledTaskAction -Execute 'powershell.exe' `
        -Argument '-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File C:\jackd_start.ps1'
$trg = New-ScheduledTaskTrigger -AtLogOn -User 'Administrator'
$prn = New-ScheduledTaskPrincipal -UserId 'Administrator' -LogonType Interactive -RunLevel Highest
Register-ScheduledTask -TaskName 'JackServer' -Action $act -Trigger $trg `
    -Principal $prn -Force | Out-Null

# Now start it in this session and prove a client can talk to it.
Start-Sleep -Seconds 2
Start-Process -FilePath "$J\jackd.exe" `
    -ArgumentList '-R','-d','portaudio','-r','48000','-p','512' -WindowStyle Hidden
Start-Sleep -Seconds 14

$o = @()
$o += "  jackd: " + ((Get-Process jackd -ErrorAction SilentlyContinue |
        ForEach-Object { "pid=$($_.Id) session=$($_.SessionId)" }) -join ',')
$o += "  ports:"
$o += (& "$J\tools\jack_lsp.exe" 2>&1 | ForEach-Object { "    $_" })
$o += "  bufsize=" + ((& "$J\tools\jack_bufsize.exe"    2>&1) -join '')
$o += "  rate   =" + ((& "$J\tools\jack_samplerate.exe" 2>&1) -join '')
$o -join "`n"
PS1

# ---- 8. edition: Evaluation -> Standard -----------------------------------
# The ISO is an Evaluation image, so the guest arrives as ServerStandardEval
# on the TIMEBASED_EVAL channel.  That SKU rejects the retail GVLK
# (0xC004F050) and cannot be activated, so the edition has to be changed first.
#
# Two things made this confusing and are worth recording:
#   * dism /Set-Edition with no key fails with Error 87 ("provide a valid
#     product key"); /Get-TargetEditions does list ServerStandard, so the key
#     is what unlocks it.
#   * the conversion SUCCEEDS but only commits on reboot, and until then CBS
#     shows RebootPending/PackagesPending and a second Set-Edition fails with
#     Error 2 ("edition was not detected").  The reboot is not optional.
say "edition"
if [ "${SET_EDITION:-1}" = "1" ]; then
rsh <<'PS1'
$ErrorActionPreference = 'Continue'
$o = @()
$ed = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').EditionID
$o += "  before: $ed"

if ($ed -eq 'ServerStandardEval') {
    # GVLK for Windows Server 2019 Standard (Microsoft's published KMS client key).
    $r = & dism /online /Set-Edition:ServerStandard `
             /ProductKey:N69G4-B89J2-4G8F4-WWYCC-J464C /AcceptEula /English 2>&1
    $o += ($r | Select-String 'Error|error|The operation completed' | ForEach-Object { "  " + $_.Line.Trim() })
    $o += "  scheduled; a reboot commits it"
} elseif ($ed -eq 'ServerStandard') {
    $o += "  already ServerStandard"
} else {
    $o += "  unexpected edition, not touching it"
}
$o -join "`n"
PS1
fi

# ---- 9. activation ---------------------------------------------------------
# After the reboot the guest is ServerStandard on VOLUME_KMSCLIENT and merely
# needs a KMS server.  HEU KMS Activator provides one locally; "/kwi" is its
# silent "activate Windows by KMS" switch, and it returns immediately (the work
# happens in child processes), so the status is read back rather than trusting
# the exit code.
say "activation"
if [ "${ACTIVATE:-1}" = "1" ]; then
rsh <<'PS1'
$ErrorActionPreference = 'Continue'
$o = @()
$status = ((cscript //nologo C:\Windows\System32\slmgr.vbs /dli) 2>&1) -join " "
if ($status -match 'LICENSED|已授权') {
    $o += "  already licensed"
} else {
    Get-Process heu -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 2
    Start-Process 'C:\heu.exe' -ArgumentList '/kwi' -WindowStyle Hidden | Out-Null
    Start-Sleep -Seconds 45
    Get-Process heu -ErrorAction SilentlyContinue | Stop-Process -Force
}
$o += ((cscript //nologo C:\Windows\System32\slmgr.vbs /dli) 2>&1 |
        Select-Object -First 6 | ForEach-Object { "  " + $_.Trim() })
$o -join "`n"
PS1
fi

say "done"
