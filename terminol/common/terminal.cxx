// vi:noai:sw=4

#include "terminol/common/terminal.hxx"
#include "terminol/support/time.hxx"
#include "terminol/support/conv.hxx"
#include "terminol/support/escape.hxx"

#include <algorithm>
#include <numeric>

namespace {

int32_t nthArg(const std::vector<int32_t> & args, size_t n, int32_t fallback = 0) {
    return n < args.size() ? args[n] : fallback;
}

// Same as nth arg, but use fallback if arg is zero.
int32_t nthArgNonZero(const std::vector<int32_t> & args, size_t n, int32_t fallback) {
    auto arg = nthArg(args, n, fallback);
    return arg != 0 ? arg : fallback;
}

} // namespace {anonymous}

const uint16_t Terminal::TAB_SIZE = 8;

const Terminal::CharSub Terminal::CS_US[] = {
    { NUL, {} }
};

const Terminal::CharSub Terminal::CS_UK[] = {
    { '#', { 0xC2, 0xA3 } }, // POUND: £
    { NUL, {} }
};

const Terminal::CharSub Terminal::CS_SPECIAL[] = {
    { '`', { 0xE2, 0x99, 0xA6 } }, // diamond: ♦
    { 'a', { 0xE2, 0x96, 0x92 } }, // 50% cell: ▒
    { 'b', { 0xE2, 0x90, 0x89 } }, // HT: ␉
    { 'c', { 0xE2, 0x90, 0x8C } }, // FF: ␌
    { 'd', { 0xE2, 0x90, 0x8D } }, // CR: ␍
    { 'e', { 0xE2, 0x90, 0x8A } }, // LF: ␊
    { 'f', { 0xC2, 0xB0       } }, // Degree: °
    { 'g', { 0xC2, 0xB1       } }, // Plus/Minus: ±
    { 'h', { 0xE2, 0x90, 0xA4 } }, // NL: ␤
    { 'i', { 0xE2, 0x90, 0x8B } }, // VT: ␋
    { 'j', { 0xE2, 0x94, 0x98 } }, // CN_RB: ┘
    { 'k', { 0xE2, 0x94, 0x90 } }, // CN_RT: ┐
    { 'l', { 0xE2, 0x94, 0x8C } }, // CN_LT: ┌
    { 'm', { 0xE2, 0x94, 0x94 } }, // CN_LB: └
    { 'n', { 0xE2, 0x94, 0xBC } }, // CROSS: ┼
    { 'o', { 0xE2, 0x8E, 0xBA } }, // Horiz. Scan Line 1: ⎺
    { 'p', { 0xE2, 0x8E, 0xBB } }, // Horiz. Scan Line 3: ⎻
    { 'q', { 0xE2, 0x94, 0x80 } }, // Horiz. Scan Line 5: ─
    { 'r', { 0xE2, 0x8E, 0xBC } }, // Horiz. Scan Line 7: ⎼
    { 's', { 0xE2, 0x8E, 0xBD } }, // Horiz. Scan Line 9: ⎽
    { 't', { 0xE2, 0x94, 0x9C } }, // TR: ├
    { 'u', { 0xE2, 0x94, 0xA4 } }, // TL: ┤
    { 'v', { 0xE2, 0x94, 0xB4 } }, // TU: ┴
    { 'w', { 0xE2, 0x94, 0xAC } }, // TD: ┬
    { 'x', { 0xE2, 0x94, 0x82 } }, // V: │
    { 'y', { 0xE2, 0x89, 0xA4 } }, // LE: ≤
    { 'z', { 0xE2, 0x89, 0xA5 } }, // GE: ≥
    { '{', { 0xCF, 0x80       } }, // PI: π
    { '|', { 0xE2, 0x89, 0xA0 } }, // NEQ: ≠
    { '}', { 0xC2, 0xA3       } }, // POUND: £
    { '~', { 0xE2, 0x8B, 0x85 } }, // DOT: ⋅
    { NUL, {} }
};

//
//
//

Terminal::Terminal(I_Observer   & observer,
                   const Config & config,
                   Deduper      & deduper,
                   uint16_t       rows,
                   uint16_t       cols,
                   const KeyMap & keyMap,
                   I_Tty        & tty) :
    _observer(observer),
    _dispatch(false),
    //
    _config(config),
    _keyMap(keyMap),
    //
    _priBuffer(_config, deduper,
               rows, cols,
               _config.getUnlimitedScrollBack() ?
               std::numeric_limits<uint32_t>::max() :
               _config.getScrollBackHistory()),
    _altBuffer(_config, deduper, rows, cols, 0),
    _buffer(&_priBuffer),
    //
    _modes(),
    _tabs(_buffer->getCols()),
    //
    _cursor(),
    _savedCursor(),
    //
    _damage(),
    //
    _pressed(false),
    //
    _tty(tty),
    _dumpWrites(false),
    _writeBuffer(),
    _utf8Machine(),
    _vtMachine(*this)
{
    _modes.set(Mode::AUTO_WRAP);
    _modes.set(Mode::SHOW_CURSOR);
    _modes.set(Mode::AUTO_REPEAT);
    _modes.set(Mode::ALT_SENDS_ESC);

    for (size_t i = 0; i != _tabs.size(); ++i) {
        _tabs[i] = (i + 1) % TAB_SIZE == 0;
    }
}

Terminal::~Terminal() {
    ASSERT(!_dispatch, "");
}

void Terminal::resize(uint16_t rows, uint16_t cols) {
    // Special exception, resizes can occur during dispatch.

    ASSERT(rows > 0 && cols > 0, "");

    // Clear any wrapNext if the number of cols has changed.
    if (_cursor.wrapNext && _cursor.pos.col != cols - 1) {
        _cursor.wrapNext = false;
    }

    // XXX Unsure about the following strategy.
    // It assumes that the saved cursor is solely used to remember the
    // position on the primary buffer while we are using the alternative
    // buffer. However, it kinda seems that urxvt works this way.

    auto priCursor = _cursor.pos.row;
    auto altCursor = _savedCursor.pos.row;

    if (_buffer != &_priBuffer) { std::swap(priCursor, altCursor); }

    auto cursorAdj      = _priBuffer.resize(rows, cols, priCursor);
    auto savedCursorAdj = _altBuffer.resize(rows, cols, altCursor);

    if (_buffer != &_priBuffer) { std::swap(cursorAdj, savedCursorAdj); }

    // Note, we mustn't call moveCursor()/damageCursor() here because:
    //  - the old coordinates might not be valid
    //  - it will clear wrapNext, if set

    _cursor.pos.row += cursorAdj;
    _cursor.pos.col  = clamp<uint16_t>(_cursor.pos.col, 0, cols - 1);

    if (!(_cursor.pos.row < rows)) {
        PRINT("Bug");
        _cursor.pos.row = rows - 1;
    }

    ASSERT(_cursor.pos.row < rows, "");
    ASSERT(_cursor.pos.col < cols, "");

    _savedCursor.pos.row += savedCursorAdj;
    _savedCursor.pos.col  = clamp<uint16_t>(_cursor.pos.col, 0, cols - 1);

    ASSERT(_savedCursor.pos.row < rows, "");
    ASSERT(_savedCursor.pos.col < cols, "");

    _tabs.resize(cols);
    for (size_t i = 0; i != _tabs.size(); ++i) {
        _tabs[i] = (i + 1) % TAB_SIZE == 0;
    }
}

void Terminal::redraw(Pos begin, Pos end) {
    fixDamage(begin, end, Damager::EXPOSURE);
}

void Terminal::keyPress(xkb_keysym_t keySym, ModifierSet modifiers) {
    if (!handleKeyBinding(keySym, modifiers) && _keyMap.isPotent(keySym)) {
        if (_config.getScrollOnTtyKeyPress() && _buffer->scrollBottomHistory()) {
            fixDamage(Pos(0, 0),
                      Pos(_buffer->getRows(), _buffer->getCols()),
                      Damager::SCROLL);
        }

        std::vector<uint8_t> str;
        if (_keyMap.convert(keySym, modifiers,
                            _modes.get(Mode::APPKEYPAD),
                            _modes.get(Mode::APPCURSOR),
                            _modes.get(Mode::CR_ON_LF),
                            _modes.get(Mode::DELETE_SENDS_DEL),
                            _modes.get(Mode::ALT_SENDS_ESC),
                            str)) {
            write(&str.front(), str.size());
        }
    }
}

