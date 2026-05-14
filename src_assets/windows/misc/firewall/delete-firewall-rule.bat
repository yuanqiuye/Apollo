@echo off

set RULE_NAME=Apollo-WinUHid

rem Delete the rule
netsh advfirewall firewall delete rule name=%RULE_NAME%
