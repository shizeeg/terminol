// vi:noai:sw=4
// Copyright © 2013 David Bryant

#ifndef COMMON__BUFFER__HXX
#define COMMON__BUFFER__HXX

#include "terminol/common/data_types.hxx"
#include "terminol/common/config.hxx"
#include "terminol/common/deduper_interface.hxx"
#include "terminol/common/char_sub.hxx"
#include "terminol/support/async_destroyer.hxx"
#include "terminol/support/regex.hxx"

#include <deque>
#include <vector>
#include <iomanip>

// Buffer is the in-memory representation of the on-screen terminal data.
// Conceptually, the Buffer is just a grid of Cells, where a Cell is a description
// of a grid element, including the UTF-8 character at that location and its
// rendering style. The Buffer is made up of two regions: the "active" region (or
// non-scroll-back region) and the "historical" region (or scroll-back region).
// The key distinction here is that the active region is mutable, whereas the
// historical region is constant (note, historical content can become active again
// during resizes if the number of rows increases).
//
// Terminology:
// - Line: The contents of a single row.
// - Paragraph: The concatenated contents of one or more consecutive rows
//   where each subsequent row is a continuation of the previous.
//
// The data structures of the active region are essentially just a 2-dimensional
// array - the first dimension represents the rows and the second dimension represents
// the columns. Each element in the array is a Cell object.
// The active region is effectively an array of Lines.
//
// To facility low-overhead text reflow and deduplication, the data
// structures of the historical region are more elaborate. Firstly, historical
// data is stored as paragraphs, e.g. if some text is continued across three
// lines then the concatenation of those three lines is stored in the
// historical data.
// An additional data structure, HLine, allows historical data to be indexed
// (by row/column) by mapping the grid into segments of these paragraphs.
//
// During a reflowed-resize the HLines are invalidated but the paragraphs
// are not. The HLines must be rebuilt by re-traversing the paragraphs.
// Because the paragraphs are never invalidated (not even during resize)
// they are stored in a deduplicator object to reduce memory usage for large
// histories.
//
// The cost of representing the on-screen data in these two different ways
// is the complexity of harmonising access to them.
class Buffer {
    // APos (Absolute-Position) is a position identifier that is able to
    // refer to historical AND active lines.
    struct APos {
        int32_t row; // >= 0 --> _active, < 0 --> _history
        int16_t col;

        APos() : row(0), col(0) {}
        APos(int32_t row_, int16_t col_) : row(row_), col(col_) {}
        APos(Pos pos, uint32_t offset) : row(pos.row - offset), col(pos.col) {}
    };

    friend bool operator == (const APos & lhs, const APos & rhs) {
        return lhs.row == rhs.row && lhs.col == rhs.col;
    }

    friend bool operator != (const APos & lhs, const APos & rhs) {
        return !(lhs == rhs);
    }

    friend bool operator <  (const APos & lhs, const APos & rhs) {
        return
            (lhs.row <  rhs.row) ||
            (lhs.row == rhs.row && lhs.col < rhs.col);
    }

    friend std::ostream & operator << (std::ostream & ost, const APos & pos) {
        return ost << pos.row << 'x' << pos.col;
    }

    // HLine (or Historical-Line) represents a line of text in the historical region.
    // It can also be thought of as representing a segment of an unwrapped line.
    struct HLine {
        uint32_t index;             // index into _tags (adjusted by _lostTags)
        uint32_t seqnum;            // continuation number, 0 -> 1st line, 1 -> 2nd line, etc

        HLine(uint32_t index_, uint32_t seqnum_) : index(index_), seqnum(seqnum_) {}
    };

    // ALine (or Active-Line) represents a line of text in the active region.
    // An ALine directly contains its cells
    struct ALine {
        std::vector<Cell> cells;    // active lines have a greater/equal capacity to their wrap/size
        bool              cont;     // does this line continue on the next line?
        int16_t           wrap;     // wrappable index, <= cells.size()

        explicit ALine(int16_t cols, const Style & style = Style()) :
            cells(cols, Cell::blank(style)), cont(false), wrap(0) {}

        ALine(std::vector<Cell> & cells_, bool cont_, int16_t wrap_, int16_t cols) :
            cells(std::move(cells_)), cont(cont_), wrap(wrap_)
        {
            ASSERT(wrap_ <= cols, "");
            cells.resize(cols, Cell::blank());
        }

        void resize(int16_t cols) {
            ASSERT(cols > 0, "cols not positive.");
            cont = false;
            wrap = std::min(wrap, cols);
            cells.resize(cols, Cell::blank());
        }

        void clear(const Style & style) {
            cont = false;
            wrap = 0;
            std::fill(cells.begin(), cells.end(), Cell::blank(style));
        }

        bool isBlank() const {
            for (auto & c : cells) {
                if (c != Cell::blank()) { return false; }
            }
            return true;
        }
    };

    // Damage for a visible line (active or historical, but in the viewport)
    struct Damage {
        int16_t begin;      // inclusive
        int16_t end;        // exclusive