void Terminal::buttonPress(Button button, int count, ModifierSet modifiers,
                           bool UNUSED(within), Pos pos) {
    PRINT("press: " << button << ", count=" << count <<
          ", state=" << modifiers << ", " << pos);

    ASSERT(!_pressed, "");

    if (_modes.get(Mode::MOUSE_BUTTON)) {
        int num = static_cast<int>(button);
        if (num >= 3) { num += 64 - 3; }

        if (modifiers.get(Modifier::SHIFT))   { num +=  4; }
        if (modifiers.get(Modifier::ALT))     { num +=  8; }
        if (modifiers.get(Modifier::CONTROL)) { num += 16; }

        std::ostringstream ost;
        if (_modes.get(Mode::MOUSE_SGR)) {
            ost << ESC << "[<"
                << num << ';' << pos.col + 1 << ';' << pos.row + 1
                << 'M';
        }
        else if (pos.row < 223 && pos.col < 223) {
            ost << ESC << "[M"
                << char(32 + num)
                << char(32 + pos.col + 1)
                << char(32 + pos.row + 1);
        }
        else {
            // Couldn't deliver it
        }

        const auto & str = ost.str();

        if (!str.empty()) {
            write(reinterpret_cast<const uint8_t *>(str.data()), str.size());
        }
    }
    else {
        if (button == Button::LEFT) {
            if (count == 1) {
                _buffer->markSelection(pos);
            }
            else {
                _buffer->expandSelection(pos);
            }

            fixDamage(Pos(), Pos(_buffer->getRows(), _buffer->getCols()),
                      Damager::SCROLL);     // FIXME Damager
        }
        else if (button == Button::MIDDLE) {
            _observer.terminalPaste(false);
        }
        else if (button == Button::RIGHT) {
            _buffer->adjustSelection(pos);
            fixDamage(Pos(), Pos(_buffer->getRows(), _buffer->getCols()),
                      Damager::SCROLL);     // FIXME Damager
        }
    }

    _pressed    = true;
    _button     = button;
    _pointerPos = pos;
}

void Terminal::buttonMotion(ModifierSet modifiers, bool within, Pos pos) {
    //PRINT("motion: within=" << within << ", " << pos);

    ASSERT(_pressed, "");

    if (_modes.get(Mode::MOUSE_MOTION)) {
        if (within) {
            int num = static_cast<int>(_button) + 32;

            if (modifiers.get(Modifier::SHIFT))   { num +=  1; }
            if (modifiers.get(Modifier::ALT))     { num +=  2; }
            if (modifiers.get(Modifier::CONTROL)) { num +=  4; }

            std::ostringstream ost;
            if (_modes.get(Mode::MOUSE_SGR)) {
                ost << ESC << "[<"
                    << num << ';' << pos.col + 1 << ';' << pos.row + 1
                    << 'M';
            }
            else if (pos.row < 223 && pos.col < 223) {
                ost << ESC << "[M"
                    << static_cast<char>(32 + num)
                    << static_cast<char>(32 + pos.col + 1)
                    << static_cast<char>(32 + pos.row + 1);
            }
            else {
                // Couldn't deliver it
            }

            const auto & str = ost.str();

            if (!str.empty()) {
                write(reinterpret_cast<const uint8_t *>(str.data()), str.size());
            }
        }
    }
    else if (!_modes.get(Mode::MOUSE_BUTTON)) {
        if (_button == Button::LEFT) {
            _buffer->delimitSelection(pos);
            fixDamage(Pos(), Pos(_buffer->getRows(), _buffer->getCols()),
                      Damager::SCROLL);     // FIXME Damager
        }
    }

    _pointerPos = pos;
}

void Terminal::buttonRelease(bool broken, ModifierSet modifiers) {
    PRINT("release, broken=" << broken);

    ASSERT(_pressed, "");

    if (_modes.get(Mode::MOUSE_BUTTON)) {
        // XXX within??

        auto num = _modes.get(Mode::MOUSE_SGR) ? static_cast<int>(_button) + 32 : 3;
        auto pos = _pointerPos;

        if (modifiers.get(Modifier::SHIFT))   { num +=  4; }
        if (modifiers.get(Modifier::ALT))     { num +=  8; }
        if (modifiers.get(Modifier::CONTROL)) { num += 16; }

        std::ostringstream ost;
        if (_modes.get(Mode::MOUSE_SGR)) {
            ost << ESC << "[<"
                << num << ';' << pos.col + 1 << ';' << pos.row + 1
                << 'm';
        }
        else if (pos.row < 223 && pos.col < 223) {
            ost << ESC << "[M"
                << static_cast<char>(32 + num)
                << static_cast<char>(32 + pos.col + 1)
                << static_cast<char>(32 + pos.row + 1);
        }
        else {
            // Couldn't deliver it
        }

        const auto & str = ost.str();

        if (!str.empty()) {
            write(reinterpret_cast<const uint8_t *>(str.data()), str.size());
        }
    }
    else {
        std::string text;
        if (_buffer->getSelectedText(text)) {
            _observer.terminalCopy(text, false);
        }
    }

    _pressed = false;
}

void Terminal::scrollWheel(ScrollDir dir, ModifierSet UNUSED(modifiers)) {
    switch (dir) {
        case ScrollDir::UP:
            // TODO consolidate scroll operations with method.
            if (_buffer->scrollUpHistory(std::max(1, _buffer->getRows() / 4))) {
                fixDamage(Pos(0, 0),
                          Pos(_buffer->getRows(), _buffer->getCols()),
                          Damager::SCROLL);
            }
            break;
        case ScrollDir::DOWN:
            if (_buffer->scrollDownHistory(std::max(1, _buffer->getRows() / 4))) {
                fixDamage(Pos(0, 0),
                          Pos(_buffer->getRows(), _buffer->getCols()),
                          Damager::SCROLL);
            }
            break;
    }
}

void Terminal::paste(const uint8_t * data, size_t size) {
    if (_config.getScrollOnPaste() && _buffer->scrollBottomHistory()) {
        fixDamage(Pos(0, 0),
                  Pos(_buffer->getRows(), _buffer->getCols()),
                  Damager::SCROLL);
    }

    if (_modes.get(Mode::BRACKETED_PASTE)) {
        std::ostringstream ostr;
        ostr << ESC << "[200~";
        const auto & str = ostr.str();
        write(reinterpret_cast<const uint8_t *>(str.data()), str.size());
    }

    write(data, size);

    if (_modes.get(Mode::BRACKETED_PASTE)) {
        std::ostringstream ostr;
        ostr << ESC << "[201~";
        const auto & str = ostr.str();
        write(reinterpret_cast<const uint8_t *>(str.data()), str.size());
    }
}

void Terminal::clearSelection() {
    _buffer->clearSelection();
    fixDamage(Pos(), Pos(_buffer->getRows(), _buffer->getCols()),
              Damager::SCROLL);     // FIXME Damager
}

void Terminal::read() {
    ASSERT(!_dispatch, "");

    _dispatch = true;

    try {
        Timer   timer(1000 / _config.getFramesPerSecond());
        uint8_t buf[BUFSIZ];          // 8192 last time I looked.
        auto    size = sizeof buf;
        if (_config.getSyncTty()) { size = std::min(size, static_cast<size_t>(16)); }
        do {
            auto rval = _tty.read(buf, size);
            if (rval == 0) { break; }
            processRead(buf, rval);
        } while (!timer.expired());
    }
    catch (const I_Tty::Exited & ex) {
        _observer.terminalChildExited(ex.exitCode);
    }

    if (!_config.getSyncTty()) {
        fixDamage(Pos(0, 0),
                  Pos(_buffer->getRows(), _buffer->getCols()),
                  Damager::TTY);
    }

    _dispatch = false;
}

