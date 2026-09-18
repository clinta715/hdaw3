#!/bin/bash
# C2c engine helper (WSL orchestrator side; Windows commands via cmd.exe).
# usage: probe-c2c/engine.sh kill | launch | md5
case "${1:-launch}" in
  kill)
    cmd.exe /c "taskkill /F /IM HDAW_headless.exe /IM hdaw_plugin_host.exe" >/dev/null 2>&1
    sleep 1; echo KILLED;;
  launch)
    cmd.exe /c "taskkill /F /IM HDAW_headless.exe /IM hdaw_plugin_host.exe" >/dev/null 2>&1
    sleep 2
    cmd.exe /c 'C:\temp\launch_hdaw_diag.cmd'
    ok=0
    for i in $(seq 1 90); do
      if curl -s -m 2 -X POST http://127.0.0.1:18765/mcp -H 'Content-Type: application/json' -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"engine_info","arguments":{}}}' 2>/dev/null | grep -q '"result"'; then ok=1; break; fi
      sleep 1
    done
    [ $ok = 1 ] && echo ENGINE-UP || echo ENGINE-TIMEOUT;;
  md5)
    md5sum "/mnt/d/pdf/roo projects/hdaw3/build/HDAW_headless.exe" "/mnt/d/pdf/roo projects/hdaw3/build/hdaw_plugin_host.exe" "/mnt/d/pdf/roo projects/hdaw3/build/hdaw_tests.exe";;
  procs)
    cmd.exe /c "tasklist /FI \"IMAGENAME eq HDAW_headless.exe\" /FI \"IMAGENAME eq hdaw_plugin_host.exe\"" 2>/dev/null | head -12;;
esac