        // Initially there is no damage.
        Damage() : begin(0), end(0) {}

        // Explicitly specify the damage.
        void damageSet(int16_t begin_, int16_t end_) {
            ASSERT(begin_ <= end_, "");

            begin = begin_;
            end   = end_;
        }

        // Accumulate more damage.
        void damageAdd(int16_t begin_, int16_t end_) {
            ASSERT(begin_ <= end_, "");

            if (begin_ == end_) {
                // Do nothing.
            }
            else if (begin == end) {
                damageSet(begin_, end_);
            }
            else {
                begin = std::min(begin, begin_);
                end   = std::max(end,   end_);
            }
        }

        // Reset to initial state.
        void reset() {
            *this = Damage();
        }
    };

    // Cursor encompasses the state associated with a VT cursor.
    struct Cursor {
        Pos     pos;            // Current cursor position.
        Style   style;          // Current cursor style.
        bool    wrapNext;       // Flag indicating whether the next char wraps.
        CharSet charSet;        // Which CharSet is in use?

        Cursor() : pos(), style(), wrapNext(false), charSet(CharSet::G0) {}
    };

    struct SavedCursor {
        Cursor          cursor;
        const CharSub * charSub;

        SavedCursor() : cursor(), charSub(nullptr) {}
    };

    //
    //
    //

    class ParaIter {
        const Buffer &    _buffer;
        APos              _pos;
        std::vector<Cell> _cells;
        bool              _cont;
        int16_t           _wrap;
        bool              _valid;

    public:
        ParaIter(const Buffer & buffer, APos pos);

        bool valid() const { return _valid; }

        const APos & getPos() const { return _pos; }

        const Cell & getCell() const { return _cells[_pos.col]; }

        void moveForward();

        void moveBackward();
    };

    //
    //
    //

    class BufferIter {
        const Buffer & _buffer;
        int32_t        _row;
        bool           _valid;

    public:
        BufferIter(const Buffer & buffer, int32_t row);

        ParaIter getParaIter() const {
            ASSERT(_valid, "Invalid.");
            return ParaIter(_buffer, APos(_row, 0));
        }

        bool valid() const {
            return _valid;
        }

        void moveForward();

        void moveBackward();

    private:
        bool isStartOfPara();
    };

    //
    //
    //

    struct Search {
        Search(const Buffer & buffer, const std::string & pattern_) :
            iter(buffer, buffer.getRows() - 2),
            pattern(pattern_) {}

        BufferIter                              iter;
        std::string                             pattern;
        std::vector<std::vector<Regex::Substr>> allOffsets;
    };

    //
    //
    //

    const Config               & _config;
    I_Deduper                  & _deduper;
    I_Destroyer                & _destroyer;
    std::deque<I_Deduper::Tag>   _tags;             // The paragraph history.
    uint32_t                     _lostTags;         // Incremented for each _tags.pop_front().
    std::vector<Cell>            _pending;          // Paragraph pending to become historical.
    std::deque<HLine>            _history;          // Historical paragraph segments. Indexable.
    std::deque<ALine>            _active;           // Active paragraph segments. Indexable.
    std::vector<Damage>          _damage;           // Viewport-relative damage.
    std::vector<bool>            _tabs;             // Column-indexable, true if tab stop exists.
    uint32_t                     _scrollOffset;     // 0 -> scroll bottom
    uint32_t                     _historyLimit;     // Maximum number of historical paragraphs to keep.
    int16_t                      _cols;             // Current width of buffer.
    int16_t                      _marginBegin;      // Index of first row in margin (inclusive).
    int16_t                      _marginEnd;        // Index of last row in  margin (exclusive).
    bool                         _barDamage;        // Has the scrollbar been invalidated?
    APos                         _selectMark;       // Start of user selection.
    APos                         _selectDelim;      // End of user selection.
    Cursor                       _cursor;           // Current cursor.
    SavedCursor                  _savedCursor;      // Saved cursor.
    CharSubArray                 _charSubs;
    Search                     * _search;

public:
    class I_Renderer {
    public:
        virtual void bufferDrawBg(Pos     pos,
                                  int16_t count,
                                  UColor  color) = 0;
        virtual void bufferDrawFg(Pos             pos,
                                  int16_t         count,
                                  UColor          color,
                                  AttrSet         attrs,
                                  const uint8_t * str,       // nul-terminated
                                  size_t          size) = 0;
        virtual void bufferDrawCursor(Pos             pos,
                                      UColor          fg,
                                      UColor          bg,
                                      AttrSet         attrs,
                                      const uint8_t * str,    // nul-terminated, count 1
                                      size_t          size,
                                      bool            wrapNext) = 0;

    protected:
        ~I_Renderer() {}
    };


    Buffer(const Config       & config,
           I_Deduper          & deduper,
           I_Destroyer        & destroyer,
           int16_t              rows,
           int16_t              cols,
           uint32_t             historyLimit,
           const CharSubArray & charSubs);

    ~Buffer();

    int16_t  getRows() const { return static_cast<int16_t>(_active.size()); }
    int16_t  getCols() const { return _cols; }