bool Terminal::needsFlush() const {
    ASSERT(!_dispatch, "");
    return !_writeBuffer.empty();
}

void Terminal::flush() {
    ASSERT(!_dispatch, "");
    ASSERT(needsFlush(), "No writes queued.");
    ASSERT(!_dumpWrites, "Dump writes is set.");

    try {
        do {
            auto rval = _tty.write(&_writeBuffer.front(), _writeBuffer.size());
            if (rval == 0) { break; }
            _writeBuffer.erase(_writeBuffer.begin(), _writeBuffer.begin() + rval);
        } while (!_writeBuffer.empty());
    }
    catch (const I_Tty::Error & ex) {
        _dumpWrites = true;
        _writeBuffer.clear();
    }
}

bool Terminal::handleKeyBinding(xkb_keysym_t keySym, ModifierSet modifiers) {
    // FIXME no hard-coded keybindings. Use config.

    if (modifiers.get(Modifier::SHIFT) && modifiers.get(Modifier::CONTROL)) {
        switch (keySym) {
            case XKB_KEY_X: {
                std::string text;
                if (_buffer->getSelectedText(text)) {
                    _observer.terminalCopy(text, false);
                }
                return true;
            }
            case XKB_KEY_C: {
                std::string text;
                if (_buffer->getSelectedText(text)) {
                    _observer.terminalCopy(text, true);
                }
                return true;
            }
            case XKB_KEY_V: {
                _observer.terminalPaste(true);
                return true;
            }
        }
    }

    if (modifiers.get(Modifier::SHIFT)) {
        switch (keySym) {
            case XKB_KEY_Up:
                if (_buffer->scrollUpHistory(1)) {
                    fixDamage(Pos(0, 0),
                              Pos(_buffer->getRows(), _buffer->getCols()),
                              Damager::SCROLL);
                }
                return true;
            case XKB_KEY_Down:
                if (_buffer->scrollDownHistory(1)) {
                    fixDamage(Pos(0, 0),
                              Pos(_buffer->getRows(), _buffer->getCols()),
                              Damager::SCROLL);
                }
                return true;
            case XKB_KEY_Page_Up:
                if (_buffer->scrollUpHistory(_buffer->getRows())) {
                    fixDamage(Pos(0, 0),
                              Pos(_buffer->getRows(), _buffer->getCols()),
                              Damager::SCROLL);
                }
                return true;
            case XKB_KEY_Page_Down:
                if (_buffer->scrollDownHistory(_buffer->getRows())) {
                    fixDamage(Pos(0, 0),
                              Pos(_buffer->getRows(), _buffer->getCols()),
                              Damager::SCROLL);
                }
                return true;
            case XKB_KEY_Home:
                if (_buffer->scrollTopHistory()) {
                    fixDamage(Pos(0, 0),
                              Pos(_buffer->getRows(), _buffer->getCols()),
                              Damager::SCROLL);
                }
                return true;
            case XKB_KEY_End:
                if (_buffer->scrollBottomHistory()) {
                    fixDamage(Pos(0, 0),
                              Pos(_buffer->getRows(), _buffer->getCols()),
                              Damager::SCROLL);
                }
                return true;
            default:
                return false;
        }
    }

    switch (keySym) {
        case XKB_KEY_F9:
            _buffer->dump(std::cerr);
            return true;
    }

    return false;
}

void Terminal::moveCursorOriginMode(Pos pos) {
    moveCursor(pos.down(_cursor.originMode ? _buffer->getMarginBegin() : 0));
}

void Terminal::moveCursor(Pos pos) {
    damageCursor();

    if (_cursor.originMode) {
        _cursor.pos.row = clamp<int32_t>(pos.row,
                                         _buffer->getMarginBegin(),
                                         _buffer->getMarginEnd() - 1);
    }
    else {
        _cursor.pos.row = clamp<int32_t>(pos.row, 0, _buffer->getRows() - 1);
    }

    _cursor.pos.col  = clamp<int32_t>(pos.col, 0, _buffer->getCols() - 1);
    _cursor.wrapNext = false;
}

void Terminal::tabCursor(TabDir dir, uint16_t count) {
    auto col = _cursor.pos.col;

    switch (dir) {
        case TabDir::FORWARD:
            while (count != 0) {
                ++col;

                if (col == _buffer->getCols()) {
                    --col;
                    break;
                }

                if (_tabs[col]) {
                    --count;
                }
            }
            break;
        case TabDir::BACKWARD:
            while (count != 0) {
                if (col == 0) {
                    break;
                }

                --col;

                if (_tabs[col]) {
                    --count;
                }
            }
            break;
    }

    moveCursor(_cursor.pos.atCol(col));
}

void Terminal::damageCursor() {
    ASSERT(_cursor.pos.row < _buffer->getRows(), "");
    ASSERT(_cursor.pos.col < _buffer->getCols(), "");
    _buffer->damageCell(_cursor.pos);
}

void Terminal::fixDamage(Pos begin, Pos end, Damager damager) {
    if (damager == Damager::TTY &&
        _config.getScrollOnTtyOutput() &&
        _buffer->scrollBottomHistory())
    {
        // Promote the damage from TTY to SCROLL.
        damager = Damager::SCROLL;
    }

    if (_observer.terminalFixDamageBegin(damager != Damager::EXPOSURE)) {
        draw(begin, end, damager);

        bool scrollbar =    // Identical in two places.
            damager == Damager::SCROLL || damager == Damager::EXPOSURE ||
            (damager == Damager::TTY && _buffer->getBarDamage());

        // FIXME discrepancy between _damage and begin/end
        _observer.terminalFixDamageEnd(damager != Damager::EXPOSURE,
                                       _damage.begin, _damage.end,
                                       scrollbar);

        if (damager == Damager::TTY) {
            _buffer->resetDamage();
        }
    }
    else {
        // If we received a redraw() then the observer had better be able
        // to handle it.
        ENFORCE(damager != Damager::EXPOSURE, "");
    }
}

bool Terminal::translate(uint8_t ascii, utf8::Seq & seq) const {
    for (auto cs = *_cursor.cs; cs->match != NUL; ++cs) {
        if (ascii == cs->match) {
            if (_config.getTraceTty()) {
                std::cerr
                    << Esc::BG_BLUE << Esc::FG_WHITE
                    << '/' << cs->match << '/' << cs->replace << '/'
                    << Esc::RESET;
            }
            seq = cs->replace;
            return true;
        }
    }

    return false;
}

