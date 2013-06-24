// vi:noai:sw=4

#include "terminol/common/config.hxx"
#include "terminol/support/conv.hxx"

#include <limits>

const Color COLOURS_LINUX[16] = {
  { 0x00, 0x00, 0x00 },
  { 0xA8, 0x00, 0x00 },
  { 0x00, 0xA8, 0x00 },
  { 0xA8, 0x57, 0x00 },
  { 0x00, 0x00, 0xA8 },
  { 0xA8, 0x00, 0xA8 },
  { 0x00, 0xA8, 0xA8 },
  { 0xA8, 0xA8, 0xA8 },
  { 0x57, 0x57, 0x57 },
  { 0xFF, 0x57, 0x57 },
  { 0x57, 0xFF, 0x57 },
  { 0xFF, 0xFF, 0x57 },
  { 0x57, 0x57, 0xFF },
  { 0xFF, 0x57, 0xFF },
  { 0x57, 0xFF, 0xFF },
  { 0xFF, 0xFF, 0xFF }
};

const Color COLOURS_RXVT[16] = {
  { 0x00, 0x00, 0x00 },
  { 0xCD, 0x00, 0x00 },
  { 0x00, 0xCD, 0x00 },
  { 0xCD, 0xCD, 0x00 },
  { 0x00, 0x00, 0xCD },
  { 0xCD, 0x00, 0xCD },
  { 0x00, 0xCD, 0xCD },
  { 0xFA, 0xEB, 0xD7 },
  { 0x40, 0x40, 0x40 },
  { 0xFF, 0x00, 0x00 },
  { 0x00, 0xFF, 0x00 },
  { 0xFF, 0xFF, 0x00 },
  { 0x00, 0x00, 0xFF },
  { 0xFF, 0x00, 0xFF },
  { 0x00, 0xFF, 0xFF },
  { 0xFF, 0xFF, 0xFF }
};

const Color COLOURS_TANGO[16] = {
  { 0x2E, 0x34, 0x36 },
  { 0xCC, 0x00, 0x00 },
  { 0x4E, 0x9A, 0x06 },
  { 0xC4, 0xA0, 0x00 },
  { 0x34, 0x65, 0xA4 },
  { 0x75, 0x50, 0x7B },
  { 0x06, 0x98, 0x9A },
  { 0xD3, 0xD7, 0xCF },
  { 0x55, 0x57, 0x53 },
  { 0xEF, 0x29, 0x29 },
  { 0x8A, 0xE2, 0x34 },
  { 0xFC, 0xE9, 0x4F },
  { 0x72, 0x9F, 0xCF },
  { 0xAD, 0x7F, 0xA8 },
  { 0x34, 0xE2, 0xE2 },
  { 0xEE, 0xEE, 0xEC }
};

const Color COLOURS_XTERM[16] = {
  { 0x00, 0x00, 0x00 },
  { 0xCD, 0x00, 0x00 },
  { 0x00, 0xCD, 0x00 },
  { 0xCD, 0xCD, 0x00 },
  { 0x00, 0x00, 0xEE },
  { 0xCD, 0x00, 0xCD },
  { 0x00, 0xCD, 0xCD },
  { 0xE5, 0xE5, 0xE5 },
  { 0x7F, 0x7F, 0x7F },
  { 0xFF, 0x00, 0x00 },
  { 0x00, 0xFF, 0x00 },
  { 0xFF, 0xFF, 0x00 },
  { 0x5C, 0x5C, 0xFF },
  { 0xFF, 0x00, 0xFF },
  { 0x00, 0xFF, 0xFF },
  { 0xFF, 0xFF, 0xFF }
};

const Color COLOURS_ZENBURN_DARK[16] = {
  { 0x00, 0x00, 0x00 },
  { 0x9E, 0x18, 0x28 },
  { 0xAE, 0xCE, 0x92 },
  { 0x96, 0x8A, 0x38 },
  { 0x41, 0x41, 0x71 },
  { 0x96, 0x3C, 0x59 },
  { 0x41, 0x81, 0x79 },
  { 0xBE, 0xBE, 0xBE },
  { 0x66, 0x66, 0x66 },
  { 0xCF, 0x61, 0x71 },
  { 0xC5, 0xF7, 0x79 },
  { 0xFF, 0xF7, 0x96 },
  { 0x41, 0x86, 0xBE },
  { 0xCF, 0x9E, 0xBE },
  { 0x71, 0xBE, 0xBE },
  { 0xFF, 0xFF, 0xFF }
};

const Color COLOURS_ZENBURN[16] = {
  { 0x3F, 0x3F, 0x3F },
  { 0x70, 0x50, 0x50 },
  { 0x60, 0xB4, 0x8A },
  { 0xDF, 0xAF, 0x8F },
  { 0x50, 0x60, 0x70 },
  { 0xDC, 0x8C, 0xC3 },
  { 0x8C, 0xD0, 0xD3 },
  { 0xDC, 0xDC, 0xCC },
  { 0x70, 0x90, 0x80 },
  { 0xDC, 0xA3, 0xA3 },
  { 0xC3, 0xBF, 0x9F },
  { 0xF0, 0xDF, 0xAF },
  { 0x94, 0xBF, 0xF3 },
  { 0xEC, 0x93, 0xD3 },
  { 0x93, 0xE0, 0xE3 },
  { 0xFF, 0xFF, 0xFF }
};

