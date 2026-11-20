#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <plutonium_sdk.hpp>
#include "grenade_aid.hpp"
#include <atomic>
#include <mutex>
#include "gsc_args.hpp"

namespace grenade
{
    namespace
    {
        std::mutex guard;
        std::atomic<bool> ready{false};
        View current;
        // The prediction being published (Begin to End). Only End makes it current.
        Path pending;
        int pending_seq = 0;

        bool kind_valid(int kind) { return kind > none && kind < kinds; }

        void begin_impl()
        {
            gsc_args::Arguments args;
            int kind = 0, seq = 0, pulled = 0;
            if (!args.capture(3) || !args.integer(0, kind) || !args.integer(1, seq) || !args.integer(2, pulled) || !args.unchanged())
                return;
            if (!kind_valid(kind) || seq <= 0 || pulled < 0 || pulled > 1) return;
            std::lock_guard lock(guard);
            pending = Path{};
            pending.kind = kind;
            pending.id = seq;
            pending.pulled = pulled == 1;
            pending_seq = seq;
        }

        void point_impl()
        {
            gsc_args::Arguments args;
            int seq = 0, flags = 0;
            nav::Vec3 point{};
            if (!args.capture(3) || !args.integer(0, seq) || !args.vector(1, point) || !args.integer(2, flags) || !args.unchanged()) return;
            if (seq <= 0 || flags < 0 || flags > (bounce | rest | burst)) return;
            std::lock_guard lock(guard);
            if (seq == pending_seq) append(pending, point, static_cast<std::uint8_t>(flags));
        }

        void strike_impl()
        {
            gsc_args::Arguments args;
            int seq = 0, index = 0;
            nav::Vec3 point{};
            if (!args.capture(3) || !args.integer(0, seq) || !args.integer(1, index) || !args.vector(2, point) || !args.unchanged()) return;
            if (seq <= 0 || index < 0 || index >= static_cast<int>(strike_count)) return;
            std::lock_guard lock(guard);
            if (seq != pending_seq) return;
            pending.strike[static_cast<std::size_t>(index)] = point;
            if (pending.strikes < static_cast<unsigned>(index) + 1) pending.strikes = static_cast<unsigned>(index) + 1;
        }

        void end_impl()
        {
            gsc_args::Arguments args;
            int seq = 0, valid = 0;
            float fuse = 0;
            if (!args.capture(3) || !args.integer(0, seq) || !args.number(1, fuse) || !args.integer(2, valid) || !args.unchanged()) return;
            if (seq <= 0 || valid < 0 || valid > 1 || !(fuse >= -1 && fuse <= 60)) return;
            std::lock_guard lock(guard);
            if (seq != pending_seq || pending.count == 0) return;
            pending.fuse = fuse;
            pending.verdict = valid;
            pending.ended = true;
            pending.updated = GetTickCount64();
            current.aim[static_cast<std::size_t>(pending.kind)] = pending;
            pending_seq = 0;
        }

        void flight_impl()
        {
            gsc_args::Arguments args;
            int id = 0, kind = 0, event = 0;
            nav::Vec3 point{};
            if (!args.capture(4) || !args.integer(0, id) || !args.integer(1, kind) || !args.vector(2, point) || !args.integer(3, event) ||
                !args.unchanged())
                return;
            if (id <= 0 || !kind_valid(kind) || event < 0 || event > static_cast<int>(Event::dud)) return;
            const auto now = GetTickCount64();
            std::lock_guard lock(guard);
            auto& f = current.flight;
            if (f.id != id)
            {
                // A new throw: keep what was predicted for it just before, to compare.
                const auto& aim = current.aim[static_cast<std::size_t>(kind)];
                current.thrown = fresh(aim.updated, now, aim_ms) ? aim : Path{};
                f = Path{};
                f.id = id;
                f.kind = kind;
            }
            switch (static_cast<Event>(event))
            {
            case Event::position:
                if (!f.ended) append(f, point, 0);
                break;
            case Event::bounced:
                if (!f.ended) append(f, point, bounce);
                break;
            case Event::rested:
                append(f, point, rest);
                f.ended = true;
                break;
            case Event::exploded:
                append(f, point, burst);
                f.ended = true;
                break;
            case Event::alive: break;
            case Event::accepted: f.verdict = 1; break;
            case Event::dud: f.verdict = 0; break;
            }
            f.updated = now;
        }

        void flight_strike_impl()
        {
            gsc_args::Arguments args;
            int id = 0, index = 0;
            nav::Vec3 point{};
            if (!args.capture(3) || !args.integer(0, id) || !args.integer(1, index) || !args.vector(2, point) || !args.unchanged()) return;
            if (id <= 0 || index < 0 || index >= static_cast<int>(strike_count)) return;
            std::lock_guard lock(guard);
            auto& f = current.flight;
            if (f.id != id) return;
            f.strike[static_cast<std::size_t>(index)] = point;
            if (f.strikes < static_cast<unsigned>(index) + 1) f.strikes = static_cast<unsigned>(index) + 1;
        }

        void radii_impl()
        {
            gsc_args::Arguments args;
            int kind = 0;
            float inner = 0, outer = 0;
            if (!args.capture(3) || !args.integer(0, kind) || !args.number(1, inner) || !args.number(2, outer) || !args.unchanged()) return;
            if (kind != emp || !(inner >= 0 && inner <= 10000) || !(outer >= 0 && outer <= 10000)) return;
            std::lock_guard lock(guard);
            current.emp_inner = inner;
            current.emp_outer = outer;
        }

        template<void (*Callback)()> void invoke() noexcept
        {
            if (!ready.load()) return;
            try
            {
                Callback();
            }
            catch (...)
            {
                clear();
            }
        }

        void PLUTONIUM_CALLBACK begin_callback() { invoke<begin_impl>(); }

        void PLUTONIUM_CALLBACK point_callback() { invoke<point_impl>(); }

        void PLUTONIUM_CALLBACK strike_callback() { invoke<strike_impl>(); }

        void PLUTONIUM_CALLBACK end_callback() { invoke<end_impl>(); }

        void PLUTONIUM_CALLBACK flight_callback() { invoke<flight_impl>(); }

        void PLUTONIUM_CALLBACK flight_strike_callback() { invoke<flight_strike_impl>(); }

        void PLUTONIUM_CALLBACK radii_callback() { invoke<radii_impl>(); }
    } // namespace

    bool startup(plutonium::sdk::v1::iinterface* api)
    {
        ready = false;
        clear();
        if (!api) return false;
        // Registered before the check, as in staff_aid. Mismatched builds ignore the calls.
        api->gsc()->register_function("GnosisThrowBegin", begin_callback);
        api->gsc()->register_function("GnosisThrowPoint", point_callback);
        api->gsc()->register_function("GnosisThrowStrike", strike_callback);
        api->gsc()->register_function("GnosisThrowEnd", end_callback);
        api->gsc()->register_function("GnosisThrowFlight", flight_callback);
        api->gsc()->register_function("GnosisThrowFlightStrike", flight_strike_callback);
        api->gsc()->register_function("GnosisThrowRadii", radii_callback);
        ready = gsc_args::bindings_match();
        return ready;
    }

    void clear()
    {
        std::lock_guard lock(guard);
        current = View{};
        pending = Path{};
        pending_seq = 0;
    }

    View snapshot()
    {
        std::lock_guard lock(guard);
        return current;
    }
} // namespace grenade
