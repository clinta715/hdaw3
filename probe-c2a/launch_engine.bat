@echo off
REM Launch the MCP-HTTP engine with param tracing. HDAW_TRACE_PARAM=1 gates the new logger.
set HDAW_TRACE_PARAM=1
"D:\pdf\roo projects\hdaw3\build\HDAW_headless.exe" --mcp-http
