/* Windows: the port is a GUI program, so double-clicking it opens no console window. Started
 * from a console (cmd, PowerShell), its messages and --print-settings still show there; output
 * sent to a file or a pipe goes there as before. Elsewhere, nothing to do. */
#ifndef MARS_CONSOLE_H
#define MARS_CONSOLE_H

void console_attach(void);

#endif