void Terminal::draw(Pos begin, Pos end, Damager damage) {
    _damage.clear();

    // Declare run at the outer scope (rather than for each row) to
    // minimise alloc/free.
    std::vector<uint8_t> run;
    // Reserve the largest amount of memory we could require.
    run.reserve(_buffer->getCols() * utf8::Length::LMAX + 1);

    for (uint16_t r = begin.row; r != end.row; ++r) {
        uint16_t c_    = 0;    // Accumulation start column.
        auto     style = Style::normal();       // FIXME default initialise
        uint16_t c;
        uint16_t colBegin2, colEnd2;

        if (damage == Damager::TTY) {
            _buffer->getDamage(r, colBegin2, colEnd2);
            //PRINT("Consulted buffer damage, got: " << colBegin2 << ", " << colEnd2);
        }
        else {
            colBegin2 = begin.col;
            colEnd2   = end.col;
            //PRINT("External damage damage, got: " << colBegin2 << ", " << colEnd2);
        }

        // Update _damage.colBegin and _damage.colEnd.
        if (_damage.begin.col == _damage.end.col) {
            _damage.begin.col = colBegin2;
            _damage.end.col   = colEnd2;
        }
        else if (colBegin2 != colEnd2) {
            _damage.begin.col = std::min(_damage.begin.col, colBegin2);
            _damage.end.col   = std::max(_damage.end.col,   colEnd2);
        }

        // Update _damage.rowBegin and _damage.rowEnd.
        if (colBegin2 != colEnd2) {
            if (_damage.begin.row == _damage.end.row) {
                _damage.begin.row = r;
            }

            _damage.end.row = r + 1;
        }

        // Step through each column, accumulating runs of compatible consecutive cells.
        for (c = colBegin2; c != colEnd2; ++c) {
            const Cell & cell = _buffer->getCell(Pos(r, c));

            if (run.empty() || style != cell.style) {
                if (!run.empty()) {
                    // flush run
                    run.push_back(NUL);
                    _observer.terminalDrawRun(Pos(r, c_), style, &run.front(), c - c_);
                    run.clear();
                }

                c_    = c;
                style = cell.style;

                if (_modes.get(Mode::REVERSE)) { std::swap(style.fg, style.bg); }

                utf8::Length length = utf8::leadLength(cell.seq.lead());
                run.resize(length);
                std::copy(cell.seq.bytes, cell.seq.bytes + length, &run.front());
            }
            else {
                size_t oldSize = run.size();
                utf8::Length length = utf8::leadLength(cell.seq.lead());
                run.resize(run.size() + length);
                std::copy(cell.seq.bytes, cell.seq.bytes + length, &run[oldSize]);
            }
        }

        // There may be an unterminated run to flush.
        if (!run.empty()) {
            run.push_back(NUL);
            _observer.terminalDrawRun(Pos(r, c_), style, &run.front(), c - c_);
            run.clear();
        }
    }

    if (_modes.get(Mode::SHOW_CURSOR) &&
        _buffer->getScrollOffset() + _cursor.pos.row < _buffer->getRows())
    {
        Pos pos(_cursor.pos.row + _buffer->getScrollOffset(), _cursor.pos.col);

        ASSERT(pos.row < _buffer->getRows(), "");
        ASSERT(pos.col < _buffer->getCols(), "");

        // Update _damage.colBegin and _damage.colEnd.
        if (_damage.begin.col == _damage.end.col) {
            _damage.begin.col = pos.col;
            _damage.end.col   = pos.col + 1;
        }
        else {
            _damage.begin.col = std::min(_damage.begin.col, pos.col);
            _damage.end.col   = std::max(_damage.end.col,   static_cast<uint16_t>(pos.col + 1));
        }

        // Update _damage.rowBegin and _damage.rowEnd.
        if (_damage.begin.row == _damage.end.row) {
            _damage.begin.row = pos.row;
            _damage.end.row   = pos.row + 1;
        }
        else {
            _damage.begin.row = std::min(_damage.begin.row, pos.row);
            _damage.end.row   = std::max(_damage.end.row,   static_cast<uint16_t>(pos.row + 1));
        }

        const Cell & cell   = _buffer->getCell(pos);
        utf8::Length length = utf8::leadLength(cell.seq.lead());
        run.resize(length);
        std::copy(cell.seq.bytes, cell.seq.bytes + length, &run.front());
        run.push_back(NUL);
        _observer.terminalDrawCursor(pos, cell.style, &run.front(), _cursor.wrapNext);
    }

    if (true) {
        Pos sBegin, sEnd;
        bool topless, bottomless;
        if (_buffer->getSelectedArea(sBegin, sEnd, topless, bottomless)) {
            _observer.terminalDrawSelection(sBegin, sEnd, topless, bottomless);
        }
    }

    bool scrollbar =    // Identical in two places.
        damage == Damager::SCROLL || damage == Damager::EXPOSURE ||
        (damage == Damager::TTY && _buffer->getBarDamage());

    if (scrollbar) {
        _observer.terminalDrawScrollbar(_buffer->getTotal(),
                                        _buffer->getBar(),
                                        _buffer->getRows());
    }
}

void Terminal::write(const uint8_t * data, size_t size) {
    if (_dumpWrites) { return; }

    if (_writeBuffer.empty()) {
        // Try to write it now, queue what we can't write.
        try {
            while (size != 0) {
                auto rval = _tty.write(data, size);
                if (rval == 0) { PRINT("Write would block!"); break; }
                data += rval; size -= rval;
            }

            // Copy remainder (if any) to queue
            if (size != 0) {
                auto oldSize = _writeBuffer.size();
                _writeBuffer.resize(oldSize + size);
                std::copy(data, data + size, &_writeBuffer[oldSize]);
            }
        }
        catch (const I_Tty::Error &) {
            _dumpWrites = true;
            _writeBuffer.clear();
        }
    }
    else {
        // Just copy it to the queue.
        // We are assuming the write() would still block. Wait for a flush().
        auto oldSize = _writeBuffer.size();
        _writeBuffer.resize(oldSize + size);
        std::copy(data, data + size, &_writeBuffer[oldSize]);
    }
}

void Terminal::resetAll() {
    _buffer->clear();

    _modes.clear();
    _modes.set(Mode::AUTO_WRAP);
    _modes.set(Mode::SHOW_CURSOR);
    _modes.set(Mode::AUTO_REPEAT);
    _modes.set(Mode::ALT_SENDS_ESC);

    for (size_t i = 0; i != _tabs.size(); ++i) {
        _tabs[i] = (i + 1) % TAB_SIZE == 0;
    }

    _cursor.reset();
    _savedCursor.reset();

    _observer.terminalResetTitle();
}

void Terminal::processRead(const uint8_t * data, size_t size) {
    for (size_t i = 0; i != size; ++i) {
        switch (_utf8Machine.consume(data[i])) {
            case utf8::Machine::State::ACCEPT:
                processChar(_utf8Machine.seq(), _utf8Machine.length());
                ASSERT(_cursor.pos.row < _buffer->getRows(), "");
                ASSERT(_cursor.pos.col < _buffer->getCols(), "");
                break;
            case utf8::Machine::State::REJECT:
                ERROR("Rejecting UTF-8 data.");
                break;
            default:
                break;
        }
    }
}

void Terminal::processChar(utf8::Seq seq, utf8::Length length) {
    _vtMachine.consume(seq, length);

    if (_config.getSyncTty()) {        // FIXME too often, may not have been a buffer change.
        fixDamage(Pos(0, 0),
                  Pos(_buffer->getRows(), _buffer->getCols()),
                  Damager::TTY);
    }
}

void Terminal::machineNormal(utf8::Seq seq, utf8::Length length) throw () {
    if (length == utf8::Length::L1) {
        translate(seq.lead(), seq);
    }

    if (_config.getTraceTty()) {
        std::cerr << Esc::FG_GREEN << Esc::UNDERLINE << seq << Esc::RESET;
    }

    if (_cursor.wrapNext && _modes.get(Mode::AUTO_WRAP)) {
        moveCursor(_cursor.pos.atCol(0));

        if (_cursor.pos.row == _buffer->getMarginEnd() - 1) {
            _buffer->addLine();
        }
        else {
            moveCursor(_cursor.pos.down());
        }
    }

    ASSERT(_cursor.pos.col < _buffer->getCols(), "");
    ASSERT(_cursor.pos.row < _buffer->getRows(), "");

    if (_modes.get(Mode::INSERT)) {
        _buffer->insertCells(_cursor.pos, 1);
    }

    _buffer->setCell(_cursor.pos, Cell::utf8(seq, _cursor.style));

    if (_cursor.pos.col == _buffer->getCols() - 1) {
        _cursor.wrapNext = true;
    }
    else {
        moveCursor(_cursor.pos.right());
    }

    ASSERT(_cursor.pos.col < _buffer->getCols(), "");
}

