#pragma once

#include <QString>

// Whether the canvas can draw with the graphics card (OpenGL), and which one.
//
// Setting `canvas/gpu` (Settings > Canvas): "auto" (default) uses the
// graphics card when its driver offers desktop OpenGL 3.0 or newer and it is
// not a software stand-in (llvmpipe, softpipe ...); "on" uses any working
// OpenGL (for testing); "off" always draws on the processor.
namespace GpuSupport {

struct Info {
    bool contextOk = false;   // an OpenGL context could be made
    bool hardware = false;    // not a software renderer
    QString vendor, renderer, version;
    int maxTextureSize = 0;
    qint64 freeMemoryMb = -1; // free graphics memory at the check; -1: unknown
    QString problem;          // why not, in plain words
};

// Checked once per program run (needs the QGuiApplication).
const Info &probe();

enum class Mode { Auto, On, Off };
Mode mode();
void setMode(Mode mode);

// The setting and the check together: should the canvas use the card?
bool wanted(QString *why = nullptr);

// "NVIDIA Quadro P1000/PCIe/SSE2, OpenGL 4.6.0 NVIDIA 580.95" and the like.
QString describe();

// A driver can crash the whole program. Set before the canvas starts using
// the card, cleared after its first good frame (or a clean fallback): still
// set at the next start means the card stopped the program, and "Automatic"
// then uses the processor until a choice is made again in Settings.
void markStarting(bool starting);
bool stoppedLastTime();
// "--no-gpu" on the command line: the processor, for this run only.
void disableForThisRun();

}  // namespace GpuSupport
