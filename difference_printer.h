#ifndef DIFFERENCE_PRINTER_H
#define DIFFERENCE_PRINTER_H

#include "vex.h"
#include <stdio.h>
#include "io/file/file.h"
#include "ui/panel/terminal_diff_panel.h"
#include "diff_engine.h"

// difference_printer.h — custom realtime watch diff printer built on determinant's TerminalDiffPanel.

typedef struct DifferencePrinter {
    TerminalDiffPanel panel;
    u64 editCounter;
    i64 lastFileSize;
    FileEventType lastEventType;
    u32 maxAddDigits;
    u32 maxDelDigits;
} DifferencePrinter;

fn DifferencePrinter_init(DifferencePrinter *printer);
fn DifferencePrinter_setColumns(DifferencePrinter *printer, u32 columns);
fn DifferencePrinter_setLayoutMode(DifferencePrinter *printer, TerminalDiffLayoutMode mode);
fn DifferencePrinter_setColor(DifferencePrinter *printer, bool useColor);

bool DifferencePrinter_print(DifferencePrinter *printer,
                             const FileWatchEvent *event,
                             const DiffResult *diff,
                             FILE *out);

#endif
