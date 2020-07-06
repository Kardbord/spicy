// Copyright (c) 2020 by the Zeek Project. See LICENSE for details.

#pragma once

#include <hilti/rt/extension-points.h>
#include <hilti/rt/types/bytes.h>
#include <hilti/rt/types/reference.h>
#include <hilti/rt/types/stream.h>

#include <spicy/rt/debug.h>
#include <spicy/rt/parser.h>

namespace spicy::rt {

namespace sink {
enum class ReassemblerPolicy { First };

/**
 * Exception thrown when sink operations fail due to usage errors.
 */
class Error : public hilti::rt::UserException {
public:
    using hilti::rt::UserException::UserException;
};

} // namespace sink
namespace sink::detail {

/** Checks whether a given struct type corresponds to a unit that can be connected to a sink. */
template<typename T, typename = int>
struct supports_sinks : std::false_type {};

template<typename T>
struct supports_sinks<T, decltype((void)T::__sink, 0)> : std::true_type {};

/** State for a sink stored the unit it's connected to. */
struct State {
    /** Data being parsed. */
    hilti::rt::ValueReference<hilti::rt::Stream> data;

    /** Resumable parse function. */
    hilti::rt::Resumable resumable;

    Parser* parser;
};

/**
 * Helper function that kicks off parsing for a unit going to be connected to
 * a sink.
 */
template<typename U>
auto _connectUnit(UnitRef<U>& unit) {
    auto parse2 = std::any_cast<spicy::rt::Parse2Function<U>>(U::__parser.parse2);

    auto self = hilti::rt::ValueReference<U>::self(&*unit);

    auto& state = unit->__sink;
    state = new sink::detail::State();                   // NOLINT
    state->resumable = (*parse2)(self, state->data, {}); // Kick-off parsing with empty data.
    state->parser = &U::__parser;
    return state;
}

/**
 * Helper function that produces a function that, when called, kicks of
 * parsing for a freshly instantiated unit going to be connected to a sink.
 * This function will be stored in the `parser` object for the unit.
 */
template<typename U>
::spicy::rt::detail::ParseSinkFunction parseFunction() {
    return []() {
        auto unit = UnitRef<U>(U());
        return std::make_pair(std::move(unit), _connectUnit(unit));
    };
}

/**
 * Returns a function for storing in a unit's `Parser` instance that enables
 * the runtime library to run a specific hook, given a generic unit pointer.
 *
 * @tparam Unit unit type that the hook belongs to
 * @tparam Member function pointer to hook
 * @tparam Args hook parameters
 * @return function of type `void (hilti::rt::StrongReferenceGeneric, <args>`
 */
template<typename Unit, auto Hook, typename... Args>
auto hookFunction() {
    return [](const hilti::rt::StrongReferenceGeneric& u, Args&&... args) -> void {
        auto unit = u.as<Unit>();
        ((*unit).*Hook)(std::forward<Args>(args)...);
    };
}

// Name used as template parameter for sink's filter state. */
inline const char sink_name[] = "__sink__";
} // namespace sink::detail

/**
 * Runtime implementation for Spicy's `sink` type.
 *
 * Note: When adding/changing methods that generated code acceeses, adapt the
 * Spicy-side `spicy_rt::Sink` as well.
 */

class Sink {
public:
    Sink() { _init(); } // NOLINT(hicpp-member-init)
    ~Sink() { _close(true); }

    Sink(const Sink&) = delete;
    Sink(Sink&&) noexcept = default;
    Sink& operator=(const Sink&) = delete;
    Sink& operator=(Sink&&) noexcept = default;

    /**
     * Connects a unit instance to the sink. The unit will then receive any
     * data written into the sink.
     *
     * @param unit unit to connect to the sink.
     */
    template<typename T>
    void connect(spicy::rt::UnitRef<T> unit) {
        SPICY_RT_DEBUG_VERBOSE(hilti::rt::fmt("connecting parser %s [%p] to sink %p", T::__parser.name, &*unit, this));
        auto state = spicy::rt::sink::detail::_connectUnit(unit);
        _units.emplace_back(std::move(unit));
        _states.emplace_back(std::move(state));
    }

