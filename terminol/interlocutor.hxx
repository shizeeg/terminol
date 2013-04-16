// vi:noai:sw=4

#ifndef INTERLOCUTOR__HXX
#define INTERLOCUTOR__HXX

#include "terminol/tty_interface.hxx"
#include "terminol/enums.hxx"
#include "terminol/utf8.hxx"
#include "terminol/common.hxx"

#include <vector>
#include <string>

class Interlocutor : protected Uncopyable {
public:
    typedef std::vector<std::string> Command;

    class I_Observer {
    public:
        // begin
        virtual void interBegin() throw () = 0;
        // control
        virtual void interControl(Control control) throw () = 0;
        // escapes
        virtual void interMoveCursor(uint16_t row, uint16_t col) throw () = 0;
        virtual void interRelMoveCursor(int16_t dRow, int16_t dCol) throw () = 0;
        virtual void interClearLine(ClearLine clear) throw () = 0;
        virtual void interClearScreen(ClearScreen clear) throw () = 0;
        virtual void interInsertChars(uint16_t num) throw () = 0;
        virtual void interInsertLines(uint16_t num) throw () = 0;
        virtual void interDeleteLines(uint16_t num) throw () = 0;
        virtual void interResetFg() throw () = 0;
        virtual void interResetBg() throw () = 0;
        virtual void interSetFg(uint8_t fg) throw () = 0;
        virtual void interSetBg(uint8_t bg) throw () = 0;
        virtual void interClearAttributes() throw () = 0;
        virtual void interSetAttribute(Attribute attribute, bool value) throw () = 0;
        virtual void interSetMode(Mode mode, bool value) throw () = 0;
        virtual void interSetTabStop() throw () = 0;
        virtual void interAdvanceTab(uint16_t count) throw () = 0;
        virtual void interSetScrollTopBottom(uint16_t row0, uint16_t row1) = 0;
        virtual void interSetScrollTop(uint16_t row) = 0;
        virtual void interResetAll() throw () = 0 ;
        virtual void interSetTitle(const std::string & title) throw () = 0;
        // UTF-8
        virtual void interUtf8(const char * s, size_t count, size_t size) throw () = 0;
        // queries
        virtual void interGetCursorPos(uint16_t & row, uint16_t & col) const throw () = 0;
        // child exited
        virtual void interChildExited(int exitCode) throw () = 0;
        // end
        virtual void interEnd() throw () = 0;

    protected:
        I_Observer() throw () {}
        ~I_Observer() throw () {}
    };

private:
    enum State {
        STATE_NORMAL,
        STATE_ESCAPE_START,
        STATE_CSI_ESCAPE,
        STATE_STR_ESCAPE,
        STATE_ESCAPE_START_STR,     // Same as STATE_ESCAPE_START but with unprocessed str.
        STATE_TEST_ESCAPE
    };

    friend std::ostream & operator << (std::ostream & ost, State state);

    I_Observer         & _observer;
    bool                 _dispatch;
    I_Tty              & _tty;
    bool                 _dumpWrites;
    State                _state;

    // TODO reconcile the storage for all escape sequence types.

    struct {
        std::string seq;
    }                    _escapeCsi;

    struct {
        char        type;
        std::string seq;
    }                    _escapeStr;

    std::vector<char>    _readBuffer;
    std::vector<char>    _writeBuffer;

public:
    Interlocutor(I_Observer & observer,
                 I_Tty      & tty);

    ~Interlocutor();

#if 0
    bool isOpen() const;

    // Only use the descriptor for select() - do not read/write().
    int  getFd();
#endif

    // Call when will not block (after select()).
    void read();

    // Queue data for write.
    void enqueueWrite(const char * data, size_t size);

    // Is there data queued for write?
    bool isWritePending() const;

    // Call when will not block (after select()).
    void write();

protected:
    void processBuffer();
    void processChar(const char * s, utf8::Length len);
    void processControl(char c);
    void processEscape(char c);
    void processCsiEscape();
    void processStrEscape();
    void processAttributes(const std::vector<int32_t> & args);
    void processModes(bool priv, bool set, const std::vector<int32_t> & args);

    void dumpCsiEscape() const;
    void dumpStrEscape() const;
};

//
//
//

std::ostream & operator << (std::ostream & ost, Interlocutor::State state);

#endif // INTERLOCUTOR__HXX
