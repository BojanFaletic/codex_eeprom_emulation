@echo off
setlocal
set GHDL=C:\ghdl-mcode-5.1.1-mingw64\bin\ghdl.exe

"%GHDL%" -a --std=08 hdl\src\spi_flash.vhd || goto :eof
"%GHDL%" -a --std=08 hdl\tb\tb_spi_flash.vhd || goto :eof
"%GHDL%" -e --std=08 tb_spi_flash || goto :eof
"%GHDL%" -r --std=08 tb_spi_flash --vcd=hdl\waves.vcd || goto :eof

echo Done. Waves at hdl\waves.vcd
endlocal