void Terminal::machineControl(uint8_t c) throw () {
    if (_config.getTraceTty()) {
        std::cerr << Esc::FG_YELLOW << Char(c) << Esc::RESET;
    }

    switch (c) {
        case BEL:
            PRINT("BEL!!");
            break;
        case HT:
            tabCursor(TabDir::FORWARD, 1);
            break;
        case BS:
            if (_cursor.wrapNext) {
                _cursor.wrapNext = false;
            }
            else {
                if (_cursor.pos.col == 0) {
                    if (_modes.get(Mode::AUTO_WRAP)) {
                        if (_cursor.pos.row > _buffer->getMarginBegin()) {
                            moveCursor(_cursor.pos.up().atCol(_buffer->getCols() - 1));
                        }
                    }
                }
                else {
                    moveCursor(_cursor.pos.left());
                }
            }
            break;
        case CR:
            moveCursor(_cursor.pos.atCol(0));
            break;
        case LF:
            if (_modes.get(Mode::CR_ON_LF)) {
                moveCursor(_cursor.pos.atCol(0));
            }
            // Fall-through:
        case FF:
        case VT:
            if (_cursor.pos.row == _buffer->getMarginEnd() - 1) {
                if (_config.getTraceTty()) {
                    std::cerr << "(ADDLINE1)" << std::endl;
                }
                _buffer->addLine();
            }
            else {
                moveCursor(_cursor.pos.down());
            }

            if (_config.getTraceTty()) {
                std::cerr << std::endl;
            }
            break;
        case SO:
            // XXX dubious
            _cursor.cs = &_cursor.g1;
            break;
        case SI:
            // XXX dubious
            _cursor.cs = &_cursor.g0;
            break;
        case CAN:
        case SUB:
            // XXX reset escape states - assertion currently prevents us getting here
            break;
        case ENQ:
        case NUL:
        case DC1:    // DC1/XON
        case DC3:    // DC3/XOFF
            break;
        default:
            PRINT("Ignored control char: " << static_cast<int>(c));
            break;
    }
}

void Terminal::machineEscape(uint8_t c) throw () {
    if (_config.getTraceTty()) {
        std::cerr << Esc::FG_MAGENTA << "ESC" << Char(c) << Esc::RESET << " ";
    }

    switch (c) {
        case 'D':   // IND - Line Feed (opposite of RI)
            // FIXME still dubious
            if (_cursor.pos.row == _buffer->getMarginEnd() - 1) {
                if (_config.getTraceTty()) { std::cerr << "(ADDLINE2)" << std::endl; }
                _buffer->addLine();
            }
            else {
                moveCursor(_cursor.pos.down());
            }
            break;
        case 'E':   // NEL - Next Line
            // FIXME still dubious
            moveCursor(Pos(_cursor.pos.row, 0));
            if (_cursor.pos.row == _buffer->getMarginEnd() - 1) {
                if (_config.getTraceTty()) { std::cerr << "(ADDLINE3)" << std::endl; }
                _buffer->addLine();
            }
            else {
                moveCursor(_cursor.pos.down());
            }
            break;
        case 'H':   // HTS - Horizontal Tab Stop
            _tabs[_cursor.pos.col] = true;
            break;
        case 'M':   // RI - Reverse Line Feed (opposite of IND)
            // FIXME still dubious
            if (_cursor.pos.row == _buffer->getMarginBegin()) {
                _buffer->insertLines(_buffer->getMarginBegin(), 1);
            }
            else {
                moveCursor(_cursor.pos.up());
            }
            break;
        case 'N':   // SS2 - Set Single Shift 2
            NYI("SS2");
            break;
        case 'O':   // SS3 - Set Single Shift 3
            NYI("SS3");
            break;
        case 'Z':   // DECID - Identify Terminal
            NYI("st.c:2194");
            //ttywrite(VT102ID, sizeof(VT102ID) - 1);
            break;
        case 'c':   // RIS - Reset to initial state
            resetAll();
            break;
        case '=':   // DECKPAM - Keypad Application Mode
            _modes.set(Mode::APPKEYPAD);
            break;
        case '>':   // DECKPNM - Keypad Numeric Mode
            _modes.unset(Mode::APPKEYPAD);
            break;
        case '7':   // DECSC - Save Cursor
            _savedCursor = _cursor;
            break;
        case '8':   // DECRC - Restore Cursor
            damageCursor();
            _cursor = _savedCursor;
            break;
        default:
            ERROR("Unknown escape sequence: ESC" << Char(c));
            break;
    }
}

