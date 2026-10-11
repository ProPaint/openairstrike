// run.ini report_to=IP:PORT (docs/3ds-port.md "Soak mode and the log push (round 2, P7)"): at the
// clean end of any mode the app pushes the files the run produced (log.txt, perf.csv, soak.txt and
// the mark dumps noted with noteRunFile) over TCP to tools/3ds_receive.py on the PC, in the framing
// of soak_report.h. libctru's soc service is started only here (socInit with a 1 MB buffer), the
// connect times out after 3 s; on failure one log line, and the files stay on the SD card (ftpd
// still works). Nothing is read from the socket.
#pragma once

#include <string>

namespace ctrapp {

// `reportTo`: "IP:PORT" (already validated by run_config). Logs its outcome (AS3D_REPORT lines);
// opens the log itself (call it after the loops closed theirs). True when every file was sent.
bool pushReport(const std::string& reportTo);

} // namespace ctrapp
