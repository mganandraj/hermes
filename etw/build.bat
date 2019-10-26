mc -um hermes_etw.man
rc.exe hermes_etw.h
link.exe  /dll /noentry /machine:x64 hermes_etw.res /OUT:hermes_etw_res.dll
wevtutil im hermes_etw.man /rf:"D:\github\mgan_hermes\hermes\etw\hermes_etw_res.dll" /mf:"D:\github\mgan_hermes\hermes\etw\hermes_etw_res.dll"
wevtutil gp "Hermes-Provider"