void Terminal::machineCsi(bool priv,
                          const std::vector<int32_t> & args,
                          uint8_t mode) throw () {
    if (_config.getTraceTty()) {
        std::cerr << Esc::FG_CYAN << "ESC[";
        if (priv) { std::cerr << '?'; }
        auto first = true;
        for (auto & a : args) {
            if (first) { first = false; }
            else       { std::cerr << ';'; }
            std::cerr << a;
        }
        std::cerr << mode << Esc::RESET << ' ';
    }

    switch (mode) {
        case '@': { // ICH - Insert Character
            // XXX what about _cursor.wrapNext
            auto count = nthArgNonZero(args, 0, 1);
            count = clamp(count, 1, _buffer->getCols() - _cursor.pos.col);
            _buffer->insertCells(_cursor.pos, count);
            break;
        }
        case 'A': // CUU - Cursor Up
            moveCursor(_cursor.pos.up(nthArgNonZero(args, 0, 1)));
            break;
        case 'B': // CUD - Cursor Down
            moveCursor(_cursor.pos.down(nthArgNonZero(args, 0, 1)));
            break;
        case 'C': // CUF - Cursor Forward
            moveCursor(_cursor.pos.right(nthArgNonZero(args, 0, 1)));
            break;
        case 'D': // CUB - Cursor Backward
            moveCursor(_cursor.pos.left(nthArgNonZero(args, 0, 1)));
            break;
        case 'E': // CNL - Cursor Next Line
            moveCursor(Pos(_cursor.pos.row + nthArgNonZero(args, 0, 1), 0));
            break;
        case 'F': // CPL - Cursor Preceding Line
            moveCursor(Pos(_cursor.pos.row - nthArgNonZero(args, 0, 1), 0));
            break;
        case 'G': // CHA - Cursor Horizontal Absolute
            moveCursor(_cursor.pos.atCol(nthArgNonZero(args, 0, 1) - 1));
            break;
        case 'f':       // HVP - Horizontal and Vertical Position
        case 'H':       // CUP - Cursor Position
            if (_config.getTraceTty()) { std::cerr << std::endl; }
            moveCursorOriginMode(Pos(nthArg(args, 0, 1) - 1, nthArg(args, 1, 1) - 1));
            break;
        case 'I':       // CHT - Cursor Forward Tabulation *
            tabCursor(TabDir::FORWARD, nthArg(args, 0, 1));
            break;
        case 'J': // ED - Erase Data
            // Clear screen.
            switch (nthArg(args, 0)) {
                default:
                case 0: // ED0 - Below
                    _buffer->clearLineRight(_cursor.pos);
                    _buffer->clearBelow(_cursor.pos.row + 1);
                    break;
                case 1: // ED1 - Above
                    _buffer->clearAbove(_cursor.pos.row);
                    _buffer->clearLineLeft(_cursor.pos.right());
                    break;
                case 2: // ED2 - All
                    _buffer->clear();
                    moveCursor(Pos());      // XXX moveCursorOriginMode???
                    break;
            }
            break;
        case 'K':   // EL - Erase line
            switch (nthArg(args, 0)) {
                default:
                case 0: // EL0 - Right (inclusive of cursor position)
                    _buffer->clearLineRight(_cursor.pos);
                    break;
                case 1: // EL1 - Left (inclusive of cursor position)
                    _buffer->clearLineLeft(_cursor.pos.right());
                    break;
                case 2: // EL2 - All
                    _buffer->clearLine(_cursor.pos.row);
                    break;
            }
            break;
        case 'L': // IL - Insert Lines
            if (_cursor.pos.row >= _buffer->getMarginBegin() &&
                _cursor.pos.row <  _buffer->getMarginEnd())
            {
                auto count = nthArgNonZero(args, 0, 1);
                count = std::min(count, _buffer->getMarginEnd() - _cursor.pos.row);
                _buffer->insertLines(_cursor.pos.row, count);
            }
            break;
        case 'M': // DL - Delete Lines
            if (_cursor.pos.row >= _buffer->getMarginBegin() &&
                _cursor.pos.row <  _buffer->getMarginEnd())
            {
                auto count = nthArgNonZero(args, 0, 1);
                count = std::min(count, _buffer->getMarginEnd() - _cursor.pos.row);
                _buffer->eraseLines(_cursor.pos.row, count);
            }
            break;
        case 'P': { // DCH - Delete Character
            // FIXME what about wrap-next?
            auto count = nthArgNonZero(args, 0, 1);
            count = std::min(count, _buffer->getCols() - _cursor.pos.col);
            _buffer->eraseCells(_cursor.pos, count);
            break;
        }
        case 'S': // SU - Scroll Up
            _buffer->scrollUpMargins(nthArgNonZero(args, 0, 1));
            break;
        case 'T': // SD - Scroll Down
            _buffer->scrollDownMargins(nthArgNonZero(args, 0, 1));
            break;
        case 'X': // ECH - Erase Char
            _buffer->setCells(_cursor.pos, nthArgNonZero(args, 0, 1),
                              Cell::ascii(SPACE, _cursor.style));
            break;
        case 'Z': // CBT - Cursor Backward Tabulation
            tabCursor(TabDir::BACKWARD, nthArgNonZero(args, 0, 1));
            break;
        case '`': // HPA
            moveCursor(_cursor.pos.atCol(nthArgNonZero(args, 0, 1) - 1));
            break;
        case 'b': // REP
            NYI("REP");
            break;
        case 'c': // Primary DA
            write(reinterpret_cast<const uint8_t *>("\x1B[?6c"), 5);
            break;
        case 'd': // VPA - Vertical Position Absolute
            moveCursorOriginMode(_cursor.pos.atRow(nthArg(args, 0, 1) - 1));
            break;
        case 'g': // TBC
            switch (nthArg(args, 0, 0)) {
                case 0:
                    // "the character tabulation stop at the active presentation"
                    // "position is cleared"
                    _tabs[_cursor.pos.col] = false;
                    break;
                case 1:
                    // "the line tabulation stop at the active line is cleared"
                    NYI("");
                    break;
                case 2:
                    // "all character tabulation stops in the active line are cleared"
                    NYI("");
                    break;
                case 3:
                    // "all character tabulation stops are cleared"
                    std::fill(_tabs.begin(), _tabs.end(), false);
                    break;
                case 4:
                    // "all line tabulation stops are cleared"
                    NYI("");
                    break;
                case 5:
                    // "all tabulation stops are cleared"
                    NYI("");
                    break;
                default:
                    goto default_;
            }
            break;
        case 'h': // SM
            //PRINT("CSI: Set terminal mode: " << strArgs(args));
            processModes(priv, true, args);
            break;
        case 'l': // RM
            //PRINT("CSI: Reset terminal mode: " << strArgs(args));
            processModes(priv, false, args);
            break;
        case 'm': // SGR - Select Graphic Rendition
            if (args.empty()) {
                std::vector<int32_t> args2;
                args2.push_back(0);
                processAttributes(args2);
            }
            else {
                processAttributes(args);
            }
            break;
        case 'n': // DSR - Device Status Report
            if (args.empty()) {
                // QDC - Query Device Code
                // RDC - Report Device Code: <ESC>[{code}0c
                NYI("What code should I send?");
            }
            else {
                switch (nthArg(args, 0)) {
                    case 5: {
                        // QDS - Query Device Status
                        // RDO - Report Device OK: <ESC>[0n
                        std::ostringstream ost;
                        ost << ESC << "[0n";
                        const auto & str = ost.str();
                        _writeBuffer.insert(_writeBuffer.begin(), str.begin(), str.end());
                        break;
                    }
                    case 6: {
                        // QCP - Query Cursor Position
                        // RCP - Report Cursor Position

                        // XXX Is cursor position reported absolute irrespective of
                        // origin-mode.

                        auto row = _cursor.pos.row;
                        auto col = _cursor.pos.col;

                        if (_cursor.originMode) { row -= _buffer->getMarginBegin(); }

                        std::ostringstream ost;
                        ost << ESC << '[' << row + 1 << ';' << col + 1 << 'R';
                        const auto & str = ost.str();
                        _writeBuffer.insert(_writeBuffer.begin(), str.begin(), str.end());
                    }
                    case 7: {
                        // Ps = 7   Request Display Name
                        NYI("");
                        break;
                    }
                    case 8: {
                        // Ps = 8   Request Version Number (place in window title)
                        NYI("");
                        break;
                    }
                    default:
                        NYI("");
                        break;
                }
            }
            break;
        case 'q': // DECSCA - Select Character Protection Attribute
            // OR IS THIS DECLL0/DECLL1/etc
            NYI("DECSCA");
            break;
        case 'r': // DECSTBM - Set Top and Bottom Margins (scrolling)
            if (priv) {
                goto default_;
            }
            else {
                if (args.empty()) {
                    _buffer->resetMargins();
                    moveCursorOriginMode(Pos());
                }
                else {
                    // http://www.vt100.net/docs/vt510-rm/DECSTBM
                    auto top    = nthArgNonZero(args, 0, 1) - 1;
                    auto bottom = nthArgNonZero(args, 1, _cursor.pos.row + 1) - 1;

                    top    = clamp<int32_t>(top,    0, _buffer->getRows() - 1);
                    bottom = clamp<int32_t>(bottom, 0, _buffer->getRows() - 1);

                    if (bottom > top) {
                        _buffer->setMargins(top, bottom + 1);
                    }
                    else {
                        _buffer->resetMargins();
                    }

                    moveCursorOriginMode(Pos());
                }
            }
            break;
        case 's': // save cursor
            _savedCursor.pos = _cursor.pos;
            break;
        case 't': // window ops?
            // FIXME see 'Window Operations' in man 7 urxvt.
            NYI("Window ops");
            break;
        case 'u': // restore cursor
            moveCursor(_savedCursor.pos);
            break;
        case 'y': // DECTST
            NYI("DECTST");
            break;
default_:
        default:
            PRINT("NYI:CSI: ESC  ??? " /*<< Str(seq)*/);
            break;
    }
}

void Terminal::machineDcs(const std::vector<uint8_t> & seq) throw () {
    if (_config.getTraceTty()) {
        std::cerr << Esc::FG_RED << "ESC" << Str(seq) << Esc::RESET << ' ';
    }
}

void Terminal::machineOsc(const std::vector<std::string> & args) throw () {
    if (_config.getTraceTty()) {
        std::cerr << Esc::FG_MAGENTA << "ESC";
        for (const auto & a : args) { std::cerr << a << ';'; }
        std::cerr << Esc::RESET << " ";
    }

    if (!args.empty()) {
        switch (unstringify<int>(args[0])) {
            case 0: // Icon name and window title
            case 1: // Icon label
            case 2: // Window title
                if (args.size() > 1) {
                    _observer.terminalSetTitle(args[1]);
                }
                break;
            case 55:
                NYI("Log history to file");
                break;
            default:
                // TODO consult http://rtfm.etla.org/xterm/ctlseq.html AND man 7 urxvt.
                PRINT("Unandled: OSC");
                for (const auto & a : args) {
                    PRINT(a);
                }
                break;
        }
    }
}