    /**
     * Connects a filter unit to the sink. Any input will then pass through the
     * filter before being forwarded tp parsing. Must not be called when data
     * has been processed already. Multiple filters can be connected and will be
     * chained.
     *
     * @param filter filter unit to connect to the sink.
     * @throws ``sink::Error`` if the type cannot be parsed
     */
    template<typename T>
    void connect_filter(spicy::rt::UnitRef<T> unit) {
        if ( _size )
            throw sink::Error("cannot connect filter after data has been forwarded already");

        SPICY_RT_DEBUG_VERBOSE(
            hilti::rt::fmt("connecting filter unit %s [%p] to sink %p", T::__parser.name, &*unit, this));
        spicy::rt::filter::connect(_filter, unit);
    }

    /**
     * Disconnects all units connected to the sink. They will then no longer
     * receive any data written into the sink.
     */
    void close() { _close(true); }

    /**
     * Connects new instances of all units to the sink that support a given
     * MIME type. The units will then all receive any data written into the
     * think.
     *
     * @param mt MIME type to connect units for
     */
    void connect_mime_type(const MIMEType& mt);

    /**
     * Connects new instances of all units to the sink that support a given
     * MIME type. The units will then all receive any data written into the
     * think.
     *
     * @param mt MIME type to connect units for
     * @throws ``mime::InvalidType`` if the type cannot be parsed
     */
    void connect_mime_type(const std::string& mt) { connect_mime_type(MIMEType(mt)); }

    /**
     * Connets instances of all units to the sink that support a given MIME
     * type. The units will then all receive any data written into the think.
     *
     * @param mt MIME type to connect units for
     * @throws ``mime::InvalidType`` if the type cannot be parsed
     */
    void connect_mime_type(const hilti::rt::Bytes& mt) { connect_mime_type(MIMEType(mt.str())); }

    /**
     * Reports a gap in the input stream.
     *
     * @param seq absolute sequence number of the gap
     * @param len length of the gap
     */
    void gap(uint64_t seq, uint64_t len);

    /**
     * Returns the current position in the sequence space.
     */
    uint64_t sequence_number() const { return _initial_seq + _cur_rseq; }

    /**
     * Enable/disable automatic trimming.
     *
     * @param enable true to enable trimming, false to disable
     */
    void set_auto_trim(bool enable) { _auto_trim = enable; }

    /**
     * Sets the initial sequence number.
     *
     * @param seq absolute sequence number to associate with 1st byte of input.
     */
    void set_initial_sequence_number(uint64_t seq) {
        if ( _haveInput() ) {
            _close(false);
            throw sink::Error("sink cannot update initial sequence number after activity has already been seen");
        }

        _initial_seq = seq;
    }

    /** Sets the sink's reassembler policy. */
    void set_policy(sink::ReassemblerPolicy policy) { _policy = policy; }

    /**
     * Returns the number of bytes written into the sink so far.
     */
    uint64_t size() const { return _size; }

    /**
     * Skips ahead in the input stream.
     *
     * @param seq absolute sequence number to skip ahead to
     */
    void skip(uint64_t seq);

    /**
     * Trims buffered input.
     *
     * @param seq absolute sequence number to trim up to.
     */
    void trim(uint64_t seq);

    /**
     * Writes data to the sink, forwarding it to all connected units.
     *
     * @param data data to write
     * @param seq absolute sequence number; defaults to end of current input
     * @param len length in sequence space; defaults to length of *data*
     */
    void write(hilti::rt::Bytes data, std::optional<uint64_t> seq = {}, std::optional<uint64_t> len = {});

    /**
     * Tracks connected filters. This is internal, but needs to be public
     * because some free-standing functions are accessing it.
     *
     * \todo(robin): We could probably declared the corresponding instantiations
     * as friends.
     */
    filter::State<sink::detail::sink_name> _filter;

private:
    struct Chunk {
        std::optional<hilti::rt::Bytes> data; // Data at +1; unset for gap
        uint64_t rseq;                        // Sequence number of first byte.
        uint64_t rupper;                      // Sequence number of last byte + 1.

