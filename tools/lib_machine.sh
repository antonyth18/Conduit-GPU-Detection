# Shared helpers for the run scripts (sourced, not executed).
#
#   host_tag              hostname without spaces (folder-safe)
#   machine_info OUT ...  writes OUT/machine.txt: non-negotiable 4 (record the
#                         machine) plus the power state the review asked for
#   need FILE PATTERN MSG stop unless FILE contains PATTERN (correctness gates)
#   thread_list           1,2,4,...,nproc

host_tag() {
  local h
  h=$(hostname -s 2>/dev/null || hostname)
  echo "${h// /-}"
}

thread_list() {
  local n t list
  n=$(nproc); list=1; t=2
  while (( t < n )); do list="$list,$t"; t=$((t * 2)); done
  (( n > 1 )) && list="$list,$n"
  echo "$list"
}

need() {
  if ! grep -q "$2" "$1" 2>/dev/null; then
    echo "STOP: $3 (expected \"$2\" in $1)"
    exit 1
  fi
}

machine_info() {
  local out=$1; shift
  {
    echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "host: $(host_tag)"
    echo "git: $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "run: $*"
    echo "OMP_PROC_BIND=${OMP_PROC_BIND:-unset} OMP_PLACES=${OMP_PLACES:-unset}"
    echo
    echo "## OS"
    uname -a
    grep -qi microsoft /proc/version 2>/dev/null && echo "WSL2: yes"
    head -4 /etc/os-release 2>/dev/null
    echo
    echo "## CPU"
    lscpu | grep -E "Model name|^CPU\(s\)|Thread\(s\)|Core\(s\)|Socket|MHz|L2|L3" || true
    echo "SMT active: $(cat /sys/devices/system/cpu/smt/active 2>/dev/null || echo unknown)"
    echo "governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)"
    echo "current MHz (cpu0): $(awk '/cpu MHz/{print $4; exit}' /proc/cpuinfo 2>/dev/null)"
    echo
    echo "## power (the review asks for performance mode on AC power)"
    for s in /sys/class/power_supply/*/online; do [[ -f $s ]] && echo "$s: $(cat "$s")"; done
    cat /sys/firmware/acpi/platform_profile 2>/dev/null | sed 's/^/platform_profile: /' || true
    command -v powerprofilesctl >/dev/null && echo "powerprofilesctl: $(powerprofilesctl get 2>/dev/null)"
    if grep -qi microsoft /proc/version 2>/dev/null && command -v powershell.exe >/dev/null; then
      echo "Windows power plan: $(powershell.exe -NoProfile -Command 'powercfg /getactivescheme' 2>/dev/null | tr -d '\r')"
      # The Settings power mode is an overlay on the plan; powercfg does not show it
      mode=$(powershell.exe -NoProfile -Command "(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Power\User\PowerSchemes' -ErrorAction SilentlyContinue).ActiveOverlayAcPowerScheme" 2>/dev/null | tr -d '\r')
      case "$mode" in
        ded574b5-45a0-4f42-8737-46345c09c238) mode="$mode (Best performance)" ;;
        961cc777-2547-4f9d-8174-7d86181b8a7a) mode="$mode (Best power efficiency)" ;;
        00000000-0000-0000-0000-000000000000|"") mode="${mode:-unset} (Balanced)" ;;
      esac
      echo "Windows power mode (on AC): $mode"
      echo "Windows AC power: $(powershell.exe -NoProfile -Command '(Get-CimInstance -Namespace root/wmi -ClassName BatteryStatus -ErrorAction SilentlyContinue).PowerOnline' 2>/dev/null | tr -d '\r')"
    fi
    echo
    echo "## memory"
    free -h
    echo
    echo "## GPU"
    nvidia-smi --query-gpu=name,memory.total,memory.used,driver_version,pstate,clocks.sm,clocks.max.sm,pcie.link.gen.current,pcie.link.gen.max,pcie.link.width.current,pcie.link.width.max,power.draw,power.limit --format=csv 2>/dev/null || echo "nvidia-smi: not found"
    echo "nvcc: $( (command -v nvcc || ls /usr/local/cuda*/bin/nvcc 2>/dev/null | head -1) || echo 'not on PATH') $( (nvcc --version 2>/dev/null || true) | tail -1)"
    [[ -x build/conduit_gpu ]] && build/conduit_gpu info 2>/dev/null | head -1
    echo
    c++ --version | head -1; cmake --version | head -1
  } > "$out/machine.txt" 2>&1
  local avail
  avail=$(free -m | awk '/^Mem:/{print $7}')
  if [[ -n $avail ]] && (( avail < 2048 )); then
    echo "WARNING: only ${avail} MB RAM available; close other applications for clean timings"
  fi
  echo "machine info -> $out/machine.txt"
}