void Terminal::machineSpecial(uint8_t special, uint8_t code) throw () {
    if (_config.getTraceTty()) {
        std::cerr << Esc::FG_BLUE << "ESC" << Char(special) << Char(code) << Esc::RESET << " ";
    }

    switch (special) {
        case '#':
            switch (code) {
                case '3':   // DECDHL - Double height/width (top half of char)
                    NYI("Double height (top)");
                    break;
                case '4':   // DECDHL - Double height/width (bottom half of char)
                    NYI("Double height (bottom)");
                    break;
                case '5':   // DECSWL - Single height/width
                    break;
                case '6':   // DECDWL - Double width
                    NYI("Double width");
                    break;
                case '8': { // DECALN - Alignment
                    // Fill terminal with 'E'
                    Cell cell = Cell::ascii('E', _cursor.style);
                    for (uint16_t r = 0; r != _buffer->getRows(); ++r) {
                        for (uint16_t c = 0; c != _buffer->getCols(); ++c) {
                            _buffer->setCell(Pos(r, c), cell);
                        }
                    }
                    break;
                }
                default:
                    NYI("?");
                    break;
            }
            break;
        case '(':
            switch (code) {
                case '0': // set specg0
                    _cursor.g0 = CS_SPECIAL;
                    break;
                case '1': // set altg0
                    NYI("Alternate Character rom");
                    break;
                case '2': // set alt specg0
                    NYI("Alternate Special Character rom");
                    break;
                case 'A': // set ukg0
                    _cursor.g0 = CS_UK;
                    break;
                case 'B': // set usg0
                    _cursor.g0 = CS_US;
                    break;
                case '<': // Multinational character set
                    NYI("Multinational character set");
                    break;
                case '5': // Finnish
                    NYI("Finnish 1");
                    break;
                case 'C': // Finnish
                    NYI("Finnish 2");
                    break;
                case 'K': // German
                    NYI("German");
                    break;
                default:
                    NYI("Unknown character set: " << code);
                    break;
            }
            break;
        case ')':
            switch (code) {
                case '0': // set specg1
                    PRINT("\nG1 = SPECIAL");
                    _cursor.g1 = CS_SPECIAL;
                    break;
                case '1': // set altg1
                    NYI("Alternate Character rom");
                    break;
                case '2': // set alt specg1
                    NYI("Alternate Special Character rom");
                    break;
                case 'A': // set ukg0
                    PRINT("\nG1 = UK");
                    _cursor.g1 = CS_UK;
                    break;
                case 'B': // set usg0
                    PRINT("\nG1 = US");
                    _cursor.g1 = CS_US;
                    break;
                case '<': // Multinational character set
                    NYI("Multinational character set");
                    break;
                case '5': // Finnish
                    NYI("Finnish 1");
                    break;
                case 'C': // Finnish
                    NYI("Finnish 2");
                    break;
                case 'K': // German
                    NYI("German");
                    break;
                default:
                    NYI("Unknown character set: " << code);
                    break;
            }
            break;
        default:
            NYI("Special: " /*<< Str(seq)*/);
            break;
    }
}

void Terminal::processAttributes(const std::vector<int32_t> & args) {
    ASSERT(!args.empty(), "");

    // FIXME check man 7 urxvt:

    // FIXME is it right to loop?
    for (size_t i = 0; i != args.size(); ++i) {
        auto v = args[i];

        switch (v) {
            case 0: // Reset/Normal
                _cursor.style = Style::normal();
                break;
            case 1: // Bold
                _cursor.style.attrs.set(Attr::BOLD);
                // Normal -> Bright.
                //if (_cursor.style.fg < 8) { _cursor.style.fg += 8; }
                break;
            case 2: // Faint (low/decreased intensity)
                _cursor.style.attrs.unset(Attr::BOLD);
                // Bright -> Normal.
                //if (_cursor.style.fg >= 8 && _cursor.style.fg < 16) { _cursor.style.fg -= 8; }
                break;
            case 3: // Italic: on
                _cursor.style.attrs.set(Attr::ITALIC);
                break;
            case 4: // Underline: Single
                _cursor.style.attrs.set(Attr::UNDERLINE);
                break;
            case 5: // Blink: slow
            case 6: // Blink: rapid
                _cursor.style.attrs.set(Attr::BLINK);
                break;
            case 7: // Image: Negative
                _cursor.style.attrs.set(Attr::INVERSE);
                break;
            case 8: // Conceal (not widely supported)
                _cursor.style.attrs.set(Attr::CONCEAL);
                break;
            case 9: // Crossed-out (not widely supported)
                NYI("Crossed-out");
                break;
            case 10: // Primary (default) font
                NYI("Primary (default) font");
                break;
            case 11: // 1st alternative font
            case 12:
            case 13:
            case 14:
            case 15:
            case 16:
            case 17:
            case 18:
            case 19: // 9th alternative font
                NYI(nthStr(v - 10) << " alternative font");
                break;
            case 20: // Fraktur (hardly ever supported)
                NYI("Fraktur");
                break;
            case 21:
                // Bold: off or Underline: Double (bold off not widely supported,
                //                                 double underline hardly ever)
                _cursor.style.attrs.unset(Attr::BOLD);
                // Bright -> Normal.
                //if (_cursor.style.fg >= 8 && _cursor.style.fg < 16) { _cursor.style.fg -= 8; }
                break;
            case 22: // Normal color or intensity (neither bold nor faint)
                _cursor.style.attrs.unset(Attr::BOLD);
                // Bright -> Normal.        XXX is this right?
                //if (_cursor.style.fg >= 8 && _cursor.style.fg < 16) { _cursor.style.fg -= 8; }
                break;
            case 23: // Not italic, not Fraktur
                _cursor.style.attrs.unset(Attr::ITALIC);
                break;
            case 24: // Underline: None (not singly or doubly underlined)
                _cursor.style.attrs.unset(Attr::UNDERLINE);
                break;
            case 25: // Blink: off
                _cursor.style.attrs.unset(Attr::BLINK);
                break;
            case 26: // Reserved?
                _cursor.style.attrs.set(Attr::INVERSE);
                break;
            case 27: // Image: Positive
                _cursor.style.attrs.unset(Attr::INVERSE);
                break;
            case 28: // Reveal (conceal off - not widely supported)
                _cursor.style.attrs.unset(Attr::CONCEAL);
                break;
            case 29: // Not crossed-out (not widely supported)
                NYI("Crossed-out");
                break;
                // 30..37 (set foreground colour - handled separately)
            case 38:
                // https://github.com/robertknight/konsole/blob/master/user-doc/README.moreColors
                if (i + 1 < args.size()) {
                    i += 1;
                    switch (args[i]) {
                        case 0:
                            NYI("User defined foreground");
                            break;
                        case 1:
                            NYI("Transparent foreground");
                            break;
                        case 2:
                            if (i + 3 < args.size()) {
                                // 24-bit foreground support
                                // ESC[ … 48;2;<r>;<g>;<b> … m Select RGB foreground color
                                _cursor.style.fg = UColor(args[i + 1], args[i + 2], args[i + 3]);
                                i += 3;     // FIXME 4??
                            }
                            else {
                                ERROR("Insufficient args");
                                i = args.size() - 1;
                            }
                            break;
                        case 3:
                            if (i + 3 < args.size()) {
                                NYI("24-bit CMY foreground");
                                i += 3;
                            }
                            else {
                                ERROR("Insufficient args");
                                i = args.size() - 1;
                            }
                            break;
                        case 4:
                            if (i + 4 < args.size()) {
                                NYI("24-bit CMYK foreground");
                                i += 4;
                            }
                            else {
                                ERROR("Insufficient args");
                                i = args.size() - 1;
                            }
                            break;
                        case 5:
                            if (i + 1 < args.size()) {
                                i += 1;
                                auto v2 = args[i];
                                if (v2 >= 0 && v2 < 256) {
                                    _cursor.style.fg.index = v2;
                                }
                                else {
                                    ERROR("Colour out of range: " << v2);
                                }
                            }
                            else {
                                ERROR("Insufficient args");
                                i = args.size() - 1;
                            }
                            break;
                        default:
                            NYI("Unknown?");
                    }
                }
                break;
            case 39:
                _cursor.style.fg = Style::defaultFg();
                break;
                // 40..47 (set background colour - handled separately)
            case 48:
                // https://github.com/robertknight/konsole/blob/master/user-doc/README.moreColors
                if (i + 1 < args.size()) {
                    i += 1;
                    switch (args[i]) {
                        case 0:
                            NYI("User defined background");
                            break;
                        case 1:
                            NYI("Transparent background");
                            break;
                        case 2:
                            if (i + 3 < args.size()) {
                                // 24-bit background support
                                // ESC[ … 48;2;<r>;<g>;<b> … m Select RGB background color
                                _cursor.style.bg = UColor(args[i + 1], args[i + 2], args[i + 3]);
                                i += 3;
                            }
                            else {
                                ERROR("Insufficient args");
                                i = args.size() - 1;
                            }
                            break;
                        case 3:
                            if (i + 3 < args.size()) {
                                NYI("24-bit CMY background");
                                i += 3;
                            }
                            else {
                                ERROR("Insufficient args");
                                i = args.size() - 1;
                            }
                            break;
                        case 4:
                            if (i + 4 < args.size()) {
                                NYI("24-bit CMYK background");
                                i += 4;
                            }
                            else {
                                ERROR("Insufficient args");
                                i = args.size() - 1;
                            }
                            break;
                        case 5:
                            if (i + 1 < args.size()) {
                                i += 1;
                                auto v2 = args[i];
                                if (v2 >= 0 && v2 < 256) {
                                    _cursor.style.bg.index = v2;
                                }
                                else {
                                    ERROR("Colour out of range: " << v2);
                                }
                            }
                            else {
                                ERROR("Insufficient args");
                                i = args.size() - 1;
                            }
                            break;
                        default:
                            NYI("Unknown?");
                    }
                }
                break;
            case 49:
                _cursor.style.bg = Style::defaultBg();
                break;
                // 50 Reserved
            case 51: // Framed
                NYI("Framed");
                break;
            case 52: // Encircled
                NYI("Encircled");
                break;
            case 53: // Overlined
                NYI("Overlined");
                break;
            case 54: // Not framed or encircled
                NYI("Not framed or encircled");
                break;
            case 55: // Not overlined
                NYI("Not overlined");
            case 99:
                NYI("Default BRIGHT fg");
                break;
            case 109:
                NYI("Default BRIGHT bg");
                break;

            default:
                // 56..59 Reserved
                // 60..64 (ideogram stuff - hardly ever supported)
                // 90..97 Set foreground colour high intensity - handled separately
                // 100..107 Set background colour high intensity - handled separately

                if (v >= 30 && v < 38) {
                    // normal fg
                    _cursor.style.fg.index = v - 30;
                    // BOLD -> += 8
                }
                else if (v >= 40 && v < 48) {
                    // normal bg
                    _cursor.style.bg.index = v - 40;
                }
                else if (v >= 90 && v < 98) {
                    // bright fg
                    _cursor.style.fg.index = v - 90 + 8;
                }
                else if (v >= 100 && v < 108) {
                    // bright bg
                    _cursor.style.bg.index = v - 100 + 8;
                }
                else if (v >= 256 && v < 512) {
                    _cursor.style.fg.index = v - 256;
                }
                else if (v >= 512 && v < 768) {
                    _cursor.style.bg.index = v - 512;
                }
                else {
                    ERROR("Unhandled attribute: " << v);
                }
                break;
        }
    }
}