        Chunk(std::optional<hilti::rt::Bytes> data, uint64_t rseq, uint64_t rupper)
            : data(std::move(data)), rseq(rseq), rupper(rupper) {}
    };

    using ChunkList = std::list<Chunk>;

    // Returns true if any input has been passed in already (including gaps).
    bool _haveInput() { return _cur_rseq || _chunks.size(); }

    // Backend for disconnecting the sink. If orderly, connected units get a
    // chance to parse any remaining input; otherwise we abort directly.
    void _close(bool orderly);

    // Turns an absolute sequence number into a relative one.
    uint64_t _rseq(uint64_t seq) const {
        // I believe this does the right thing for wrap-around ...
        return seq - _initial_seq;
    }

    // Turns a relative sequence number into an absolute one.
    uint64_t _aseq(uint64_t rseq) const {
        // I believe this does the right thing for wrap-around ...
        return _initial_seq + rseq;
    }

    // (Re-)initialize instance.
    void _init();

    // Add new data to buffer, beginning search for insert position at given start *c*.
    ChunkList::iterator _addAndCheck(std::optional<hilti::rt::Bytes> data, uint64_t rseq, uint64_t rupper,
                                     ChunkList::iterator c);

    // Deliver data to connected parsers. Returns false if the data is empty (i.e., a gap).
    bool _deliver(std::optional<hilti::rt::Bytes> data, uint64_t rseq, uint64_t rupper);

    // Entry point for all new data. If not bytes instance is given, that signals a gap.
    void _newData(std::optional<hilti::rt::Bytes> data, uint64_t rseq, uint64_t len);

    // Skip up to sequence number.
    void _skip(uint64_t rseq);

    // Trim up to sequence number.
    void _trim(uint64_t rseq);

    // Deliver as much as possible starting at given buffer position.
    void _tryDeliver(ChunkList::iterator c);

    // Trigger various hooks.
    void _reportGap(uint64_t rseq, uint64_t len) const;
    void _reportOverlap(uint64_t rseq, const hilti::rt::Bytes& old, const hilti::rt::Bytes& new_) const;
    void _reportSkipped(uint64_t rseq) const;
    void _reportUndelivered(uint64_t rseq, const hilti::rt::Bytes& data) const;
    void _reportUndeliveredUpTo(uint64_t rupper) const;

    // Output reassembler state for debugging.
    void _debugReassembler(const std::string& msg, const std::optional<hilti::rt::Bytes>& data, uint64_t seq,
                           uint64_t len) const;
    void _debugReassemblerBuffer(const std::string& msg) const;
    void _debugDeliver(const hilti::rt::Bytes& data) const;

    // States for connected units.
    std::vector<sink::detail::State*> _states;

    // Must come after `_state` as it's keeping the states around.
    std::vector<hilti::rt::StrongReferenceGeneric> _units;

    // Filter input and output.
    struct FilterData {
        hilti::rt::ValueReference<hilti::rt::Stream> input;
        hilti::rt::StrongReference<hilti::rt::Stream> output;
        hilti::rt::stream::View output_cur;
    };

    std::optional<FilterData> _filter_data;

    // Reassembly state.
    sink::ReassemblerPolicy _policy; // Current policy
    bool _auto_trim{};               // True if automatic trimming is enabled.
    uint64_t _size{};
    uint64_t _initial_seq{};       // Initial sequence number.
    uint64_t _cur_rseq{};          // Sequence of last delivered byte + 1 (i.e., seq of next)
    uint64_t _last_reassem_rseq{}; // Sequence of last byte reassembled and delivered + 1.
    uint64_t _trim_rseq{};         // Sequence of last byte trimmed so far + 1.
    ChunkList _chunks;             // Buffered data not yet delivered or trimmed
};

} // namespace spicy::rt

namespace hilti::rt::detail::adl {
extern inline std::string to_string(const spicy::rt::Sink& /* x */, adl::tag /*unused*/) { return "<sink>"; }
} // namespace hilti::rt::detail::adl