    // How many _wrapped_ lines are there in the scroll-back history?
    uint32_t getHistoricalRows() const { return _history.size(); }
    // How many historical and active lines are there?
    uint32_t getTotalRows() const { return _history.size() + _active.size(); }
    // How many rows is viewport offset from the start of history?
    uint32_t getHistoryOffset() const { return _history.size() - _scrollOffset; }
    // How many rows is the viewport offset from the beginning of active?
    uint32_t getScrollOffset() const { return _scrollOffset; }
    // Is the bar damaged (does it need redrawing)?
    bool     getBarDamage() const { return _barDamage; }

    void markSelection(Pos pos);
    void delimitSelection(Pos pos, bool initial);
    void expandSelection(Pos pos, int level);
    void clearSelection();
    bool getSelectedText(std::string & text) const;

    void clearHistory();

    bool scrollUpHistory(uint16_t rows);

    bool scrollDownHistory(uint16_t rows);

    bool scrollTopHistory();

    bool scrollBottomHistory();

    Pos getCursorPos() const { return _cursor.pos; }

    void migrateFrom(Buffer & other, bool clear_);

    void write(utf8::Seq seq, bool autoWrap, bool insert);

    void backspace(bool autoWrap);

    void forwardIndex(bool resetCol = false);

    void reverseIndex();

    void setTab();

    void unsetTab();

    void clearTabs();

    void moveCursor(Pos pos, bool marginRelative = false);

    void moveCursor2(bool rowRelative, int16_t row,
                     bool colRelative, int16_t col);

    void saveCursor();

    void restoreCursor();

    void resizeClip(int16_t rows, int16_t cols);

    void resizeReflow(int16_t rows, int16_t cols);

    void tabForward(uint16_t count);

    void tabBackward(uint16_t count);

    void reset();

    void setMargins(int16_t begin, int16_t end);

    void resetMargins() {
        _marginBegin = 0;
        _marginEnd   = getRows();
    }

    void resetTabs() {
        for (size_t i = 0; i != _tabs.size(); ++i) {
            _tabs[i] = i % 8 == 0;
        }
    }

    void resetCursor() {
        _cursor.pos      = Pos();
        _cursor.wrapNext = false;
        // XXX should _cursor.charSet be reset?
        resetStyle();
    }

    void resetStyle() { _cursor.style = Style(); }

    void setAttr(Attr attr) { _cursor.style.attrs.set(attr); }

    void unsetAttr(Attr attr) { _cursor.style.attrs.unset(attr); }

    void setFg(const UColor & color) { _cursor.style.fg = color; }

    void setBg(const UColor & color) { _cursor.style.bg = color; }

    void insertCells(uint16_t n);

    void eraseCells(uint16_t n);

    void blankCells(uint16_t n);

    void clearLine();

    void clearLineLeft();

    void clearLineRight();

    void clear();

    void clearAbove();

    void clearBelow();

    void insertLines(uint16_t n);

    void eraseLines(uint16_t n);

    void scrollUpMargins(uint16_t n);

    void scrollDownMargins(uint16_t n);

    void damageViewport(bool scrollbar);

    void damageActive();

    void testPattern();

    void damageCell();

    void accumulateDamage(Region & damage) const;

    void dispatch(bool reverse, I_Renderer & renderer);

    void useCharSet(CharSet charSet);

    void setCharSub(CharSet charSet, const CharSub * charSub);

    const CharSub * getCharSub(CharSet charSet) const;

    bool isSearching() const { return _search; }
    void beginSearch(const std::string & pattern);
    const std::string & getSearchPattern() const;
    void setSearchPattern(const std::string & pattern);
    void nextSearch();
    void prevSearch();
    void endSearch();

    void dumpTags(std::ostream & ost) const;
    void dumpHistory(std::ostream & ost) const;
    void dumpActive(std::ostream & ost) const;
    void dumpSelection(std::ostream & ost) const;

protected:
    void getLine(int32_t row, std::vector<Cell> & cells,
                 bool & cont, int16_t & wrap) const;

    void dispatchBg(bool reverse, I_Renderer & renderer) const;
    void dispatchFg(bool reverse, I_Renderer & renderer) const;
    void dispatchCursor(bool reverse, I_Renderer & renderer) const;
    void dispatchSearch(bool reverse, I_Renderer & renderer) const;
    void resetDamage();

    void rebuildHistory();

    static bool isCellSelected(APos apos, APos begin, APos end, int16_t wrap);

    void testClearSelection(APos begin, APos end);

    bool normaliseSelection(APos & begin, APos & end) const;

    void insertLinesAt(int16_t row, uint16_t n);

    void eraseLinesAt(int16_t row, uint16_t n);

    bool marginsSet() const;

    void damageColumns(int16_t begin, int16_t end);

    void damageRows(int16_t begin, int16_t end);

    void damageSelection();

    void addLine();

    void bump();

    void unbump();

    void enforceHistoryLimit();
};

#endif // COMMON__BUFFER__HXX