const Color COLOURS_SOLARIZED_DARK[16] = {
  { 0x07, 0x36, 0x42 },
  { 0xDC, 0x32, 0x2F },
  { 0x85, 0x99, 0x00 },
  { 0xB5, 0x89, 0x00 },
  { 0x26, 0x8B, 0xD2 },
  { 0xD3, 0x36, 0x82 },
  { 0x2A, 0xA1, 0x98 },
  { 0xEE, 0xE8, 0xD5 },
  { 0x00, 0x2B, 0x36 },
  { 0xCB, 0x4B, 0x16 },
  { 0x58, 0x6E, 0x75 },
  { 0x65, 0x7B, 0x83 },
  { 0x83, 0x94, 0x96 },
  { 0x6C, 0x71, 0xC4 },
  { 0x93, 0xA1, 0xA1 },
  { 0xFD, 0xF6, 0xE3 }
};

const Color COLOURS_SOLARIZED_LIGHT[16] = {
  { 0xEE, 0xE8, 0xD5 },
  { 0xDC, 0x32, 0x2F },
  { 0x85, 0x99, 0x00 },
  { 0xB5, 0x89, 0x00 },
  { 0x26, 0x8B, 0xD2 },
  { 0xD3, 0x36, 0x82 },
  { 0x2A, 0xA1, 0x98 },
  { 0x07, 0x36, 0x42 },
  { 0xFD, 0xF6, 0xE3 },
  { 0xCB, 0x4B, 0x16 },
  { 0x93, 0xA1, 0xA1 },
  { 0x83, 0x94, 0x96 },
  { 0x65, 0x7B, 0x83 },
  { 0x6C, 0x71, 0xC4 },
  { 0x58, 0x6E, 0x75 },
  { 0x00, 0x2B, 0x36 }
};

//
//
//

Config::Config() :
    resizeStrategy(Resize::PRESERVE),
    fontName("MesloLGM"),
    fontSize(15),
    termName("xterm-256color"),
    scrollWithHistory(false),
    scrollOnTtyOutput(false),
    scrollOnTtyKeyPress(true),
    scrollOnResize(false),
    scrollOnPaste(true),
    title("terminol"),
    icon("terminol"),
    chdir(),
    scrollBackHistory(4096),
    unlimitedScrollBack(true),
    reflowHistory(1024),
    framesPerSecond(50),
    traditionalWrapping(false),
    //
    traceTty(false),
    syncTty(false),
    //
    initialX(-1),
    initialY(-1),
    initialRows(24),
    initialCols(80),
    //
    customCursorFillColor(false),
    customCursorTextColor(false),
    //
    scrollbarWidth(8),
    //
    borderThickness(1),
    doubleClickTimeout(400),
    //
    serverFork(true)
{
    setColorScheme("solarized-dark");

    std::ostringstream ost;
    ost << "/tmp/terminols-" << ::getenv("USER");
    socketPath = ost.str();
}

void Config::setColorScheme(const std::string & name) {
    if (name == "linux") {
        std::copy(COLOURS_LINUX, COLOURS_LINUX + 16, systemColors);

        fgColor = systemColors[7];
        bgColor = systemColors[0];
        customCursorFillColor = false;
    }
    else if (name == "rxvt") {
        std::copy(COLOURS_RXVT, COLOURS_RXVT + 16, systemColors);

        fgColor = systemColors[7];
        bgColor = systemColors[0];
        customCursorFillColor = false;
    }
    else if (name == "tango") {
        std::copy(COLOURS_TANGO, COLOURS_TANGO + 16, systemColors);

        fgColor = systemColors[7];
        bgColor = systemColors[0];
        customCursorFillColor = false;
    }
    else if (name == "xterm") {
        std::copy(COLOURS_XTERM, COLOURS_XTERM + 16, systemColors);

        fgColor = systemColors[7];
        bgColor = systemColors[0];
        customCursorFillColor = false;
    }
    else if (name == "zenburn-dark") {
        std::copy(COLOURS_ZENBURN_DARK, COLOURS_ZENBURN_DARK + 16, systemColors);

        fgColor = systemColors[7];
        bgColor = systemColors[0];
        customCursorFillColor = false;
    }
    else if (name == "zenburn") {
        std::copy(COLOURS_ZENBURN, COLOURS_ZENBURN + 16, systemColors);

        fgColor = systemColors[7];
        bgColor = systemColors[0];
        customCursorFillColor = false;
    }
    else if (name == "solarized-dark") {
        std::copy(COLOURS_SOLARIZED_DARK, COLOURS_SOLARIZED_DARK + 16, systemColors);

        fgColor = systemColors[12];
        bgColor = systemColors[8];

        customCursorFillColor = true;
        cursorFillColor       = systemColors[14];
    }
    else if (name == "solarized-light") {
        std::copy(COLOURS_SOLARIZED_LIGHT, COLOURS_SOLARIZED_LIGHT + 16, systemColors);
        fgColor = systemColors[12];
        bgColor = systemColors[8];

        customCursorFillColor = true;
        cursorFillColor       = systemColors[14];
    }
    else {
        ERROR("No such color scheme: " << name);
    }

    scrollbarFgColor = { 0x7F, 0x7F, 0x7F };
    scrollbarBgColor = bgColor;
    borderColor      = bgColor;
}