void Terminal::processModes(bool priv, bool set, const std::vector<int32_t> & args) {
    //PRINT("processModes: priv=" << priv << ", set=" << set << ", args=" << strArgs(args));

    for (auto a : args) {
        if (priv) {
            switch (a) {
                case 1: // DECCKM - Cursor Keys Mode - Application / Cursor
                    _modes.setTo(Mode::APPCURSOR, set);
                    break;
                case 2: // DECANM - ANSI/VT52 Mode
                    NYI("DECANM: " << set);
                    _cursor.g0 = CS_US;
                    _cursor.g1 = CS_US;
                    _cursor.cs = &_cursor.g0;
                    break;
                case 3: // DECCOLM - Column Mode
                    if (set) {
                        // resize 132x24
                        _observer.terminalResizeBuffer(24, 132);
                    }
                    else {
                        // resize 80x24
                        _observer.terminalResizeBuffer(24, 80);
                    }
                    break;
                case 4: // DECSCLM - Scroll Mode - Smooth / Jump (IGNORED)
                    NYI("DECSCLM: " << set);
                    break;
                case 5: // DECSCNM - Screen Mode - Reverse / Normal
                    if (_modes.get(Mode::REVERSE) != set) {
                        _modes.setTo(Mode::REVERSE, set);
                        _buffer->damageAll();
                    }
                    break;
                case 6: // DECOM - Origin Mode - Relative / Absolute
                    _cursor.originMode = set;
                    moveCursorOriginMode(Pos());
                    break;
                case 7: // DECAWM - Auto Wrap Mode
                    _modes.setTo(Mode::AUTO_WRAP, set);
                    break;
                case 8: // DECARM - Auto Repeat Mode
                    _modes.setTo(Mode::AUTO_REPEAT, set);
                    break;
                case 9: // DECINLM - Interlacing Mode
                    NYI("DECINLM");
                    break;
                case 12: // CVVIS/att610 - Cursor Very Visible.
                    //NYI("CVVIS/att610: " << set);
                    break;
                case 18: // DECPFF - Printer feed (IGNORED)
                case 19: // DECPEX - Printer extent (IGNORED)
                    NYI("DECPFF/DECPEX: " << set);
                    break;
                case 25: // DECTCEM - Text Cursor Enable Mode
                    _modes.setTo(Mode::SHOW_CURSOR, set);
                    break;
                case 40:
                    // Allow column
                    break;
                case 42: // DECNRCM - National characters (IGNORED)
                    NYI("Ignored: "  << a << ", " << set);
                    break;
                case 1000: // 1000,1002: enable xterm mouse report
                    _modes.setTo(Mode::MOUSE_BUTTON, set);
                    _modes.setTo(Mode::MOUSE_MOTION, false);
                    break;
                case 1002:
                    _modes.setTo(Mode::MOUSE_MOTION, set);
                    _modes.setTo(Mode::MOUSE_BUTTON, false);
                    break;
                case 1004:
                    // ??? tmux REPORT FOCUS
                    break;
                case 1005:
                    // ??? tmux MOUSE FORMAT = XTERM EXT
                    break;
                case 1006:
                    // MOUSE FORMAT = STR
                    _modes.setTo(Mode::MOUSE_SGR, set);
                    break;
                case 1015:
                    // MOUSE FORMAT = URXVT
                    break;
                case 1034: // ssm/rrm, meta mode on/off
                    NYI("1034: " << set);
                    break;
                case 1037: // deleteSendsDel
                    _modes.setTo(Mode::DELETE_SENDS_DEL, set);
                    break;
                case 1039: // altSendsEscape
                    _modes.setTo(Mode::ALT_SENDS_ESC, set);
                    break;
                case 1049: // rmcup/smcup, alternative screen
                case 47:   // XXX ???
                case 1047:
                    if (_buffer == &_altBuffer) {
                        _buffer->clear();
                    }

                    _buffer = set ? &_altBuffer : &_priBuffer;
                    if (a != 1049) {
                        _buffer->damageAll();
                        break;
                    }
                    // Fall-through:
                case 1048:
                    if (set) {
                        _savedCursor = _cursor;
                    }
                    else {
                        damageCursor();
                        _cursor = _savedCursor;
                    }

                    _buffer->damageAll();
                    break;
                case 2004:
                    _modes.setTo(Mode::BRACKETED_PASTE, set);
                    break;
                default:
                    ERROR("erresc: unknown private set/reset mode : " << a);
                    break;
            }
        }
        else {
            switch (a) {
                case 0:  // Error (IGNORED)
                    break;
                case 2:  // KAM - keyboard action
                    _modes.setTo(Mode::KBDLOCK, set);
                    break;
                case 4:  // IRM - Insertion-replacement
                    _modes.setTo(Mode::INSERT, set);
                    break;
                case 12: // SRM - Send/Receive
                    _modes.setTo(Mode::ECHO, set);      // XXX correct sense
                    break;
                case 20: // LNM - Linefeed/new line
                    _modes.setTo(Mode::CR_ON_LF, set);
                    break;
                default:
                    ERROR("erresc: unknown set/reset mode: " <<  a);
                    break;
            }
        }
    }
}

std::ostream & operator << (std::ostream & ost, Terminal::Button button) {
    switch (button) {
        case Terminal::Button::LEFT:
            return ost << "left";
        case Terminal::Button::MIDDLE:
            return ost << "middle";
        case Terminal::Button::RIGHT:
            return ost << "right";
    }

    FATAL("Unreachable");
}

std::ostream & operator << (std::ostream & ost, Terminal::ScrollDir dir) {
    switch (dir) {
        case Terminal::ScrollDir::UP:
            return ost << "up";
        case Terminal::ScrollDir::DOWN:
            return ost << "down";
    }

    FATAL("Unreachable");
}
